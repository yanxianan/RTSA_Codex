#include "sources/SimulationScenarioLoader.h"
#include "sources/SimulationScenarioWriter.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

namespace rtsa {
namespace {

QJsonDocument deliveredScenario()
{
    QFile file(QStringLiteral(RTSA_TEST_SOURCE_DIR "/config/default-simulation.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll());
}

bool writeScenario(const QString& path, const QJsonDocument& document)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(document.toJson(QJsonDocument::Compact)) >= 0;
}

} // namespace

class SimulationScenarioTests final : public QObject {
    Q_OBJECT

private slots:
    void deliveredScenariosLoad();
    void invalidVersionTypeAndToneCountAreRejected();
    void versionOneLoadsWithFaultsDisabled();
    void invalidFaultInjectionIsRejected();
    void invalidSweepRangeIsRejected();
    void scenarioSaveRoundTripsAtomically();
    void invalidConfigurationIsNotSaved();
    void exclusiveStopBoundaryIsRejected();
    void oversizedFileIsRejectedBeforeParsing();
};

void SimulationScenarioTests::deliveredScenariosLoad()
{
    const SimulationScenarioLoadResult defaults = SimulationScenarioLoader::loadFile(
        QStringLiteral(RTSA_TEST_SOURCE_DIR "/config/default-simulation.json"));
    QVERIFY2(defaults.success, qPrintable(defaults.errorMessage));
    QCOMPARE(defaults.configuration.binCount, std::size_t(16384));
    QCOMPARE(defaults.configuration.tones.size(), std::size_t(2));
    QCOMPARE(defaults.configuration.tones[0].widthBins, 1.5F);
    QVERIFY(!defaults.configuration.unthrottled);

    const SimulationScenarioLoadResult stress = SimulationScenarioLoader::loadFile(
        QStringLiteral(RTSA_TEST_SOURCE_DIR "/config/unthrottled-stress.json"));
    QVERIFY2(stress.success, qPrintable(stress.errorMessage));
    QVERIFY(stress.configuration.unthrottled);

    const SimulationScenarioLoadResult faults = SimulationScenarioLoader::loadFile(
        QStringLiteral(RTSA_TEST_SOURCE_DIR "/config/fault-injection.json"));
    QVERIFY2(faults.success, qPrintable(faults.errorMessage));
    QCOMPARE(faults.configuration.faults.pauseEveryFrames, std::uint32_t(25));
    QCOMPARE(faults.configuration.faults.sequenceSkipCount, std::uint32_t(3));
    QCOMPARE(faults.configuration.faults.invalidFrameEveryFrames, std::uint32_t(17));
    QCOMPARE(faults.configuration.faults.burstFrameCount, std::uint32_t(5));
}

void SimulationScenarioTests::invalidVersionTypeAndToneCountAreRejected()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QJsonDocument document = deliveredScenario();
    QVERIFY(!document.isNull());

    QJsonObject root = document.object();
    root.insert(QStringLiteral("schemaVersion"), 3);
    const QString versionPath = directory.filePath(QStringLiteral("version.json"));
    QVERIFY(writeScenario(versionPath, QJsonDocument(root)));
    QVERIFY(!SimulationScenarioLoader::loadFile(versionPath).success);

    root = document.object();
    QJsonObject simulation = root.value(QStringLiteral("simulation")).toObject();
    simulation.insert(QStringLiteral("unthrottled"), QStringLiteral("true"));
    root.insert(QStringLiteral("simulation"), simulation);
    const QString typePath = directory.filePath(QStringLiteral("type.json"));
    QVERIFY(writeScenario(typePath, QJsonDocument(root)));
    QVERIFY(!SimulationScenarioLoader::loadFile(typePath).success);

