#include "app_log.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QtLogging>
#include <atomic>
#include <cstdio>

namespace rock {
namespace {
constexpr qsizetype maximumEntries = 3000;
constexpr qsizetype maximumMessageLength = 8192;
constexpr qint64 maximumFileSize = 2 * 1024 * 1024;
constexpr int archivedFiles = 4;
std::atomic<QtMessageHandler> previousMessageHandler{};
thread_local bool insideLogOperation = false;

struct LogOperationGuard {
    bool previous{insideLogOperation};
    LogOperationGuard() {insideLogOperation = true;}
    ~LogOperationGuard() {insideLogOperation = previous;}
};

QString boundedText(const QString& text, qsizetype maximumLength) {
    if (text.size() <= maximumLength)return text;
    qsizetype end = maximumLength;
    if (text.at(end - 1).isHighSurrogate())--end;
    return text.left(end) + QStringLiteral("…[已截断]");
}

QString singleLine(QString text) {
    text.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    text.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return text;
}

const char* levelName(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:return "DEBUG";
    case LogLevel::Info:return "INFO";
    case LogLevel::Warning:return "WARN";
    case LogLevel::Error:return "ERROR";
    }
    return "INFO";
}

void stderrMessage(const QString& message) {
    const auto bytes = message.toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

void qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    thread_local bool handling = false;
    if (handling || insideLogOperation) {
        stderrMessage(message);
        return;
    }
    struct HandlerGuard {
        bool& flag;
        explicit HandlerGuard(bool& value) : flag(value) {flag = true;}
        ~HandlerGuard() {flag = false;}
    } guard(handling);
    LogLevel level = LogLevel::Info;
    switch (type) {
    case QtDebugMsg:level = LogLevel::Debug;break;
    case QtInfoMsg:level = LogLevel::Info;break;
    case QtWarningMsg:level = LogLevel::Warning;break;
    case QtCriticalMsg:
    case QtFatalMsg:level = LogLevel::Error;break;
    }
    AppLog::instance().write(level, QString::fromUtf8(context.category ? context.category : "qt"), message);
    const auto previous = previousMessageHandler.load(std::memory_order_acquire);
    if (previous && previous != qtMessageHandler)previous(type, context, message);
    else stderrMessage(message);
}
}

struct AppLog::State {
    mutable QMutex mutex;
    QList<LogEntry> entries;
    quint64 sequence{};
    bool initialized{};
    bool fileEnabled{};
    QString path;
    QString error;
    QFile file;

    void failStorage(const QString& operation) {
        error = operation;
        if (!file.errorString().isEmpty())error += QStringLiteral("：") + file.errorString();
        fileEnabled = false;
        file.close();
    }

    QString archivePath(int number) const {
        return QFileInfo(path).absolutePath() + QStringLiteral("/app.%1.log").arg(number);
    }

    bool rotate() {
        file.close();
        const auto oldest = archivePath(archivedFiles);
        if (QFile::exists(oldest) && !QFile::remove(oldest)) {
            error = QStringLiteral("无法删除最早的日志文件：") + oldest;
            fileEnabled = false;
            return false;
        }
        for (int number = archivedFiles - 1; number >= 1; --number) {
            const auto source = archivePath(number);
            const auto target = archivePath(number + 1);
            if (QFile::exists(source) && !QFile::rename(source, target)) {
                error = QStringLiteral("无法轮换日志文件：") + source;
                fileEnabled = false;
                return false;
            }
        }
        if (QFile::exists(path) && !QFile::rename(path, archivePath(1))) {
            error = QStringLiteral("无法轮换当前日志文件：") + path;
            fileEnabled = false;
            return false;
        }
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            failStorage(QStringLiteral("无法打开新的日志文件"));
            return false;
        }
        return true;
    }

    void appendFile(const LogEntry& entry) {
        if (!fileEnabled)return;
        const auto bytes = formatLogEntry(entry).toUtf8() + '\n';
        if (file.size() + bytes.size() > maximumFileSize && !rotate())return;
        if (file.write(bytes) != bytes.size() || !file.flush())
            failStorage(QStringLiteral("无法写入日志文件"));
    }
};

QString formatLogEntry(const LogEntry& entry) {
    return QStringLiteral("%1 [%2] [%3] %4")
        .arg(entry.time.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
             QString::fromLatin1(levelName(entry.level)), singleLine(entry.category), singleLine(entry.message));
}

AppLog::AppLog() : state_(std::make_unique<State>()) {}
AppLog::~AppLog() = default;

AppLog& AppLog::instance() {
    // The message handler can still run during Qt/static teardown.
    static auto* log = new AppLog;
    return *log;
}

void AppLog::initialize() {
    LogOperationGuard guard;
    QMutexLocker locker(&state_->mutex);
    if (state_->initialized)return;
    state_->initialized = true;
    qRegisterMetaType<LogEntry>();
    const auto folder = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/logs"));
    state_->path = QDir(folder).filePath(QStringLiteral("app.log"));
    state_->file.setFileName(state_->path);
    if (!QDir().mkpath(folder))state_->error = QStringLiteral("无法创建日志目录：") + folder;
    else if (!state_->file.open(QIODevice::WriteOnly | QIODevice::Append))
        state_->failStorage(QStringLiteral("无法打开日志文件"));
    else {
        state_->fileEnabled = true;
        // Keep any records produced before initialization in the persistent log.
        for (const auto& entry : state_->entries)state_->appendFile(entry);
    }
    previousMessageHandler.store(qInstallMessageHandler(qtMessageHandler), std::memory_order_release);
}

void AppLog::write(LogLevel level, const QString& category, const QString& message) {
    LogEntry entry;
    {
        LogOperationGuard guard;
        QMutexLocker locker(&state_->mutex);
        entry.sequence = ++state_->sequence;
        entry.time = QDateTime::currentDateTime();
        entry.level = level;
        entry.category = boundedText(category.isEmpty() ? QStringLiteral("app") : category, 128);
        entry.message = boundedText(message, maximumMessageLength);
        if (state_->entries.size() >= maximumEntries)state_->entries.removeFirst();
        state_->entries.append(entry);
        state_->appendFile(entry);
    }
    emit entryAdded(entry);
}

QList<LogEntry> AppLog::entries() const {
    QMutexLocker locker(&state_->mutex);
    return state_->entries;
}

QString AppLog::filePath() const {
    QMutexLocker locker(&state_->mutex);
    return state_->path;
}

QString AppLog::storageError() const {
    QMutexLocker locker(&state_->mutex);
    return state_->error;
}

void AppLog::clear() {
    {
        LogOperationGuard guard;
        QMutexLocker locker(&state_->mutex);
        state_->entries.clear();
        if (state_->fileEnabled && (!state_->file.resize(0) || !state_->file.seek(0)))
            state_->failStorage(QStringLiteral("无法清空当前日志文件"));
    }
    emit cleared();
}
}
