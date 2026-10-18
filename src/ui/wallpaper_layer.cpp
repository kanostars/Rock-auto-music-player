#include "wallpaper_layer.h"
#include "wallpaper_store.h"
#include "theme.h"

#include <QEvent>
#include <QFileInfo>
#include <QHideEvent>
#include <QImageReader>
#include <QMovie>
#include <QPainter>
#include <QPointer>
#include <QShowEvent>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#endif

namespace rock {
namespace {

QSize boundedSize(QSize source,QSize limit=QSize(1920,1080)) {
    return source.isValid()?source.scaled(limit,Qt::KeepAspectRatio):QSize();
}

#ifdef Q_OS_WIN
template<class T> class ComPtr {
public:
    ~ComPtr(){reset();}
    ComPtr()=default;
    ComPtr(const ComPtr&)=delete;
    ComPtr& operator=(const ComPtr&)=delete;
    T* operator->()const{return value_;}
    explicit operator bool()const{return value_!=nullptr;}
    T* get()const{return value_;}
    T** put(){reset();return &value_;}
    void reset(){if(value_){value_->Release();value_=nullptr;}}
private:
    T* value_{};
};

class MediaScope {
public:
    MediaScope() {
        com_=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if(SUCCEEDED(com_)||com_==RPC_E_CHANGED_MODE) result_=MFStartup(MF_VERSION,MFSTARTUP_LITE);
        else result_=com_;
    }
    ~MediaScope(){if(SUCCEEDED(result_))MFShutdown();if(SUCCEEDED(com_))CoUninitialize();}
    HRESULT result()const{return result_;}
private:
    HRESULT com_{E_FAIL},result_{E_FAIL};
};

QString mediaError(HRESULT result) {
    return QStringLiteral("无法读取视频壁纸（Windows 媒体组件 0x%1），请尝试 MP4 / H.264 视频。")
        .arg(qulonglong(static_cast<unsigned long>(result)),8,16,QChar('0'));
}

class NativeVideo {
public:
    HRESULT open(const QString& path,QSize limit=QSize(1920,1080)) {
        ComPtr<IMFAttributes> attributes;
        HRESULT result=MFCreateAttributes(attributes.put(),2);
        if(FAILED(result))return result;
        attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING,TRUE);
        const auto nativePath=QFileInfo(path).absoluteFilePath().toStdWString();
        result=MFCreateSourceReaderFromURL(nativePath.c_str(),attributes.get(),reader_.put());
        if(FAILED(result))return result;
        // No audio samples are selected or submitted to an audio device.
        result=reader_->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS,FALSE);
        if(FAILED(result))return result;
        result=reader_->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM,TRUE);
        if(FAILED(result))return result;
        ComPtr<IMFMediaType> output,native;
        result=MFCreateMediaType(output.put());
        if(FAILED(result))return result;
        output->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);
        output->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_RGB32);
        if(SUCCEEDED(reader_->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,native.put()))) {
            UINT32 width{},height{};
            if(SUCCEEDED(MFGetAttributeSize(native.get(),MF_MT_FRAME_SIZE,&width,&height))&&width&&height&&width<=32768&&height<=32768) {
                const auto size=boundedSize(QSize(int(width),int(height)),limit);
                if(size.width()<int(width)||size.height()<int(height))
                    MFSetAttributeSize(output.get(),MF_MT_FRAME_SIZE,UINT32(size.width()),UINT32(size.height()));
            }
        }
        result=reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,output.get());
        if(FAILED(result)) {
            // Some system decoders cannot scale in the SourceReader conversion.
            output->DeleteItem(MF_MT_FRAME_SIZE);
            result=reader_->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,nullptr,output.get());
        }
        if(FAILED(result))return result;
        return readFormat();
    }

    // S_FALSE marks end of stream. A successful frame owns its pixels.
    HRESULT read(QImage& image,LONGLONG& timestamp) {
        for(int attempt=0;attempt<128;++attempt) {
            DWORD flags{};
            ComPtr<IMFSample> sample;
            HRESULT result=reader_->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,0,nullptr,&flags,&timestamp,sample.put());
            if(FAILED(result))return result;
            if(flags&MF_SOURCE_READERF_ERROR)return E_FAIL;
            if(flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                result=readFormat();
                if(FAILED(result))return result;
            }
            if(!sample) {
                if(flags&MF_SOURCE_READERF_ENDOFSTREAM)return S_FALSE;
                continue;
            }
            ComPtr<IMFMediaBuffer> buffer;
            result=sample->ConvertToContiguousBuffer(buffer.put());
            if(FAILED(result))return result;
            LONGLONG sampleDuration{};
            if(SUCCEEDED(sample->GetSampleDuration(&sampleDuration))&&sampleDuration>0)
                frameDuration_=sampleDuration;
            image=QImage(int(width_),int(height_),QImage::Format_RGB32);
            if(image.isNull())return E_OUTOFMEMORY;
            ComPtr<IMF2DBuffer> surface;
            if(SUCCEEDED(buffer->QueryInterface(IID_IMF2DBuffer,reinterpret_cast<void**>(surface.put())))) {
                BYTE* first{};LONG pitch{};
                result=surface->Lock2D(&first,&pitch);
                if(SUCCEEDED(result)) {
                    if(std::abs(qint64(pitch))<qint64(width_)*4){surface->Unlock2D();return E_FAIL;}
                    for(UINT32 y=0;y<height_;++y)
                        std::memcpy(image.scanLine(int(y)),first+qint64(y)*pitch,size_t(width_)*4);
                    surface->Unlock2D();
                    return S_OK;
                }
            }
            BYTE* bytes{};DWORD capacity{},length{};
            result=buffer->Lock(&bytes,&capacity,&length);
            if(FAILED(result))return result;
            const auto pitch=qint64(stride_);
            const auto rowBytes=qint64(width_)*4;
            const auto required=std::abs(pitch)*qint64(height_-1)+rowBytes;
            if(std::abs(pitch)<rowBytes||required>length) {buffer->Unlock();return E_FAIL;}
            const BYTE* first=pitch<0?bytes+std::abs(pitch)*qint64(height_-1):bytes;
            for(UINT32 y=0;y<height_;++y)
                std::memcpy(image.scanLine(int(y)),first+qint64(y)*pitch,size_t(rowBytes));
            buffer->Unlock();
            return S_OK;
        }
        return MF_E_INVALID_STREAM_DATA;
    }

    HRESULT rewind() {
        PROPVARIANT position{};
        position.vt=VT_I8;
        position.hVal.QuadPart=0;
        return reader_->SetCurrentPosition(GUID_NULL,position);
    }
    LONGLONG frameDuration()const{return frameDuration_;}

