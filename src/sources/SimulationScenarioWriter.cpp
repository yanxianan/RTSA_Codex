#include "sources/SimulationScenarioWriter.h"

#include "sources/SimulationScenarioLoader.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace rtsa {
namespace {

QString directionToken(const SweepDirection direction)
{
    switch (direction) {
    case SweepDirection::Down:
        return QStringLiteral("down");
    case SweepDirection::PingPong:
        return QStringLiteral("ping-pong");
    case SweepDirection::Up:
    default:
        return QStringLiteral("up");
    }
}

QJsonObject encodeScenario(const QString& scenarioName, const SimulationConfig& config)
{
    QJsonArray tones;
    for (const ToneConfig& tone : config.tones) {
        tones.append(QJsonObject {
            { QStringLiteral("enabled"), tone.enabled },
            { QStringLiteral("frequencyHz"), tone.frequencyHz },
            { QStringLiteral("amplitudeDbfs"), tone.amplitudeDbfs },
            { QStringLiteral("widthBins"), tone.widthBins }
        });
    }

    const QJsonObject sweep {
        { QStringLiteral("enabled"), config.sweepEnabled },
        { QStringLiteral("startFrequencyHz"), config.sweepStartHz },
        { QStringLiteral("stopFrequencyHz"), config.sweepStopHz },
        { QStringLiteral("periodSeconds"), config.sweepPeriodSeconds },
        { QStringLiteral("amplitudeDbfs"), config.sweepAmplitudeDbfs },
        { QStringLiteral("direction"), directionToken(config.sweepDirection) }
    };
    const QJsonObject transient {
        { QStringLiteral("probabilityPerFrame"), config.transientProbability },
        { QStringLiteral("amplitudeDbfs"), config.transientAmplitudeDbfs },
        { QStringLiteral("durationSeconds"), config.transientDurationSeconds }
    };
    const QJsonObject faultInjection {
        { QStringLiteral("pauseEveryFrames"),
          static_cast<double>(config.faults.pauseEveryFrames) },
        { QStringLiteral("pauseDurationSeconds"), config.faults.pauseDurationSeconds },
        { QStringLiteral("sequenceJumpEveryFrames"),
          static_cast<double>(config.faults.sequenceJumpEveryFrames) },
        { QStringLiteral("sequenceSkipCount"),
          static_cast<double>(config.faults.sequenceSkipCount) },
        { QStringLiteral("invalidFrameEveryFrames"),
          static_cast<double>(config.faults.invalidFrameEveryFrames) },
        { QStringLiteral("burstEveryFrames"),
          static_cast<double>(config.faults.burstEveryFrames) },
        { QStringLiteral("burstFrameCount"),
          static_cast<double>(config.faults.burstFrameCount) }
    };
    const QJsonObject simulation {
        { QStringLiteral("binCount"), static_cast<double>(config.binCount) },
        { QStringLiteral("frameRate"), config.frameRate },
        { QStringLiteral("unthrottled"), config.unthrottled },
        { QStringLiteral("centerFrequencyHz"), config.centerFrequencyHz },
        { QStringLiteral("spanHz"), config.spanHz },
        { QStringLiteral("noiseFloorDbfs"), config.noiseFloorDbfs },
        { QStringLiteral("noiseDeviationDb"), config.noiseDeviationDb },
        { QStringLiteral("randomSeed"), static_cast<double>(config.randomSeed) },
        { QStringLiteral("tones"), tones },
        { QStringLiteral("sweep"), sweep },
        { QStringLiteral("transient"), transient },
        { QStringLiteral("faultInjection"), faultInjection }
    };
    return QJsonObject {
        { QStringLiteral("schemaVersion"), 2 },
        { QStringLiteral("name"), scenarioName.trimmed() },
        { QStringLiteral("simulation"), simulation }
    };
}

} // namespace

SimulationScenarioSaveResult SimulationScenarioWriter::saveFile(
    const QString& filePath,
    const QString& scenarioName,
    const SimulationConfig& config)
{
    const QByteArray data = QJsonDocument(encodeScenario(scenarioName, config))
                                .toJson(QJsonDocument::Indented);
    const SimulationScenarioLoadResult validation =
        SimulationScenarioLoader::loadData(data);
    if (!validation.success) {
        return SimulationScenarioSaveResult {
            false,
            QStringLiteral("拒绝保存无效模拟场景：%1").arg(validation.errorMessage)
        };
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return SimulationScenarioSaveResult {
            false,
            QStringLiteral("无法创建模拟场景 %1：%2").arg(filePath, file.errorString())
        };
    }
    if (file.write(data) != data.size()) {
        file.cancelWriting();
        return SimulationScenarioSaveResult {
            false,
            QStringLiteral("写入模拟场景失败：%1").arg(file.errorString())
        };
    }
    if (!file.commit()) {
        return SimulationScenarioSaveResult {
            false,
            QStringLiteral("提交模拟场景失败：%1").arg(file.errorString())
        };
    }
    return SimulationScenarioSaveResult { true, {} };
}

} // namespace rtsa
