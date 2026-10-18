#pragma once
#include <QFutureWatcher>
#include <QObject>
#include <QStringList>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace rock {
enum class WallpaperKind {Image,Gif,Video};
struct WallpaperEntry {
    QString id,name,path,thumbnail;
    WallpaperKind kind{WallpaperKind::Image};
};
class WallpaperStore:public QObject {
    Q_OBJECT
public:
    static WallpaperStore& instance();
    ~WallpaperStore() override;
    const std::vector<WallpaperEntry>& entries() const{return entries_;}
    QString activeId() const{return activeId_;}
    const WallpaperEntry* active() const;
    bool busy() const{return importPending_;}
    void importFiles(const QStringList& paths);
    bool select(const QString& id);
    bool rename(const QString& id,const QString& name);
    bool remove(const QString& id);
    void clearSelection();
signals:
    void changed();
    void activeChanged();
    void importFinished(int added,const QStringList& errors);
    void busyChanged(bool busy);
private:
    struct Imported {WallpaperEntry entry;QString digest;};
    struct Batch {std::vector<Imported> items;QStringList errors;};
    explicit WallpaperStore(QObject* parent);
    std::vector<WallpaperEntry> entries_;
    std::map<QString,QString> digests_;
    QString directory_,activeId_;
    QStringList pendingRemovals_;
    int cleanupAttempts_{};
    bool importPending_{};
    QFutureWatcher<Batch> watcher_;
    std::shared_ptr<std::atomic_bool> cancelled_;
    void load();
    void save() const;
    void cleanupPending();
    void finishImport(bool notify);
};
}