private:
    HRESULT readFormat() {
        ComPtr<IMFMediaType> type;
        HRESULT result=reader_->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,type.put());
        if(FAILED(result))return result;
        result=MFGetAttributeSize(type.get(),MF_MT_FRAME_SIZE,&width_,&height_);
        if(FAILED(result))return result;
        if(!width_||!height_||width_>8192||height_>8192||quint64(width_)*height_>16777216)return MF_E_INVALIDMEDIATYPE;
        UINT32 stride{};
        if(SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE,&stride)))stride_=static_cast<LONG>(stride);
        else stride_=LONG(width_*4);
        UINT32 rate{},denominator{};
        if(SUCCEEDED(MFGetAttributeRatio(type.get(),MF_MT_FRAME_RATE,&rate,&denominator))&&rate&&denominator)
            frameDuration_=LONGLONG(10000000)*denominator/rate;
        return S_OK;
    }
    ComPtr<IMFSourceReader> reader_;
    UINT32 width_{},height_{};
    LONG stride_{};
    LONGLONG frameDuration_{10000000/30};
};
#endif

}

bool wallpaperVideoThumbnail(const QString& path,QImage& image,QString& error) {
    image={};error.clear();
#ifdef Q_OS_WIN
    MediaScope media;
    HRESULT result=media.result();
    if(SUCCEEDED(result)) {
        NativeVideo video;
        result=video.open(path,QSize(640,360));
        if(SUCCEEDED(result)) {LONGLONG timestamp{};result=video.read(image,timestamp);}
    }
    if(result==S_OK&&!image.isNull())return true;
    error=mediaError(result==S_FALSE?MF_E_INVALID_STREAM_DATA:result);
#else
    Q_UNUSED(path)
    error=QStringLiteral("当前平台不支持视频壁纸，请选择图片或 GIF。");
#endif
    return false;
}

