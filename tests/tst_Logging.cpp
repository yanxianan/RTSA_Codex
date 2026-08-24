#include "services/ApplicationLogger.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

#include <atomic>
#include <thread>
#include <vector>

namespace rtsa {
namespace {

void discardQtMessage(QtMsgType, const QMessageLogContext&, const QString&)
{
}

} // namespace

class LoggingTests final : public QObject {
    Q_OBJECT

private slots:
    void structuredOutputFiltersAndSanitizes();
    void rotatesAndBoundsRetainedFiles();
    void concurrentWritersProduceCompleteLines();
    void qtMessagesAreCaptured();
    void levelTokensAreValidated();
    void oversizedRecordIsTruncatedToConfiguredLimit();
    void qtHandlerCanStopWhileMessagesAreInFlight();
};

void LoggingTests::structuredOutputFiltersAndSanitizes()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ApplicationLogger logger;
    LoggerConfig config;
    config.directoryPath = directory.path();
    config.minimumLevel = LogLevel::Info;
    config.mirrorToConsole = false;
    QString error;
    QVERIFY2(logger.start(config, &error), qPrintable(error));
    const QString path = logger.currentFilePath();
    logger.log(LogLevel::Debug,
               QStringLiteral("source"),
               QStringLiteral("hidden"),
               QStringLiteral("debug message"));
    logger.log(LogLevel::Info,
               QStringLiteral("source|simulated"),
               QStringLiteral("frame"),
               QStringLiteral("line\nbreak"));
    logger.stop();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    QVERIFY(!content.contains("debug message"));
    QVERIFY(content.contains("|INFO|source/simulated|frame|line\\nbreak\n"));
}

void LoggingTests::rotatesAndBoundsRetainedFiles()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ApplicationLogger logger;
    LoggerConfig config;
    config.directoryPath = directory.path();
    config.maximumFileBytes = 256;
    config.retainedFiles = 2;
    config.mirrorToConsole = false;
    QVERIFY(logger.start(config));
    const QString path = logger.currentFilePath();
    for (int index = 0; index < 30; ++index) {
        logger.log(LogLevel::Info,
                   QStringLiteral("rotation"),
                   QStringLiteral("write"),
                   QString(80, QLatin1Char('x')) + QString::number(index));
    }
    logger.stop();

    QVERIFY(QFileInfo::exists(path));
    QVERIFY(QFileInfo::exists(path + QStringLiteral(".1")));
    QVERIFY(QFileInfo::exists(path + QStringLiteral(".2")));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".3")));
    QVERIFY(QFileInfo(path).size() <= config.maximumFileBytes);
    QVERIFY(QFileInfo(path + QStringLiteral(".1")).size() <= config.maximumFileBytes);
    QVERIFY(QFileInfo(path + QStringLiteral(".2")).size() <= config.maximumFileBytes);
}

void LoggingTests::concurrentWritersProduceCompleteLines()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ApplicationLogger logger;
    LoggerConfig config;
    config.directoryPath = directory.path();
    config.minimumLevel = LogLevel::Debug;
    config.maximumFileBytes = 1024 * 1024;
    config.mirrorToConsole = false;
    QVERIFY(logger.start(config));
    const QString path = logger.currentFilePath();

    std::vector<std::thread> writers;
    for (int threadIndex = 0; threadIndex < 4; ++threadIndex) {
        writers.emplace_back([&logger, threadIndex] {
            for (int line = 0; line < 50; ++line) {
                logger.log(LogLevel::Debug,
                           QStringLiteral("thread-%1").arg(threadIndex),
                           QStringLiteral("sample"),
                           QString::number(line));
            }
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }
    logger.stop();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(static_cast<int>(file.readAll().count('\n')), 200);
}

void LoggingTests::qtMessagesAreCaptured()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ApplicationLogger logger;
    LoggerConfig config;
    config.directoryPath = directory.path();
    config.mirrorToConsole = false;
    QVERIFY(logger.start(config));
    const QString path = logger.currentFilePath();
    qWarning().noquote() << "logger-bridge-marker";
    logger.stop();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    QVERIFY(content.contains("|WARNING|default|qt-message|logger-bridge-marker\n"));
}

void LoggingTests::levelTokensAreValidated()
{
    LogLevel level = LogLevel::Error;
    QString error;
    QVERIFY(ApplicationLogger::parseLevel(QStringLiteral(" warn "), level, &error));
    QCOMPARE(static_cast<int>(level), static_cast<int>(LogLevel::Warning));
    QCOMPARE(ApplicationLogger::levelToken(level), QStringLiteral("WARNING"));
    QVERIFY(error.isEmpty());
    QVERIFY(!ApplicationLogger::parseLevel(QStringLiteral("verbose"), level, &error));
    QVERIFY(!error.isEmpty());
}

void LoggingTests::oversizedRecordIsTruncatedToConfiguredLimit()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ApplicationLogger logger;
    LoggerConfig config;
    config.directoryPath = directory.path();
    config.maximumFileBytes = 256;
    config.mirrorToConsole = false;
    for (const QString& name : { QStringLiteral("rtsa.log"),
                                 QStringLiteral("rtsa.log.1"),
                                 QStringLiteral("rtsa.log.7") }) {
        QFile oversized(directory.filePath(name));
        QVERIFY(oversized.open(QIODevice::WriteOnly));
        QCOMPARE(oversized.write(QByteArray(4096, 'x')), qint64(4096));
    }
    QVERIFY(logger.start(config));
    const QString path = logger.currentFilePath();
    logger.log(LogLevel::Error,
               QStringLiteral("oversize"),
               QStringLiteral("single-record"),
               QString(4096, QChar(0x6D4B)));
    logger.stop();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    QVERIFY(content.size() <= config.maximumFileBytes);
    QVERIFY(content.endsWith("[truncated]\n"));
    QVERIFY(QString::fromUtf8(content).contains(QStringLiteral("[truncated]")));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".1"))
            || QFileInfo(path + QStringLiteral(".1")).size()
                <= config.maximumFileBytes);
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".7")));
}

void LoggingTests::qtHandlerCanStopWhileMessagesAreInFlight()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QtMessageHandler originalHandler = qInstallMessageHandler(&discardQtMessage);
    std::atomic<bool> keepWriting { true };
    std::thread writer([&keepWriting] {
        while (keepWriting.load(std::memory_order_acquire)) {
            qWarning() << "teardown-race";
        }
    });

    for (int iteration = 0; iteration < 50; ++iteration) {
        ApplicationLogger logger;
        LoggerConfig config;
        config.directoryPath = directory.path();
        config.maximumFileBytes = 1024 * 1024;
        config.mirrorToConsole = false;
        QVERIFY(logger.start(config));
        QTest::qWait(1);
        logger.stop();
    }
    keepWriting.store(false, std::memory_order_release);
    writer.join();
    qInstallMessageHandler(originalHandler);
}

} // namespace rtsa

QTEST_APPLESS_MAIN(rtsa::LoggingTests)

#include "tst_Logging.moc"