    root = document.object();
    simulation = root.value(QStringLiteral("simulation")).toObject();
    QJsonArray tones = simulation.value(QStringLiteral("tones")).toArray();
    tones.append(tones.at(0));
    simulation.insert(QStringLiteral("tones"), tones);
    root.insert(QStringLiteral("simulation"), simulation);
    const QString tonesPath = directory.filePath(QStringLiteral("tones.json"));
    QVERIFY(writeScenario(tonesPath, QJsonDocument(root)));
    QVERIFY(!SimulationScenarioLoader::loadFile(tonesPath).success);
}

void SimulationScenarioTests::versionOneLoadsWithFaultsDisabled()
{
    QJsonDocument document = deliveredScenario();
    QVERIFY(!document.isNull());
    QJsonObject root = document.object();
    root.insert(QStringLiteral("schemaVersion"), 1);
    QJsonObject simulation = root.value(QStringLiteral("simulation")).toObject();
    simulation.remove(QStringLiteral("faultInjection"));
    root.insert(QStringLiteral("simulation"), simulation);

    const SimulationScenarioLoadResult loaded = SimulationScenarioLoader::loadData(
        QJsonDocument(root).toJson(QJsonDocument::Compact));
    QVERIFY2(loaded.success, qPrintable(loaded.errorMessage));
    QCOMPARE(loaded.configuration.faults.pauseEveryFrames, std::uint32_t(0));
    QCOMPARE(loaded.configuration.faults.sequenceJumpEveryFrames, std::uint32_t(0));
    QCOMPARE(loaded.configuration.faults.invalidFrameEveryFrames, std::uint32_t(0));
    QCOMPARE(loaded.configuration.faults.burstEveryFrames, std::uint32_t(0));
}

void SimulationScenarioTests::invalidFaultInjectionIsRejected()
{
    QJsonDocument document = deliveredScenario();
    QVERIFY(!document.isNull());
    QJsonObject root = document.object();
    QJsonObject simulation = root.value(QStringLiteral("simulation")).toObject();
    QJsonObject faults = simulation.value(QStringLiteral("faultInjection")).toObject();
    faults.insert(QStringLiteral("pauseDurationSeconds"), 0.0);
    simulation.insert(QStringLiteral("faultInjection"), faults);
    root.insert(QStringLiteral("simulation"), simulation);

    const SimulationScenarioLoadResult loaded = SimulationScenarioLoader::loadData(
        QJsonDocument(root).toJson(QJsonDocument::Compact));
    QVERIFY(!loaded.success);
    QVERIFY(loaded.errorMessage.contains(QStringLiteral("pauseDurationSeconds")));
}

void SimulationScenarioTests::invalidSweepRangeIsRejected()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QJsonDocument document = deliveredScenario();
    QVERIFY(!document.isNull());
    QJsonObject root = document.object();
    QJsonObject simulation = root.value(QStringLiteral("simulation")).toObject();
    QJsonObject sweep = simulation.value(QStringLiteral("sweep")).toObject();
    sweep.insert(QStringLiteral("startFrequencyHz"), 1050000000.0);
    sweep.insert(QStringLiteral("stopFrequencyHz"), 950000000.0);
    simulation.insert(QStringLiteral("sweep"), sweep);
    root.insert(QStringLiteral("simulation"), simulation);
    const QString path = directory.filePath(QStringLiteral("range.json"));
    QVERIFY(writeScenario(path, QJsonDocument(root)));

    const SimulationScenarioLoadResult result = SimulationScenarioLoader::loadFile(path);
    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains(QStringLiteral("起始频率")));
}