// All windows subscribe to the same source: opening several settings windows
// never creates extra movie decoders, copies pixel buffers or adds audio output.
class WallpaperPlayback final : public QObject {
public:
    static std::shared_ptr<WallpaperPlayback> acquire();
    WallpaperPlayback();
    ~WallpaperPlayback() override;
    void attach(WallpaperLayer* layer);
    void detach(WallpaperLayer* layer);
    void syncPlayback();
private:
    struct VideoState;
    std::unique_ptr<VideoState> video_;
    QMovie* movie_{};
    QTimer* timer_{};
    QImage frame_;
    QString path_;
    bool videoSelected_{};
    std::vector<QPointer<WallpaperLayer>> layers_;
    void reload();
    void pollVideo();
    void publish();
    void reportError(const QString& message);
};

struct WallpaperPlayback::VideoState {
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    bool quit{},running{};
    quint64 generation{},frameVersion{},errorVersion{};
    quint64 shownFrame{},shownError{};
    QString path,error;
    QImage frame;

    // Start after every field is initialized; the worker can run immediately.
    VideoState(){worker=std::thread([this]{run();});}
    ~VideoState(){
        {std::lock_guard lock(mutex);quit=true;++generation;}
        wake.notify_all();
        if(worker.joinable())worker.join();
    }
    void request(const QString& nextPath,bool nextRunning) {
        {std::lock_guard lock(mutex);
            if(path==nextPath&&running==nextRunning)return;
            if(path!=nextPath){frame={};error.clear();++frameVersion;}
            path=nextPath;running=nextRunning;++generation;
        }
        wake.notify_all();
    }
    void run() {
#ifdef Q_OS_WIN
        MediaScope media;
        std::unique_lock lock(mutex);
        while(!quit) {
            wake.wait(lock,[this]{return quit||(running&&!path.isEmpty());});
            if(quit)break;
            const auto currentGeneration=generation;
            const auto currentPath=path;
            lock.unlock();
            NativeVideo video;
            HRESULT result=media.result();
            if(SUCCEEDED(result))result=video.open(currentPath);
            bool firstFrame=true,producedFrame=false;
            LONGLONG firstTimestamp{};
            std::chrono::steady_clock::time_point firstTime;
            std::chrono::steady_clock::time_point lastDeadline;
            while(SUCCEEDED(result)) {
                lock.lock();
                const bool cancelled=quit||generation!=currentGeneration||!running;
                lock.unlock();
                if(cancelled)break;
                QImage next;
                LONGLONG timestamp{};
                result=video.read(next,timestamp);
                if(result==S_FALSE) {
                    if(!producedFrame){result=MF_E_INVALID_STREAM_DATA;break;}
                    // Hold the last frame for its own duration before looping;
                    // even a one-frame clip must not turn into a busy loop.
                    lock.lock();
                    wake.wait_until(lock,lastDeadline+std::chrono::nanoseconds(std::clamp<LONGLONG>(video.frameDuration(),10000,100000000)*100),
                        [this,currentGeneration]{return quit||generation!=currentGeneration||!running;});
                    const bool cancelled=quit||generation!=currentGeneration||!running;
                    lock.unlock();
                    if(cancelled)break;
                    result=video.rewind();
                    firstFrame=true;producedFrame=false;
                    continue;
                }
                if(FAILED(result))break;
                if(firstFrame){firstFrame=false;firstTimestamp=timestamp;firstTime=std::chrono::steady_clock::now();}
                // Timestamp pacing uses a private monotonic clock and never the music transport.
                const auto elapsed=std::clamp<LONGLONG>(timestamp-firstTimestamp,0,24LL*60*60*10000000);
                const auto deadline=firstTime+std::chrono::nanoseconds(elapsed*100);
                lastDeadline=deadline;
                lock.lock();
                wake.wait_until(lock,deadline,[this,currentGeneration]{return quit||generation!=currentGeneration||!running;});
                if(quit||generation!=currentGeneration||!running){lock.unlock();break;}
                if(next.width()>1920||next.height()>1080) {
                    lock.unlock();
                    next=next.scaled(boundedSize(next.size()),Qt::KeepAspectRatio,Qt::SmoothTransformation);
                    lock.lock();
                    if(quit||generation!=currentGeneration||!running){lock.unlock();break;}
                }
                frame=std::move(next);++frameVersion;producedFrame=true;
                lock.unlock();
            }
            lock.lock();
            if(FAILED(result)&&generation==currentGeneration&&!quit) {
                error=mediaError(result);++errorVersion;
                // Retry only after a new visibility or selection request.
                running=false;
            }
        }
#else
        std::unique_lock lock(mutex);
        wake.wait(lock,[this]{return quit;});
#endif
    }
};

