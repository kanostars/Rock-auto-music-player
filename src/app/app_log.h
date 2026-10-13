#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>
#include <memory>

namespace rock {
enum class LogLevel { Debug, Info, Warning, Error };

struct LogEntry {
    quint64 sequence{};
    QDateTime time;
    LogLevel level{LogLevel::Info};
    QString category;
    QString message;
};

QString formatLogEntry(const LogEntry& entry);

class AppLog final : public QObject {
    Q_OBJECT
public:
    static AppLog& instance();
    // Initialize once from the application thread before launching workers.
    void initialize();
    void write(LogLevel level, const QString& category, const QString& message);
    QList<LogEntry> entries() const;
    QString filePath() const;
    QString storageError() const;
    // Clear the current session and app.log; keep rotated history files.
    void clear();

signals:
    void entryAdded(const rock::LogEntry& entry);
    void cleared();

private:
    AppLog();
    ~AppLog() override;
    struct State;
    std::unique_ptr<State> state_;
};
}

Q_DECLARE_METATYPE(rock::LogEntry)
