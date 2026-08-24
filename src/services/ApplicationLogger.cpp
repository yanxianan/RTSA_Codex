#include "services/ApplicationLogger.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QTextStream>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace rtsa {
namespace {

QString sanitizedField(QString value)
{
    value.replace(QLatin1Char('|'), QLatin1Char('/'));
    value.replace(QLatin1Char('\r'), QStringLiteral("\\r"));
    value.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return value;
}

LogLevel levelForQtMessage(const QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return LogLevel::Debug;
    case QtInfoMsg:
        return LogLevel::Info;
    case QtWarningMsg:
        return LogLevel::Warning;
    case QtCriticalMsg:
    case QtFatalMsg:
    default:
        return LogLevel::Error;
    }
}

int levelRank(const LogLevel level) noexcept
{
    return static_cast<int>(level);
}

QByteArray boundedRecord(const QString& line, const qint64 maximumBytes)
{
    QByteArray bytes = line.toUtf8();
    if (bytes.size() <= maximumBytes) {
        return bytes;
    }

    const QString marker = QStringLiteral("[truncated]\n");
    int low = 0;
    int high = line.size();
    while (low < high) {
        const int middle = low + (high - low + 1) / 2;
        if ((line.left(middle) + marker).toUtf8().size() <= maximumBytes) {
            low = middle;
        } else {
            high = middle - 1;
        }
    }
    if (low > 0 && line.at(low - 1).isHighSurrogate()) {
        --low;
    }
    bytes = (line.left(low) + marker).toUtf8();
    if (bytes.size() > maximumBytes) {
        bytes.truncate(static_cast<int>(maximumBytes));
    }
    return bytes;
}

} // namespace

QMutex ApplicationLogger::handlerMutex_;
ApplicationLogger* ApplicationLogger::activeLogger_ = nullptr;

ApplicationLogger::~ApplicationLogger()
{
    stop();
}

bool ApplicationLogger::start(const LoggerConfig& config, QString* errorMessage)
{
    stop();
    if (config.directoryPath.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("日志目录不能为空。");
        }
        return false;
    }
    config_ = config;
    config_.maximumFileBytes = std::max<qint64>(128, config_.maximumFileBytes);
    config_.retainedFiles = std::clamp(config_.retainedFiles, 1, 10);
    QDir directory;
    if (!directory.mkpath(config_.directoryPath)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法创建日志目录：%1").arg(config_.directoryPath);
        }
        return false;
    }
    if (!openCurrentFile(errorMessage)) {
        return false;
    }

    running_ = true;
    {
        QMutexLocker handlerLock(&handlerMutex_);
        if (activeLogger_) {
            running_ = false;
            file_.close();
            if (errorMessage) {
                *errorMessage = QStringLiteral("已有日志实例正在运行。");
            }
            return false;
        }
        previousHandler_ = qInstallMessageHandler(&ApplicationLogger::qtMessageHandler);
        activeLogger_ = this;
    }
    return true;
}

void ApplicationLogger::stop()
{
    {
        // Holding the registry mutex waits for any handler that already leased
        // this instance. New handler calls see no active logger after removal.
        QMutexLocker handlerLock(&handlerMutex_);
        if (activeLogger_ == this) {
            qInstallMessageHandler(previousHandler_);
            activeLogger_ = nullptr;
            previousHandler_ = nullptr;
        }
    }

    QMutexLocker lock(&mutex_);
    if (file_.isOpen()) {
        file_.flush();
        file_.close();
    }
    running_ = false;
}

bool ApplicationLogger::isRunning() const noexcept
{
    return running_.load(std::memory_order_acquire);
}

void ApplicationLogger::log(const LogLevel level,
                            const QString& module,
                            const QString& event,
                            const QString& message)
{
    QMutexLocker lock(&mutex_);
    if (!running_.load(std::memory_order_acquire)
        || levelRank(level) < levelRank(config_.minimumLevel)) {
        return;
    }

    const QString line = QStringLiteral("%1|%2|%3|%4|%5\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
             levelToken(level),
             sanitizedField(module),
             sanitizedField(event),
             sanitizedField(message));
    const QByteArray bytes = boundedRecord(line, config_.maximumFileBytes);
    rotateIfNeeded(bytes.size());
    if (file_.isOpen()) {
        static_cast<void>(file_.write(bytes));
        file_.flush();
    }
}

QString ApplicationLogger::currentFilePath() const
{
    QMutexLocker lock(&mutex_);
    return file_.fileName();
}

