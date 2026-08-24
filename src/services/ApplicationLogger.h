#pragma once

#include <QFile>
#include <QMutex>
#include <QString>
#include <QtGlobal>

#include <atomic>
#include <cstdint>

namespace rtsa {

enum class LogLevel : std::uint8_t {
    Debug,
    Info,
    Warning,
    Error
};

struct LoggerConfig {
    QString directoryPath;
    LogLevel minimumLevel = LogLevel::Info;
    qint64 maximumFileBytes = 5 * 1024 * 1024;
    int retainedFiles = 3;
    bool mirrorToConsole = true;
};

class ApplicationLogger final {
public:
    ApplicationLogger() = default;
    ~ApplicationLogger();

    ApplicationLogger(const ApplicationLogger&) = delete;
    ApplicationLogger& operator=(const ApplicationLogger&) = delete;

    bool start(const LoggerConfig& config, QString* errorMessage = nullptr);
    void stop();
    bool isRunning() const noexcept;

    void log(LogLevel level,
             const QString& module,
             const QString& event,
             const QString& message);

    QString currentFilePath() const;

    static bool parseLevel(const QString& token,
                           LogLevel& level,
                           QString* errorMessage = nullptr);
    static QString levelToken(LogLevel level);

private:
    static void qtMessageHandler(QtMsgType type,
                                 const QMessageLogContext& context,
                                 const QString& message);
    void writeQtMessage(QtMsgType type,
                        const QMessageLogContext& context,
                        const QString& message);
    void rotateIfNeeded(qint64 incomingBytes);
    bool openCurrentFile(QString* errorMessage);

    static QMutex handlerMutex_;
    static ApplicationLogger* activeLogger_;

    mutable QMutex mutex_;
    LoggerConfig config_;
    QFile file_;
    QtMessageHandler previousHandler_ = nullptr;
    std::atomic<bool> running_ { false };
};

} // namespace rtsa