std::shared_ptr<WallpaperPlayback> WallpaperPlayback::acquire() {
    static std::weak_ptr<WallpaperPlayback> shared;
    auto source=shared.lock();
    if(!source){source=std::make_shared<WallpaperPlayback>();shared=source;}
    return source;
}

WallpaperPlayback::WallpaperPlayback():video_(std::make_unique<VideoState>()),timer_(new QTimer(this)) {
    timer_->setInterval(33);
    timer_->setTimerType(Qt::CoarseTimer);
    connect(timer_,&QTimer::timeout,this,&WallpaperPlayback::pollVideo);
    connect(&WallpaperStore::instance(),&WallpaperStore::activeChanged,this,&WallpaperPlayback::reload);
    reload();
}

WallpaperPlayback::~WallpaperPlayback()=default;

void WallpaperPlayback::attach(WallpaperLayer* layer) {
    layers_.push_back(layer);layer->frame_=frame_;layer->update();syncPlayback();
}

void WallpaperPlayback::detach(WallpaperLayer* layer) {
    std::erase_if(layers_,[layer](const auto& view){return !view||view==layer;});syncPlayback();
}

void WallpaperPlayback::publish() {
    for(const auto& view:layers_)if(view){view->frame_=frame_;view->update();}
}

void WallpaperPlayback::reportError(const QString& message) {
    for(const auto& view:layers_)if(view)emit view->playbackError(message);
}

void WallpaperPlayback::reload() {
    if(movie_){
        // A corrupt later GIF frame can trigger selection clearing from inside
        // QMovie's own error callback. Do not destroy that decoder on its stack.
        disconnect(movie_,nullptr,this,nullptr);movie_->stop();movie_->deleteLater();movie_=nullptr;
    }
    frame_={};path_.clear();videoSelected_=false;
    if(const auto* entry=WallpaperStore::instance().active()) {
        path_=entry->path;
        if(entry->kind==WallpaperKind::Video) {
            videoSelected_=true;
            frame_=QImage(entry->thumbnail);
        } else if(entry->kind==WallpaperKind::Gif) {
            movie_=new QMovie(path_,QByteArray(),this);
            movie_->setCacheMode(QMovie::CacheNone);
            QImageReader reader(path_);
            const auto size=reader.size();
            if(size.width()>1920||size.height()>1080)movie_->setScaledSize(boundedSize(size));
            connect(movie_,&QMovie::frameChanged,this,[this]{frame_=movie_->currentImage();publish();});
            connect(movie_,&QMovie::finished,this,[this]{syncPlayback();});
            connect(movie_,&QMovie::error,this,[this](QImageReader::ImageReaderError){reportError(QStringLiteral("无法播放 GIF 壁纸，请重新导入有效的 GIF 图片。"));});
            movie_->jumpToFrame(0);
        } else {
            QImageReader reader(path_);
            reader.setAutoTransform(true);
            const auto size=reader.size();
            if(size.width()>2560||size.height()>1440)reader.setScaledSize(boundedSize(size,QSize(2560,1440)));
            frame_=reader.read();
            if(frame_.isNull())reportError(QStringLiteral("壁纸图片无法读取，请重新导入。"));
        }
    }
    video_->request(videoSelected_?path_:QString(),false);
    syncPlayback();publish();
}

