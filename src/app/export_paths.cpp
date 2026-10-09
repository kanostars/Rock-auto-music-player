#include "export_paths.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace rock {
namespace {
const char* directoryKey(ExportKind kind) {
    return kind==ExportKind::Midi?"export/midiDirectory":"export/projectDirectory";
}

QString exportDirectory(ExportKind kind) {
    const auto saved=QSettings().value(directoryKey(kind)).toString();
    if(!saved.isEmpty())return QDir(saved).absolutePath();
    auto documents=QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if(documents.isEmpty())documents=QDir::homePath();
    return QDir(documents).filePath(kind==ExportKind::Midi?"RockAutoMusicPlay/MIDI":"RockAutoMusicPlay/工程");
}
}

bool prepareExportDirectory(ExportKind kind,QString& directory,QString& error) {
    directory=exportDirectory(kind);error.clear();
    const QFileInfo existing(directory);
    if(existing.exists()&&!existing.isDir()) {
        error=QString("导出目录被同名文件占用：\n%1\n请移走该文件后重试。").arg(QDir::toNativeSeparators(directory));
        return false;
    }
    if(!QDir().mkpath(directory)) {
        error=QString("无法创建导出目录：\n%1\n请检查路径和目录权限。").arg(QDir::toNativeSeparators(directory));
        return false;
    }
    QTemporaryFile writableCheck(QDir(directory).filePath(".rock-export-XXXXXX"));
    if(!writableCheck.open()) {
        error=QString("无法写入导出目录：\n%1\n%2").arg(QDir::toNativeSeparators(directory),writableCheck.errorString());
        return false;
    }
    return true;
}

void rememberExportDirectory(ExportKind kind,const QString& savedFilePath) {
    if(savedFilePath.isEmpty())return;
    const QFileInfo savedFile(savedFilePath);
    if(savedFile.isFile())QSettings().setValue(directoryKey(kind),savedFile.absolutePath());
}
}