void SimulationScenarioTests::scenarioSaveRoundTripsAtomically()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SimulationConfig config;
    config.binCount = 32768;
    config.frameRate = 750.0;
    config.unthrottled = true;
    config.centerFrequencyHz = 2.4e9;
    config.spanHz = 100.0e6;
    config.randomSeed = 123456U;
    config.tones = {
        ToneConfig { true, 2.39e9, -25.0F, 4.5F },
        ToneConfig { false, 2.42e9, -15.0F, 6.5F }
    };
    config.sweepStartHz = 2.36e9;
    config.sweepStopHz = 2.44e9;
    config.sweepDirection = SweepDirection::PingPong;
    config.sweepPeriodSeconds = 8.0;
    config.transientDurationSeconds = 0.75;
    config.faults.pauseEveryFrames = 100;
    config.faults.pauseDurationSeconds = 0.25;
    config.faults.sequenceJumpEveryFrames = 33;
    config.faults.sequenceSkipCount = 7;
    config.faults.invalidFrameEveryFrames = 41;
    config.faults.burstEveryFrames = 50;
    config.faults.burstFrameCount = 9;

    const QString path = directory.filePath(QStringLiteral("round-trip.json"));
    const SimulationScenarioSaveResult saved = SimulationScenarioWriter::saveFile(
        path, QStringLiteral("Round trip"), config);
    QVERIFY2(saved.success, qPrintable(saved.errorMessage));
    const SimulationScenarioLoadResult loaded = SimulationScenarioLoader::loadFile(path);
    QVERIFY2(loaded.success, qPrintable(loaded.errorMessage));
    QCOMPARE(loaded.scenarioName, QStringLiteral("Round trip"));
    QCOMPARE(loaded.configuration.binCount, config.binCount);
    QCOMPARE(loaded.configuration.randomSeed, config.randomSeed);
    QCOMPARE(loaded.configuration.unthrottled, config.unthrottled);
    QCOMPARE(loaded.configuration.tones.size(), config.tones.size());
    QCOMPARE(loaded.configuration.tones[0].widthBins, config.tones[0].widthBins);
    QCOMPARE(static_cast<int>(loaded.configuration.sweepDirection),
             static_cast<int>(config.sweepDirection));
    QCOMPARE(loaded.configuration.transientDurationSeconds,
             config.transientDurationSeconds);
    QCOMPARE(loaded.configuration.faults.pauseEveryFrames,
             config.faults.pauseEveryFrames);
    QCOMPARE(loaded.configuration.faults.pauseDurationSeconds,
             config.faults.pauseDurationSeconds);
    QCOMPARE(loaded.configuration.faults.sequenceJumpEveryFrames,
             config.faults.sequenceJumpEveryFrames);
    QCOMPARE(loaded.configuration.faults.sequenceSkipCount,
             config.faults.sequenceSkipCount);
    QCOMPARE(loaded.configuration.faults.invalidFrameEveryFrames,
             config.faults.invalidFrameEveryFrames);
    QCOMPARE(loaded.configuration.faults.burstEveryFrames,
             config.faults.burstEveryFrames);
    QCOMPARE(loaded.configuration.faults.burstFrameCount,
             config.faults.burstFrameCount);
}

void SimulationScenarioTests::invalidConfigurationIsNotSaved()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    SimulationConfig config;
    config.sweepStopHz = config.centerFrequencyHz + config.spanHz * 0.5;
    const QString path = directory.filePath(QStringLiteral("invalid.json"));
    const SimulationScenarioSaveResult result = SimulationScenarioWriter::saveFile(
        path, QStringLiteral("Invalid"), config);
    QVERIFY(!result.success);
    QVERIFY(!QFileInfo::exists(path));
}

void SimulationScenarioTests::exclusiveStopBoundaryIsRejected()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QJsonDocument document = deliveredScenario();
    QVERIFY(!document.isNull());
    QJsonObject root = document.object();
    QJsonObject simulation = root.value(QStringLiteral("simulation")).toObject();
    QJsonArray tones = simulation.value(QStringLiteral("tones")).toArray();
    QJsonObject tone = tones.at(0).toObject();
    tone.insert(QStringLiteral("frequencyHz"), 1100000000.0);
    tones.replace(0, tone);
    simulation.insert(QStringLiteral("tones"), tones);
    root.insert(QStringLiteral("simulation"), simulation);
    const QString tonePath = directory.filePath(QStringLiteral("tone-stop.json"));
    QVERIFY(writeScenario(tonePath, QJsonDocument(root)));
    QVERIFY(!SimulationScenarioLoader::loadFile(tonePath).success);

    root = document.object();
    simulation = root.value(QStringLiteral("simulation")).toObject();
    QJsonObject sweep = simulation.value(QStringLiteral("sweep")).toObject();
    sweep.insert(QStringLiteral("stopFrequencyHz"), 1100000000.0);
    simulation.insert(QStringLiteral("sweep"), sweep);
    root.insert(QStringLiteral("simulation"), simulation);
    const QString sweepPath = directory.filePath(QStringLiteral("sweep-stop.json"));
    QVERIFY(writeScenario(sweepPath, QJsonDocument(root)));
    QVERIFY(!SimulationScenarioLoader::loadFile(sweepPath).success);
}

void SimulationScenarioTests::oversizedFileIsRejectedBeforeParsing()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile file(directory.filePath(QStringLiteral("oversized.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QByteArray(1024 * 1024 + 1, ' ')), qint64(1024 * 1024 + 1));
    file.close();

    const SimulationScenarioLoadResult result = SimulationScenarioLoader::loadFile(
        file.fileName());
    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains(QStringLiteral("1 MiB")));
}

} // namespace rtsa

QTEST_APPLESS_MAIN(rtsa::SimulationScenarioTests)

#include "tst_SimulationScenario.moc"
