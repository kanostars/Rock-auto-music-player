#include "audio_player.h"
#include <QFile>
#include <QResource>
#include <miniaudio.h>
#include <atomic>
#include <stdexcept>

static void initSoundbank() {Q_INIT_RESOURCE(soundbank);}
namespace rock {
SampleBank loadHandpanBank() {
    initSoundbank();SampleBank bank;
    for(size_t i=0;i<bank.size();++i)for(int variant=0;variant<sampleVariants;++variant) {
        QFile file(QString(":/handpan/%1_%2.wav").arg(QChar(keys[i])).arg(variant+1));
        if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("无法读取内置手盘采样。");
        auto bytes=file.readAll();auto config=ma_decoder_config_init(ma_format_f32,2,audioRate);
        ma_uint64 count=0;void* pcm=nullptr;
        auto result=ma_decode_memory(bytes.constData(),bytes.size(),&config,&count,&pcm);
        std::unique_ptr<void,void(*)(void*)> decoded(pcm,[](void* p){ma_free(p,nullptr);});
        if(result!=MA_SUCCESS||count==0||count>audioRate*10)throw std::runtime_error("内置手盘采样解码失败。");
        auto* data=static_cast<float*>(pcm);bank[i][variant].assign(data,data+count*2);
    }
    return bank;
}
struct AudioPlayer::Impl {
    explicit Impl(AudioBackend b):backend(b){}
    AudioBackend backend;
    ma_device device{};
    bool initialized{};
    std::unique_ptr<HandpanMixer> mixer;
    std::atomic<uint64_t> cursor{},end{};
    std::atomic<float> volume{.6f};
    static void callback(ma_device* device,void* output,const void*,ma_uint32 frames) {
        auto& self=*static_cast<Impl*>(device->pUserData);
        self.mixer->render(static_cast<float*>(output),frames,self.volume.load(std::memory_order_relaxed));
        self.cursor.store(self.mixer->cursor(),std::memory_order_relaxed);
    }
    ~Impl(){if(initialized)ma_device_uninit(&device);}
};
AudioPlayer::AudioPlayer(AudioBackend backend):impl_(std::make_unique<Impl>(backend)){}
AudioPlayer::~AudioPlayer()=default;
bool AudioPlayer::play(const Song& song,const Conversion& result,double seconds,QString& error) {
    pause();auto& p=*impl_;
    if(result.conflicts>0) {
        error=QString("存在 %1 处同键冲突，无法播放音频。请消除冲突并重新转换。").arg(result.conflicts);
        return false;
    }
    try {
        if(!p.mixer)p.mixer=std::make_unique<HandpanMixer>(loadHandpanBank());
        p.mixer->prepare(song,result);p.mixer->seek(seconds);
    } catch(const std::exception& e){error=QString::fromUtf8(e.what());return false;}
    p.cursor.store(p.mixer->cursor());p.end.store(p.mixer->endFrame());
    if(!p.initialized) {
        auto config=ma_device_config_init(ma_device_type_playback);
        config.playback.format=ma_format_f32;config.playback.channels=2;config.sampleRate=audioRate;
        config.periodSizeInMilliseconds=10;config.dataCallback=Impl::callback;config.pUserData=&p;
        const ma_backend systemBackends[]{ma_backend_wasapi,ma_backend_dsound,ma_backend_winmm};
        const ma_backend nullBackend=ma_backend_null;
        // Never silently fall back to a null device in the real application.
        auto result=p.backend==AudioBackend::NullTest?ma_device_init_ex(&nullBackend,1,nullptr,&config,&p.device):
            ma_device_init_ex(systemBackends,3,nullptr,&config,&p.device);
        if(result!=MA_SUCCESS){error=QString("无法打开音频输出设备（%1）。请检查扬声器/耳机和系统默认输出后重试。").arg(result);return false;}
        p.initialized=true;
    }
    auto started=ma_device_start(&p.device);
    if(started!=MA_SUCCESS) {
        ma_device_uninit(&p.device);p.initialized=false;
        error=QString("音频启动失败（%1），请检查系统输出设备后重试。").arg(started);return false;
    }
    error.clear();return true;
}
void AudioPlayer::pause(){if(impl_->initialized)ma_device_stop(&impl_->device);}
void AudioPlayer::setVolume(float volume){impl_->volume.store(volume,std::memory_order_relaxed);}
double AudioPlayer::position() const{return double(impl_->cursor.load(std::memory_order_relaxed))/audioRate;}
bool AudioPlayer::finished() const{return impl_->cursor.load(std::memory_order_relaxed)>=impl_->end.load(std::memory_order_relaxed);}
bool AudioPlayer::running() const{return impl_->initialized&&ma_device_is_started(&impl_->device);}
}
