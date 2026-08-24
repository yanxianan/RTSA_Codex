#include "ui/MainWindow.h"
#include "services/ApplicationLogger.h"
#include "sources/SimulationScenarioLoader.h"
#include "sources/SpectrumSourceFactory.h"
#include "ui/ApplicationTheme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <utility>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("RTSA"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("RTSA"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("RFSoC real-time spectrum analyzer"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption fullScreenOption(QStringLiteral("fullscreen"),
                                        QStringLiteral("Start in full-screen mode."));
    QCommandLineOption smokeTestOption(QStringLiteral("smoke-test-ms"),
                                       QStringLiteral("Start acquisition and exit after the specified milliseconds."),
                                       QStringLiteral("milliseconds"));
    QCommandLineOption sourceOption(QStringLiteral("source"),
                                    QStringLiteral("Select spectrum source: simulated or dma."),
                                    QStringLiteral("type"),
                                    QStringLiteral("simulated"));
    QCommandLineOption scenarioOption(QStringLiteral("scenario"),
                                      QStringLiteral("Load a versioned JSON simulation scenario."),
                                      QStringLiteral("path"));
    QCommandLineOption logDirectoryOption(QStringLiteral("log-dir"),
                                          QStringLiteral("Directory for rotating application logs."),
                                          QStringLiteral("path"));
    QCommandLineOption logLevelOption(QStringLiteral("log-level"),
                                      QStringLiteral("Minimum log level: debug, info, warning or error."),
                                      QStringLiteral("level"),
                                      QStringLiteral("info"));
    QCommandLineOption logMaximumBytesOption(QStringLiteral("log-max-bytes"),
                                             QStringLiteral("Maximum bytes per log file."),
                                             QStringLiteral("bytes"),
                                             QStringLiteral("5242880"));
    QCommandLineOption logRetainedFilesOption(QStringLiteral("log-retained-files"),
                                              QStringLiteral("Number of rotated log files to retain (1-10)."),
                                              QStringLiteral("count"),
                                              QStringLiteral("3"));
    QCommandLineOption noFileLogOption(QStringLiteral("no-file-log"),
                                       QStringLiteral("Disable file logging for tests or diagnostics."));
    parser.addOption(fullScreenOption);
    parser.addOption(smokeTestOption);
    parser.addOption(sourceOption);
    parser.addOption(scenarioOption);
    parser.addOption(logDirectoryOption);
    parser.addOption(logLevelOption);
    parser.addOption(logMaximumBytesOption);
    parser.addOption(logRetainedFilesOption);
    parser.addOption(noFileLogOption);
    parser.process(application);

    rtsa::ApplicationLogger logger;
    if (!parser.isSet(noFileLogOption)) {
        rtsa::LoggerConfig loggerConfig;
        loggerConfig.directoryPath = parser.isSet(logDirectoryOption)
            ? parser.value(logDirectoryOption)
            : QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                  .filePath(QStringLiteral("logs"));
        QString loggerError;
        if (!rtsa::ApplicationLogger::parseLevel(
                parser.value(logLevelOption), loggerConfig.minimumLevel, &loggerError)) {
            qCritical().noquote() << loggerError;
            return 2;
        }
        bool maximumBytesValid = false;
        loggerConfig.maximumFileBytes = parser.value(logMaximumBytesOption)
            .toLongLong(&maximumBytesValid);
        bool retainedFilesValid = false;
        loggerConfig.retainedFiles = parser.value(logRetainedFilesOption)
            .toInt(&retainedFilesValid);
        if (!maximumBytesValid || loggerConfig.maximumFileBytes < 1024
            || !retainedFilesValid || loggerConfig.retainedFiles < 1
            || loggerConfig.retainedFiles > 10) {
            qCritical() << "Invalid log rotation arguments.";
            return 2;
        }
        if (!logger.start(loggerConfig, &loggerError)) {
            qWarning().noquote() << loggerError;
        }
    }
    logger.log(rtsa::LogLevel::Info,
               QStringLiteral("app"),
               QStringLiteral("startup"),
               QStringLiteral("RTSA %1 starting").arg(QCoreApplication::applicationVersion()));

    rtsa::SpectrumSourceKind sourceKind = rtsa::SpectrumSourceKind::Simulated;
    QString sourceError;
    if (!rtsa::parseSpectrumSourceKind(parser.value(sourceOption), sourceKind, sourceError)) {
        logger.log(rtsa::LogLevel::Error,
                   QStringLiteral("source"),
                   QStringLiteral("selection-failed"),
                   sourceError);
        qCritical().noquote() << sourceError;
        return 3;
    }

    rtsa::SimulationConfig scenarioConfiguration;
    bool hasScenario = false;
    if (parser.isSet(scenarioOption)) {
        if (sourceKind != rtsa::SpectrumSourceKind::Simulated) {
            sourceError = QStringLiteral("--scenario 只能与 --source simulated 一起使用。");
            logger.log(rtsa::LogLevel::Error,
                       QStringLiteral("scenario"),
                       QStringLiteral("source-mismatch"),
                       sourceError);
            qCritical().noquote() << sourceError;
            return 5;
        }
        const rtsa::SimulationScenarioLoadResult scenario =
            rtsa::SimulationScenarioLoader::loadFile(parser.value(scenarioOption));
        if (!scenario.success) {
            logger.log(rtsa::LogLevel::Error,
                       QStringLiteral("scenario"),
                       QStringLiteral("load-failed"),
                       scenario.errorMessage);
            qCritical().noquote() << scenario.errorMessage;
            return 6;
        }
        scenarioConfiguration = scenario.configuration;
        hasScenario = true;
        logger.log(rtsa::LogLevel::Info,
                   QStringLiteral("scenario"),
                   QStringLiteral("loaded"),
                   scenario.scenarioName);
    }
    rtsa::SourceCreationResult sourceResult = rtsa::createSpectrumSource(sourceKind);
    if (!sourceResult.source) {
        logger.log(rtsa::LogLevel::Error,
                   QStringLiteral("source"),
                   QStringLiteral("creation-failed"),
                   sourceResult.errorMessage);
        qCritical().noquote() << sourceResult.errorMessage;
        return 4;
    }
    logger.log(rtsa::LogLevel::Info,
               QStringLiteral("source"),
               QStringLiteral("selected"),
               rtsa::spectrumSourceKindToken(sourceKind));

    rtsa::applyApplicationTheme(application);

    // Automated smoke tests must not modify the developer's persisted settings.
    rtsa::MainWindow window(std::move(sourceResult.source),
                            nullptr,
                            !parser.isSet(smokeTestOption),
                            hasScenario ? &scenarioConfiguration : nullptr);
    if (parser.isSet(fullScreenOption)) {
        window.showFullScreen();
    } else {
        window.show();
    }

    if (parser.isSet(smokeTestOption)) {
        bool valid = false;
        const int durationMs = parser.value(smokeTestOption).toInt(&valid);
        QTimer::singleShot(0, &window, &rtsa::MainWindow::startAcquisition);
        QTimer::singleShot(valid ? std::max(100, durationMs) : 1500,
                           &application,
                           &QCoreApplication::quit);
    }

    const int exitCode = application.exec();
    logger.log(rtsa::LogLevel::Info,
               QStringLiteral("app"),
               QStringLiteral("shutdown"),
               QStringLiteral("event loop exited with code %1").arg(exitCode));
    return exitCode;
}
