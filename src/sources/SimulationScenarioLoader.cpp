#include "sources/SimulationScenarioLoader.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace rtsa {
namespace {

constexpr qint64 kMaximumScenarioBytes = 1024 * 1024;

bool fail(QString& error, const QString& path, const QString& reason)
{
    error = QStringLiteral("场景字段 %1 %2").arg(path, reason);
    return false;
}

bool readNumber(const QJsonObject& object,
                const QString& key,
                const QString& path,
                const double minimum,
                const double maximum,
                double& value,
                QString& error)
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isDouble()) {
        return fail(error, path, QStringLiteral("必须是数值。"));
    }
    value = jsonValue.toDouble();
    if (!std::isfinite(value) || value < minimum || value > maximum) {
        return fail(error,
                    path,
                    QStringLiteral("超出范围 [%1, %2]。")
                        .arg(minimum, 0, 'g', 12)
                        .arg(maximum, 0, 'g', 12));
    }
    return true;
}

bool readInteger(const QJsonObject& object,
                 const QString& key,
                 const QString& path,
                 const double minimum,
                 const double maximum,
                 std::uint64_t& value,
                 QString& error)
{
    double number = 0.0;
    if (!readNumber(object, key, path, minimum, maximum, number, error)) {
        return false;
    }
    if (std::floor(number) != number) {
        return fail(error, path, QStringLiteral("必须是整数。"));
    }
    value = static_cast<std::uint64_t>(number);
    return true;
}

bool readBoolean(const QJsonObject& object,
                 const QString& key,
                 const QString& path,
                 bool& value,
                 QString& error)
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isBool()) {
        return fail(error, path, QStringLiteral("必须是布尔值。"));
    }
    value = jsonValue.toBool();
    return true;
}

bool readObject(const QJsonObject& object,
                const QString& key,
                const QString& path,
                QJsonObject& value,
                QString& error)
{
    const QJsonValue jsonValue = object.value(key);
    if (!jsonValue.isObject()) {
        return fail(error, path, QStringLiteral("必须是对象。"));
    }
    value = jsonValue.toObject();
    return true;
}

bool isSupportedBinCount(const std::uint64_t value)
{
    constexpr std::array<std::uint64_t, 7> supported {
        1024, 2048, 4096, 8192, 16384, 32768, 65536
    };
    for (const std::uint64_t candidate : supported) {
        if (candidate == value) {
            return true;
        }
    }
    return false;
}

bool parseDirection(const QJsonObject& sweep,
                    SweepDirection& direction,
                    QString& error)
{
    const QJsonValue value = sweep.value(QStringLiteral("direction"));
    if (!value.isString()) {
        return fail(error, QStringLiteral("simulation.sweep.direction"),
                    QStringLiteral("必须是字符串。"));
    }
    const QString token = value.toString().trimmed().toLower();
    if (token == QStringLiteral("up")) {
        direction = SweepDirection::Up;
        return true;
    }
    if (token == QStringLiteral("down")) {
        direction = SweepDirection::Down;
        return true;
    }
    if (token == QStringLiteral("ping-pong")) {
        direction = SweepDirection::PingPong;
        return true;
    }
    return fail(error, QStringLiteral("simulation.sweep.direction"),
                QStringLiteral("只接受 up、down 或 ping-pong。"));
}

