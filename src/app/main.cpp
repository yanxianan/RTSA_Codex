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
#include <QIcon>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <utility>

#include <clocale>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QTextCodec>
#endif
#if defined(Q_OS_WIN)
#include <windows.h>
#include <shobjidl.h>
#endif

int main(int argc, char* argv[])
{
#if defined(Q_OS_WIN)
    SetCurrentProcessExplicitAppUserModelID(L"SpectrumPro.RTSA.App");
#endif

    std::setlocale(LC_ALL, "C.UTF-8");
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("SpectrumPro"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("SpectrumPro"));
    application.setApplicationDisplayName(QStringLiteral("SpectrumPro - 大带宽实时频谱分析仪"));

    QIcon appIcon(QStringLiteral(":/icons/app.ico"));
    if (appIcon.isNull()) {
        appIcon = QIcon(QStringLiteral(":/icons/app.png"));
    }
    application.setWindowIcon(appIcon);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("SpectrumPro - 大带宽实时频谱分析仪"));
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
    QCommandLineOption renderModeOption(
        QStringLiteral("render-mode"),
        QStringLiteral("Spectrum render path: direct or offscreen."),
        QStringLiteral("mode"),
        QStringLiteral("offscreen"));
    QCommandLineOption displayFpsOption(
        QStringLiteral("display-fps"),
        QStringLiteral("Display refresh cap in frames per second: 30, 60 or 100."),
        QStringLiteral("fps"),
        QStringLiteral("100"));
    QCommandLineOption renderDiagnosticsOption(
        QStringLiteral("render-diagnostics"),
        QStringLiteral("Log render timing percentiles and overwrite counters."));
    QCommandLineOption renderBenchmarkOption(
        QStringLiteral("render-benchmark"),
        QStringLiteral("Run a synthetic render workload: static, trace, waterfall or mixed."),
        QStringLiteral("mode"),
        QStringLiteral("off"));
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
    parser.addOption(renderModeOption);
    parser.addOption(displayFpsOption);
    parser.addOption(renderDiagnosticsOption);
    parser.addOption(renderBenchmarkOption);
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

    const QString requestedRenderMode =
        parser.value(renderModeOption).trimmed().toLower();
    if (requestedRenderMode != QStringLiteral("direct")
        && requestedRenderMode != QStringLiteral("offscreen")) {
        qCritical().noquote()
            << "Invalid --render-mode; expected direct or offscreen.";
        return 7;
    }
    bool displayFpsValid = false;
    const int requestedDisplayFps = parser.value(displayFpsOption).toInt(&displayFpsValid);
    if (!displayFpsValid
        || (requestedDisplayFps != 30
            && requestedDisplayFps != 60
            && requestedDisplayFps != 100)) {
        qCritical().noquote()
            << "Invalid --display-fps; expected 30, 60 or 100.";
        return 7;
    }
    const QString requestedRenderBenchmark =
        parser.value(renderBenchmarkOption).trimmed().toLower();
    if (requestedRenderBenchmark != QStringLiteral("off")
        && requestedRenderBenchmark != QStringLiteral("static")
        && requestedRenderBenchmark != QStringLiteral("trace")
        && requestedRenderBenchmark != QStringLiteral("waterfall")
        && requestedRenderBenchmark != QStringLiteral("mixed")) {
        qCritical().noquote()
            << "Invalid --render-benchmark; expected off, static, trace, waterfall or mixed.";
        return 7;
    }
    const bool benchmarkEnabled = requestedRenderBenchmark != QStringLiteral("off");

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
                            !parser.isSet(smokeTestOption) && !benchmarkEnabled,
                            hasScenario ? &scenarioConfiguration : nullptr);
    if (!window.setSpectrumRenderMode(requestedRenderMode)) {
        qCritical().noquote() << "Unable to apply the requested render mode.";
        return 7;
    }
    window.setDisplayRefreshRate(requestedDisplayFps);
    if (!window.setRenderBenchmarkMode(requestedRenderBenchmark)) {
        qCritical().noquote() << "Unable to apply the requested render benchmark.";
        return 7;
    }
    window.setRenderDiagnosticsEnabled(
        parser.isSet(renderDiagnosticsOption) || benchmarkEnabled);
    logger.log(rtsa::LogLevel::Info,
               QStringLiteral("render"),
               QStringLiteral("configured"),
               QStringLiteral("mode=%1 benchmark=%2 displayFps=%3 diagnostics=%4")
                   .arg(requestedRenderMode)
                   .arg(requestedRenderBenchmark)
                   .arg(requestedDisplayFps)
                   .arg(parser.isSet(renderDiagnosticsOption) || benchmarkEnabled
                            ? QStringLiteral("on")
                            : QStringLiteral("off")));
    if (parser.isSet(fullScreenOption)) {
        window.showFullScreen();
    } else {
        window.show();
    }

    if (parser.isSet(smokeTestOption)) {
        bool valid = false;
        const int durationMs = parser.value(smokeTestOption).toInt(&valid);
        if (!benchmarkEnabled) {
            QTimer::singleShot(0, &window, &rtsa::MainWindow::startAcquisition);
        }
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