bool ApplicationLogger::parseLevel(const QString& token,
                                   LogLevel& level,
                                   QString* errorMessage)
{
    const QString normalized = token.trimmed().toLower();
    if (normalized == QStringLiteral("debug")) {
        level = LogLevel::Debug;
    } else if (normalized == QStringLiteral("info")) {
        level = LogLevel::Info;
    } else if (normalized == QStringLiteral("warning")
               || normalized == QStringLiteral("warn")) {
        level = LogLevel::Warning;
    } else if (normalized == QStringLiteral("error")) {
        level = LogLevel::Error;
    } else {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "未知日志级别“%1”；可选值为 debug、info、warning 或 error。")
                .arg(token);
        }
        return false;
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

QString ApplicationLogger::levelToken(const LogLevel level)
{
    switch (level) {
    case LogLevel::Debug:
        return QStringLiteral("DEBUG");
    case LogLevel::Warning:
        return QStringLiteral("WARNING");
    case LogLevel::Error:
        return QStringLiteral("ERROR");
    case LogLevel::Info:
    default:
        return QStringLiteral("INFO");
    }
}

void ApplicationLogger::qtMessageHandler(const QtMsgType type,
                                         const QMessageLogContext& context,
                                         const QString& message)
{
    bool mirror = false;
    QtMessageHandler previous = nullptr;
    {
        QMutexLocker handlerLock(&handlerMutex_);
        ApplicationLogger* logger = activeLogger_;
        if (logger) {
            logger->writeQtMessage(type, context, message);
            mirror = logger->config_.mirrorToConsole;
            previous = logger->previousHandler_;
        }
    }

    if (mirror && previous) {
        previous(type, context, message);
    } else if (mirror) {
        const QByteArray utf8 = message.toLocal8Bit();
        std::fprintf(stderr, "%s\n", utf8.constData());
        std::fflush(stderr);
    }
    if (type == QtFatalMsg) {
        std::abort();
    }
}

void ApplicationLogger::writeQtMessage(const QtMsgType type,
                                       const QMessageLogContext& context,
                                       const QString& message)
{
    const QString module = context.category && *context.category
        ? QString::fromUtf8(context.category)
        : QStringLiteral("application");
    log(levelForQtMessage(type), module, QStringLiteral("qt-message"), message);
}

void ApplicationLogger::rotateIfNeeded(const qint64 incomingBytes)
{
    if (!file_.isOpen()
        || file_.size() == 0
        || file_.size() + incomingBytes <= config_.maximumFileBytes) {
        return;
    }

    const QString currentPath = file_.fileName();
    file_.flush();
    file_.close();
    const QString oldestPath = QStringLiteral("%1.%2")
        .arg(currentPath).arg(config_.retainedFiles);
    QFile::remove(oldestPath);
    for (int index = config_.retainedFiles - 1; index >= 1; --index) {
        const QString from = QStringLiteral("%1.%2").arg(currentPath).arg(index);
        const QString to = QStringLiteral("%1.%2").arg(currentPath).arg(index + 1);
        if (QFileInfo::exists(from)) {
            QFile::remove(to);
            QFile::rename(from, to);
        }
    }
    const QString firstBackup = QStringLiteral("%1.1").arg(currentPath);
    QFile::remove(firstBackup);
    QFile::rename(currentPath, firstBackup);
    file_.setFileName(currentPath);
    static_cast<void>(file_.open(QIODevice::WriteOnly | QIODevice::Append));
}

bool ApplicationLogger::openCurrentFile(QString* errorMessage)
{
    const QString path = QDir(config_.directoryPath).filePath(QStringLiteral("rtsa.log"));
    const QDir logDirectory(config_.directoryPath);
    const QFileInfoList backups = logDirectory.entryInfoList(
        QStringList { QStringLiteral("rtsa.log.*") }, QDir::Files);
    for (const QFileInfo& backupInfo : backups) {
        bool indexValid = false;
        const int index = backupInfo.fileName().mid(QStringLiteral("rtsa.log.").size())
                              .toInt(&indexValid);
        if (indexValid
            && (index < 1 || index > config_.retainedFiles
                || backupInfo.size() > config_.maximumFileBytes)
            && !QFile::remove(backupInfo.absoluteFilePath())) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("无法清理超限日志文件：%1")
                                    .arg(backupInfo.absoluteFilePath());
            }
            return false;
        }
    }
    if (QFileInfo(path).size() > config_.maximumFileBytes
        && !QFile::remove(path)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法清理超限日志文件：%1").arg(path);
        }
        return false;
    }
    file_.setFileName(path);
    if (!file_.open(QIODevice::WriteOnly | QIODevice::Append)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("无法打开日志文件 %1：%2")
                .arg(path, file_.errorString());
        }
        return false;
    }
    return true;
}

} // namespace rtsa