bool parseFaultInjection(const QJsonObject& simulation,
                         SimulationFaultConfig& faults,
                         QString& error)
{
    QJsonObject object;
    if (!readObject(simulation, QStringLiteral("faultInjection"),
                    QStringLiteral("simulation.faultInjection"), object, error)) {
        return false;
    }

    std::uint64_t integer = 0;
    if (!readInteger(object, QStringLiteral("pauseEveryFrames"),
                     QStringLiteral("simulation.faultInjection.pauseEveryFrames"),
                     0.0, 1000000.0, integer, error)) {
        return false;
    }
    faults.pauseEveryFrames = static_cast<std::uint32_t>(integer);
    if (!readNumber(object, QStringLiteral("pauseDurationSeconds"),
                    QStringLiteral("simulation.faultInjection.pauseDurationSeconds"),
                    0.001, 60.0, faults.pauseDurationSeconds, error)
        || !readInteger(object, QStringLiteral("sequenceJumpEveryFrames"),
                        QStringLiteral("simulation.faultInjection.sequenceJumpEveryFrames"),
                        0.0, 1000000.0, integer, error)) {
        return false;
    }
    faults.sequenceJumpEveryFrames = static_cast<std::uint32_t>(integer);
    if (!readInteger(object, QStringLiteral("sequenceSkipCount"),
                     QStringLiteral("simulation.faultInjection.sequenceSkipCount"),
                     1.0, 1000000.0, integer, error)) {
        return false;
    }
    faults.sequenceSkipCount = static_cast<std::uint32_t>(integer);
    if (!readInteger(object, QStringLiteral("invalidFrameEveryFrames"),
                     QStringLiteral("simulation.faultInjection.invalidFrameEveryFrames"),
                     0.0, 1000000.0, integer, error)) {
        return false;
    }
    faults.invalidFrameEveryFrames = static_cast<std::uint32_t>(integer);
    if (!readInteger(object, QStringLiteral("burstEveryFrames"),
                     QStringLiteral("simulation.faultInjection.burstEveryFrames"),
                     0.0, 1000000.0, integer, error)) {
        return false;
    }
    faults.burstEveryFrames = static_cast<std::uint32_t>(integer);
    if (!readInteger(object, QStringLiteral("burstFrameCount"),
                     QStringLiteral("simulation.faultInjection.burstFrameCount"),
                     1.0, 10000.0, integer, error)) {
        return false;
    }
    faults.burstFrameCount = static_cast<std::uint32_t>(integer);
    return true;
}

