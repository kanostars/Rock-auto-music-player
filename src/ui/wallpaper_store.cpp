#include "wallpaper_store.h"
#include "wallpaper_layer.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>

namespace rock {
namespace {
const QSet<QString> imageExtensions{"png","jpg","jpeg","bmp","gif","ico"};
const QSet<QString> videoExtensions{"mp4","m4v","mov","wmv","avi"};
bool validId(const QString& id){const QUuid value(id);return !value.isNull()&&value.toString(QUuid::WithoutBraces)==id;}
QString imageError(const QString& name,const QString& reason){return name+"："+reason;}
bool managedFileName(const QString& name){
    if(!validId(name.left(36)))return false;const auto suffix=name.mid(36);
    return suffix==".thumb.png"||(suffix.startsWith('.')&&(imageExtensions.contains(suffix.mid(1))||videoExtensions.contains(suffix.mid(1))));
}
}
WallpaperStore& WallpaperStore::instance(){static auto* store=new WallpaperStore(QCoreApplication::instance());return *store;}
WallpaperStore::WallpaperStore(QObject* parent):QObject(parent){
    directory_=QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("wallpapers");
    directory_=QDir(directory_).absolutePath();load();QTimer::singleShot(0,this,&WallpaperStore::cleanupPending);
    connect(&watcher_,&QFutureWatcher<Batch>::finished,this,[this]{finishImport(true);});
}
WallpaperStore::~WallpaperStore(){if(cancelled_)cancelled_->store(true);watcher_.waitForFinished();finishImport(false);}
void WallpaperStore::finishImport(bool notify){
    if(!importPending_)return;auto result=watcher_.result();importPending_=false;const int count=static_cast<int>(result.items.size());
    for(auto& item:result.items){digests_[item.entry.id]=item.digest;entries_.push_back(std::move(item.entry));}
    save();if(notify){emit busyChanged(false);if(count)emit changed();emit importFinished(count,result.errors);}
}
const WallpaperEntry* WallpaperStore::active() const{
    const auto found=std::find_if(entries_.begin(),entries_.end(),[this](const auto& entry){return entry.id==activeId_;});
    return found!=entries_.end()&&QFileInfo::exists(found->path)?&*found:nullptr;
}
void WallpaperStore::load(){
    const QSettings settings;const auto document=QJsonDocument::fromJson(settings.value("wallpaper/library").toByteArray());
    if(document.isArray())for(const auto& value:document.array()){
        const auto object=value.toObject();const auto id=object["id"].toString(),extension=object["extension"].toString();
        if(!validId(id)||(!imageExtensions.contains(extension)&&!videoExtensions.contains(extension)))continue;
        const auto path=QDir(directory_).filePath(id+"."+extension);if(!QFileInfo(path).isFile())continue;
        if(std::any_of(entries_.begin(),entries_.end(),[&](const auto& item){return item.id==id;}))continue;
        const auto kind=videoExtensions.contains(extension)?WallpaperKind::Video:extension=="gif"?WallpaperKind::Gif:WallpaperKind::Image;
        entries_.push_back({id,object["name"].toString().left(512),path,QDir(directory_).filePath(id+".thumb.png"),kind});
        digests_[id]=object["digest"].toString();
    }
    activeId_=settings.value("wallpaper/activeId").toString();if(!transparent()&&!active())activeId_.clear();
    transparency_=std::clamp(settings.value("wallpaper/transparency",65).toInt(),0,100);
    for(const auto& file:settings.value("wallpaper/pendingRemovals").toStringList())if(managedFileName(file))pendingRemovals_.push_back(file);
}
void WallpaperStore::save() const{
    QJsonArray list;for(const auto& entry:entries_){
        const auto digest=digests_.find(entry.id);
        list.append(QJsonObject{{"id",entry.id},{"name",entry.name},{"extension",QFileInfo(entry.path).suffix().toLower()},
                                {"digest",digest==digests_.end()?QString():digest->second}});
    }
    QSettings settings;settings.setValue("wallpaper/library",QJsonDocument(list).toJson(QJsonDocument::Compact));settings.setValue("wallpaper/activeId",activeId_);settings.setValue("wallpaper/transparency",transparency_);settings.setValue("wallpaper/pendingRemovals",pendingRemovals_);
}
void WallpaperStore::cleanupPending(){
    if(pendingRemovals_.isEmpty())return;
    pendingRemovals_.erase(std::remove_if(pendingRemovals_.begin(),pendingRemovals_.end(),[this](const auto& file){const auto path=QDir(directory_).filePath(file);return !QFileInfo::exists(path)||QFile::remove(path);}),pendingRemovals_.end());
    save();if(!pendingRemovals_.isEmpty()&&++cleanupAttempts_<40)QTimer::singleShot(100,this,&WallpaperStore::cleanupPending);
}
bool WallpaperStore::select(const QString& id){
    const auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& entry){return entry.id==id;});
    if(found==entries_.end()||!QFileInfo::exists(found->path))return false;
    if(activeId_==id)return true;activeId_=id;save();emit activeChanged();emit changed();return true;
}
void WallpaperStore::selectTransparent(){
    if(transparent())return;activeId_=QStringLiteral("builtin-transparent");save();emit activeChanged();emit changed();
}
void WallpaperStore::setTransparency(int value){
    value=std::clamp(value,0,100);if(transparency_==value)return;transparency_=value;
    QSettings().setValue("wallpaper/transparency",transparency_);emit transparencyChanged();
}
bool WallpaperStore::rename(const QString& id,const QString& name){
    const auto displayName=name.trimmed();if(displayName.isEmpty()||displayName.size()>512)return false;
    const auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& entry){return entry.id==id;});
    if(found==entries_.end())return false;if(found->name==displayName)return true;
    found->name=displayName;save();emit changed();return true;
}
void WallpaperStore::clearSelection(){if(activeId_.isEmpty())return;activeId_.clear();save();emit activeChanged();emit changed();}
bool WallpaperStore::remove(const QString& id){
    if(busy())return false;
    const auto found=std::find_if(entries_.begin(),entries_.end(),[&](const auto& entry){return entry.id==id;});
    if(found==entries_.end())return false;
    const bool selected=activeId_==id;
    // Release GIF/video readers before removing only the app-owned copies.
    if(selected){activeId_.clear();emit activeChanged();}
    for(const auto& path:{found->path,found->thumbnail})if(QFileInfo::exists(path)&&!QFile::remove(path))pendingRemovals_.push_back(QFileInfo(path).fileName());
    digests_.erase(id);entries_.erase(found);save();emit changed();
    if(!pendingRemovals_.isEmpty()){cleanupAttempts_=0;QTimer::singleShot(100,this,&WallpaperStore::cleanupPending);}return true;
}
void WallpaperStore::importFiles(const QStringList& paths){
    if(busy()||paths.isEmpty())return;
    const auto directory=directory_;QSet<QString> existing;for(const auto& [id,digest]:digests_)if(!digest.isEmpty())existing.insert(digest);
    cancelled_=std::make_shared<std::atomic_bool>(false);const auto cancelled=cancelled_;
    importPending_=true;
    emit busyChanged(true);
    watcher_.setFuture(QtConcurrent::run([directory,paths,existing=std::move(existing),cancelled]() mutable{
        Batch batch;
        if(!QDir().mkpath(directory)){batch.errors<<"无法创建壁纸保存目录。";return batch;}
        for(const auto& path:paths){
            if(cancelled->load())break;
            const QFileInfo source(path);const auto name=source.fileName();
            if(!source.isFile()||source.size()<=0){batch.errors<<imageError(name,"文件不存在或为空。");continue;}
            if(source.size()>qint64(2)*1024*1024*1024){batch.errors<<imageError(name,"请选择小于 2 GB 的文件。");continue;}
            auto extension=source.suffix().toLower();QImage thumbnail;WallpaperKind kind;
            if(videoExtensions.contains(extension)){
                kind=WallpaperKind::Video;QString error;
                if(!wallpaperVideoThumbnail(path,thumbnail,error)){batch.errors<<imageError(name,error);continue;}
            }else{
                QImageReader reader(path);reader.setAutoTransform(true);const auto format=QString::fromLatin1(reader.format()).toLower();
                if(!reader.canRead()||!imageExtensions.contains(format)){batch.errors<<imageError(name,"请选择支持的图片、GIF 或视频文件。");continue;}
                extension=format=="jpeg"?QString("jpg"):format;const auto size=reader.size();
                if(!size.isValid()||size.width()>32768||size.height()>32768||qint64(size.width())*size.height()>64000000){batch.errors<<imageError(name,"图片尺寸过大，请先缩小图片。");continue;}
                reader.setScaledSize(size.scaled(640,360,Qt::KeepAspectRatio));thumbnail=reader.read();kind=extension=="gif"?WallpaperKind::Gif:WallpaperKind::Image;
                if(thumbnail.isNull()){batch.errors<<imageError(name,"图片解码失败。");continue;}
            }
            thumbnail=thumbnail.scaled(640,360,Qt::KeepAspectRatio,Qt::SmoothTransformation);
            const auto id=QUuid::createUuid().toString(QUuid::WithoutBraces);const auto destination=QDir(directory).filePath(id+"."+extension),thumb=QDir(directory).filePath(id+".thumb.png");
            QFile input(path);QSaveFile output(destination);QCryptographicHash hash(QCryptographicHash::Sha256);bool copied=true;
            if(!input.open(QIODevice::ReadOnly)||!output.open(QIODevice::WriteOnly)){batch.errors<<imageError(name,"无法读取或保存壁纸。");continue;}
            while(!input.atEnd()){
                const auto bytes=input.read(1024*1024);
                if(cancelled->load()||bytes.isEmpty()||output.write(bytes)!=bytes.size()){copied=false;break;}hash.addData(bytes);
            }
            if(!copied||input.error()!=QFileDevice::NoError){output.cancelWriting();if(!cancelled->load())batch.errors<<imageError(name,"复制壁纸失败，请重试。");continue;}
            const auto digest=QString::fromLatin1(hash.result().toHex());
            if(existing.contains(digest)){output.cancelWriting();batch.errors<<imageError(name,"壁纸库中已存在，已跳过。");continue;}
            if(!output.commit()){batch.errors<<imageError(name,"保存壁纸失败。");continue;}
            if(!thumbnail.save(thumb,"PNG")){QFile::remove(destination);batch.errors<<imageError(name,"无法保存壁纸缩略图。");continue;}
            existing.insert(digest);batch.items.push_back({{id,source.completeBaseName().left(512),destination,thumb,kind},digest});
        }
        return batch;
    }));
}
}