void WallpaperPlayback::syncPlayback() {
    const bool running=std::any_of(layers_.begin(),layers_.end(),[](const auto& view){return view&&view->isVisible()&&view->window()->isVisible()&&!view->window()->isMinimized();});
    if(movie_) {
        if(running){if(movie_->state()==QMovie::NotRunning)movie_->start();else movie_->setPaused(false);}
        else movie_->setPaused(true);
    }
    video_->request(videoSelected_?path_:QString(),videoSelected_&&running);
    if(videoSelected_&&running)timer_->start();else timer_->stop();
}

void WallpaperPlayback::pollVideo() {
    QString error;
    bool changed=false;
    {
        std::lock_guard lock(video_->mutex);
        if(video_->shownFrame!=video_->frameVersion) {
            video_->shownFrame=video_->frameVersion;
            if(!video_->frame.isNull()){frame_=video_->frame;changed=true;}
        }
        if(video_->shownError!=video_->errorVersion){video_->shownError=video_->errorVersion;error=video_->error;}
    }
    if(changed)publish();
    if(!error.isEmpty())reportError(error);
}

WallpaperLayer::WallpaperLayer(QWidget* parent):QWidget(parent),playback_(WallpaperPlayback::acquire()) {
    setObjectName(QStringLiteral("wallpaperLayer"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::NoFocus);
    if(parent){setGeometry(parent->rect());parent->installEventFilter(this);}
    if(window()!=parent&&window())window()->installEventFilter(this);
    lower();playback_->attach(this);
}

WallpaperLayer::~WallpaperLayer(){playback_->detach(this);}

void WallpaperLayer::syncPlayback(){playback_->syncPlayback();}

void WallpaperLayer::paintEvent(QPaintEvent*) {
    if(frame_.isNull()||width()<=0||height()<=0)return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const double scale=std::max(double(width())/frame_.width(),double(height())/frame_.height());
    const QSizeF sourceSize(width()/scale,height()/scale);
    const QRectF source((frame_.width()-sourceSize.width())/2,(frame_.height()-sourceSize.height())/2,sourceSize.width(),sourceSize.height());
    painter.drawImage(QRectF(rect()),frame_,source);
    const auto tintProperty=parentWidget()?parentWidget()->property("wallpaperTint"):QVariant();
    const double tint=tintProperty.isValid()?tintProperty.toDouble():(parentWidget()&&parentWidget()->isWindow()?.80:.28);
    auto veil=Theme::color("#eef3f5");veil.setAlphaF(std::clamp(tint,0.,1.));
    painter.fillRect(rect(),veil);
}

void WallpaperLayer::showEvent(QShowEvent* event){QWidget::showEvent(event);syncPlayback();}
void WallpaperLayer::hideEvent(QHideEvent* event){QWidget::hideEvent(event);syncPlayback();}

bool WallpaperLayer::eventFilter(QObject* watched,QEvent* event) {
    if(watched==parentWidget()&&event->type()==QEvent::Resize){setGeometry(parentWidget()->rect());lower();}
    if(event->type()==QEvent::WindowStateChange||event->type()==QEvent::Show||event->type()==QEvent::Hide)
        QTimer::singleShot(0,this,&WallpaperLayer::syncPlayback);
    if(event->type()==QEvent::StyleChange||event->type()==QEvent::PaletteChange||event->type()==QEvent::DynamicPropertyChange)update();
    return QWidget::eventFilter(watched,event);
}

}