bool parseSimulation(const QJsonObject& object,
                     const std::uint64_t schemaVersion,
                     SimulationConfig& config,
                     QString& error)
{
    std::uint64_t integer = 0;
    if (!readInteger(object, QStringLiteral("binCount"),
                     QStringLiteral("simulation.binCount"), 1024.0, 65536.0,
                     integer, error)
        || !isSupportedBinCount(integer)) {
        if (error.isEmpty()) {
            fail(error, QStringLiteral("simulation.binCount"),
                 QStringLiteral("必须是受支持的 2 的整数次幂。"));
        }
        return false;
    }
    config.binCount = static_cast<std::size_t>(integer);

    if (!readNumber(object, QStringLiteral("frameRate"),
                    QStringLiteral("simulation.frameRate"), 1.0, 1000.0,
                    config.frameRate, error)
        || !readBoolean(object, QStringLiteral("unthrottled"),
                        QStringLiteral("simulation.unthrottled"),
                        config.unthrottled, error)
        || !readNumber(object, QStringLiteral("centerFrequencyHz"),
                       QStringLiteral("simulation.centerFrequencyHz"),
                       0.0, 20.0e9, config.centerFrequencyHz, error)
        || !readNumber(object, QStringLiteral("spanHz"),
                       QStringLiteral("simulation.spanHz"),
                       1000.0, 10.0e9, config.spanHz, error)) {
        return false;
    }

    double number = 0.0;
    if (!readNumber(object, QStringLiteral("noiseFloorDbfs"),
                    QStringLiteral("simulation.noiseFloorDbfs"),
                    -180.0, -10.0, number, error)) {
        return false;
    }
    config.noiseFloorDbfs = static_cast<float>(number);
    if (!readNumber(object, QStringLiteral("noiseDeviationDb"),
                    QStringLiteral("simulation.noiseDeviationDb"),
                    0.0, 30.0, number, error)) {
        return false;
    }
    config.noiseDeviationDb = static_cast<float>(number);
    if (!readInteger(object, QStringLiteral("randomSeed"),
                     QStringLiteral("simulation.randomSeed"), 1.0,
                     static_cast<double>(std::numeric_limits<std::uint32_t>::max()),
                     integer, error)) {
        return false;
    }
    config.randomSeed = static_cast<std::uint32_t>(integer);

    const QJsonValue tonesValue = object.value(QStringLiteral("tones"));
    if (!tonesValue.isArray()) {
        return fail(error, QStringLiteral("simulation.tones"),
                    QStringLiteral("必须是数组。"));
    }
    const QJsonArray tones = tonesValue.toArray();
    if (tones.size() > 2) {
        return fail(error, QStringLiteral("simulation.tones"),
                    QStringLiteral("当前界面最多支持 2 个固定信号。"));
    }
    config.tones.clear();
    const double viewStartHz = config.centerFrequencyHz - config.spanHz * 0.5;
    const double viewStopHz = config.centerFrequencyHz + config.spanHz * 0.5;
    for (int index = 0; index < tones.size(); ++index) {
        if (!tones.at(index).isObject()) {
            return fail(error, QStringLiteral("simulation.tones[%1]").arg(index),
                        QStringLiteral("必须是对象。"));
        }
        const QJsonObject toneObject = tones.at(index).toObject();
        ToneConfig tone;
        const QString prefix = QStringLiteral("simulation.tones[%1]").arg(index);
        if (!readBoolean(toneObject, QStringLiteral("enabled"),
                         prefix + QStringLiteral(".enabled"), tone.enabled, error)
            || !readNumber(toneObject, QStringLiteral("frequencyHz"),
                           prefix + QStringLiteral(".frequencyHz"),
                           viewStartHz, viewStopHz, tone.frequencyHz, error)
            || !readNumber(toneObject, QStringLiteral("amplitudeDbfs"),
                           prefix + QStringLiteral(".amplitudeDbfs"),
                           -180.0, 0.0, number, error)) {
            return false;
        }
        if (tone.frequencyHz >= viewStopHz) {
            return fail(error, prefix + QStringLiteral(".frequencyHz"),
                        QStringLiteral("必须小于显示终止频率。"));
        }
        tone.amplitudeDbfs = static_cast<float>(number);
        if (toneObject.contains(QStringLiteral("widthHz"))) {
            if (!readNumber(toneObject, QStringLiteral("widthHz"),
                            prefix + QStringLiteral(".widthHz"),
                            1.0, 100.0e9, number, error)) {
                return false;
            }
            tone.widthHz = number;
        } else if (toneObject.contains(QStringLiteral("widthBins"))) {
            if (!readNumber(toneObject, QStringLiteral("widthBins"),
                            prefix + QStringLiteral(".widthBins"),
                            0.01, 100000.0, number, error)) {
                return false;
            }
            const double binResolution = config.spanHz / static_cast<double>(config.binCount);
            tone.widthHz = std::max(1.0, number * binResolution);
        } else {
            return fail(error, prefix + QStringLiteral(".widthHz"),
                        QStringLiteral("缺少 'widthHz' 字段。"));
        }
        config.tones.push_back(tone);
    }

    QJsonObject sweep;
    if (!readObject(object, QStringLiteral("sweep"),
                    QStringLiteral("simulation.sweep"), sweep, error)
        || !readBoolean(sweep, QStringLiteral("enabled"),
                        QStringLiteral("simulation.sweep.enabled"),
                        config.sweepEnabled, error)
        || !readNumber(sweep, QStringLiteral("startFrequencyHz"),
                       QStringLiteral("simulation.sweep.startFrequencyHz"),
                       viewStartHz, viewStopHz, config.sweepStartHz, error)
        || !readNumber(sweep, QStringLiteral("stopFrequencyHz"),
                       QStringLiteral("simulation.sweep.stopFrequencyHz"),
                       viewStartHz, viewStopHz, config.sweepStopHz, error)) {
        return false;
    }
    if (config.sweepStartHz >= config.sweepStopHz) {
        return fail(error, QStringLiteral("simulation.sweep"),
                    QStringLiteral("起始频率必须小于终止频率。"));
    }
    if (config.sweepStopHz >= viewStopHz) {
        return fail(error, QStringLiteral("simulation.sweep.stopFrequencyHz"),
                    QStringLiteral("必须小于显示终止频率。"));
    }
    if (!readNumber(sweep, QStringLiteral("periodSeconds"),
                    QStringLiteral("simulation.sweep.periodSeconds"),
                    0.1, 3600.0, config.sweepPeriodSeconds, error)
        || !readNumber(sweep, QStringLiteral("amplitudeDbfs"),
                       QStringLiteral("simulation.sweep.amplitudeDbfs"),
                       -180.0, 0.0, number, error)
        || !parseDirection(sweep, config.sweepDirection, error)) {
        return false;
    }
    config.sweepAmplitudeDbfs = static_cast<float>(number);

    QJsonObject transient;
    if (!readObject(object, QStringLiteral("transient"),
                    QStringLiteral("simulation.transient"), transient, error)
        || !readNumber(transient, QStringLiteral("probabilityPerFrame"),
                       QStringLiteral("simulation.transient.probabilityPerFrame"),
                       0.0, 1.0, number, error)) {
        return false;
    }
    config.transientProbability = static_cast<float>(number);
    if (!readNumber(transient, QStringLiteral("amplitudeDbfs"),
                    QStringLiteral("simulation.transient.amplitudeDbfs"),
                    -180.0, 0.0, number, error)) {
        return false;
    }
    config.transientAmplitudeDbfs = static_cast<float>(number);
    if (!readNumber(transient, QStringLiteral("durationSeconds"),
                    QStringLiteral("simulation.transient.durationSeconds"),
                    0.001, 60.0, config.transientDurationSeconds, error)) {
        return false;
    }
    return schemaVersion < 2U || parseFaultInjection(object, config.faults, error);
}

} // namespace

SimulationScenarioLoadResult SimulationScenarioLoader::loadFile(const QString& filePath)
{
    SimulationScenarioLoadResult result;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        result.errorMessage = QStringLiteral("无法读取模拟场景 %1：%2")
                                  .arg(filePath, file.errorString());
        return result;
    }
    if (file.size() <= 0 || file.size() > kMaximumScenarioBytes) {
        result.errorMessage = QStringLiteral("模拟场景文件大小必须在 1 字节到 1 MiB 之间。");
        return result;
    }
    return loadData(file.read(kMaximumScenarioBytes + 1));
}

SimulationScenarioLoadResult SimulationScenarioLoader::loadData(const QByteArray& jsonData)
{
    SimulationScenarioLoadResult result;
    if (jsonData.isEmpty() || jsonData.size() > kMaximumScenarioBytes) {
        result.errorMessage = QStringLiteral("模拟场景文件大小必须在 1 字节到 1 MiB 之间。");
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.errorMessage = QStringLiteral("模拟场景 JSON 无效（偏移 %1）：%2")
                                  .arg(parseError.offset)
                                  .arg(parseError.errorString());
        return result;
    }

    const QJsonObject root = document.object();
    std::uint64_t version = 0;
    if (!readInteger(root, QStringLiteral("schemaVersion"),
                     QStringLiteral("schemaVersion"), 1.0, 2.0,
                     version, result.errorMessage)) {
        return result;
    }

    const QJsonValue name = root.value(QStringLiteral("name"));
    if (!name.isString() || name.toString().trimmed().isEmpty()
        || name.toString().size() > 128) {
        fail(result.errorMessage, QStringLiteral("name"),
             QStringLiteral("必须是 1～128 个字符的字符串。"));
        return result;
    }

    QJsonObject simulation;
    if (!readObject(root, QStringLiteral("simulation"),
                    QStringLiteral("simulation"), simulation, result.errorMessage)
        || !parseSimulation(simulation, version,
                            result.configuration, result.errorMessage)) {
        return result;
    }

    result.scenarioName = name.toString().trimmed();
    result.success = true;
    return result;
}

} // namespace rtsa
