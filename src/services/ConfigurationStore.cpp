#include "services/ConfigurationStore.h"

#include <QSettings>
#include <QStringList>
#include <QVariant>

#include <algorithm>
#include <array>
#include <cmath>

namespace rtsa {
namespace {

bool isSupportedBinCount(const int value) noexcept
{
    constexpr std::array<int, 7> supported { 1024, 2048, 4096, 8192,
                                              16384, 32768, 65536 };
    return std::find(supported.cbegin(), supported.cend(), value) != supported.cend();
}

bool readDouble(QSettings& storage,
                const QString& key,
                const double fallback,
                double& destination)
{
    if (!storage.contains(key)) {
        destination = fallback;
        return true;
    }
    bool ok = false;
    const double value = storage.value(key).toDouble(&ok);
    if (!ok || !std::isfinite(value)) {
        return false;
    }
    destination = value;
    return true;
}

bool readInt(QSettings& storage,
             const QString& key,
             const int fallback,
             int& destination)
{
    if (!storage.contains(key)) {
        destination = fallback;
        return true;
    }
    bool ok = false;
    const int value = storage.value(key).toInt(&ok);
    if (!ok) {
        return false;
    }
    destination = value;
    return true;
}

bool readBool(QSettings& storage,
              const QString& key,
              const bool fallback,
              bool& destination)
{
    if (!storage.contains(key)) {
        destination = fallback;
        return true;
    }
    const QString value = storage.value(key).toString().trimmed().toLower();
    if (value == QStringLiteral("true") || value == QStringLiteral("1")) {
        destination = true;
        return true;
    }
    if (value == QStringLiteral("false") || value == QStringLiteral("0")) {
        destination = false;
        return true;
    }
    return false;
}

QString statusMessage(const QSettings::Status status)
{
    switch (status) {
    case QSettings::AccessError:
        return QStringLiteral("配置文件不可访问，已使用安全默认值。");
    case QSettings::FormatError:
        return QStringLiteral("配置文件格式损坏，已使用安全默认值。");
    case QSettings::NoError:
    default:
        return {};
    }
}

} // namespace

bool AppSettings::isValid() const noexcept
{
    return std::isfinite(centerFrequencyMHz)
        && centerFrequencyMHz >= 0.0 && centerFrequencyMHz <= 20000.0
        && std::isfinite(spanMHz) && spanMHz >= 0.001 && spanMHz <= 10000.0
        && isSupportedBinCount(binCount)
        && std::isfinite(sourceFrameRate)
        && sourceFrameRate >= 1.0 && sourceFrameRate <= 1000.0
        && std::isfinite(noiseFloorDbfs)
        && noiseFloorDbfs >= -180.0 && noiseFloorDbfs <= -10.0
        && std::isfinite(noiseDeviationDb)
        && noiseDeviationDb >= 0.0 && noiseDeviationDb <= 30.0
        && std::isfinite(tone1FrequencyMHz)
        && tone1FrequencyMHz >= 0.0 && tone1FrequencyMHz <= 25000.0
        && std::isfinite(tone1AmplitudeDbfs)
        && tone1AmplitudeDbfs >= -180.0 && tone1AmplitudeDbfs <= 0.0
        && std::isfinite(tone1WidthMHz)
        && tone1WidthMHz >= 0.0001 && tone1WidthMHz <= 10000.0
        && std::isfinite(tone2FrequencyMHz)
        && tone2FrequencyMHz >= 0.0 && tone2FrequencyMHz <= 25000.0
        && std::isfinite(tone2AmplitudeDbfs)
        && tone2AmplitudeDbfs >= -180.0 && tone2AmplitudeDbfs <= 0.0
        && std::isfinite(tone2WidthMHz)
        && tone2WidthMHz >= 0.0001 && tone2WidthMHz <= 10000.0
        && std::isfinite(sweepStartFrequencyMHz)
        && sweepStartFrequencyMHz >= 0.0 && sweepStartFrequencyMHz <= 25000.0
        && std::isfinite(sweepStopFrequencyMHz)
        && sweepStopFrequencyMHz >= 0.0 && sweepStopFrequencyMHz <= 25000.0
        && sweepStartFrequencyMHz < sweepStopFrequencyMHz
        && sweepDirection >= 0 && sweepDirection <= 2
        && std::isfinite(sweepPeriodSeconds)
        && sweepPeriodSeconds >= 0.1 && sweepPeriodSeconds <= 3600.0
        && std::isfinite(sweepAmplitudeDbfs)
        && sweepAmplitudeDbfs >= -180.0 && sweepAmplitudeDbfs <= 0.0
        && std::isfinite(transientProbabilityPercent)
        && transientProbabilityPercent >= 0.0 && transientProbabilityPercent <= 100.0
        && std::isfinite(transientAmplitudeDbfs)
        && transientAmplitudeDbfs >= -180.0 && transientAmplitudeDbfs <= 0.0
        && std::isfinite(transientDurationSeconds)
        && transientDurationSeconds >= 0.001 && transientDurationSeconds <= 60.0
        && std::isfinite(referenceLevelDbfs)
        && referenceLevelDbfs >= -100.0 && referenceLevelDbfs <= 50.0
        && std::isfinite(bottomLevelDbfs)
        && bottomLevelDbfs >= -200.0 && bottomLevelDbfs <= 40.0
        && bottomLevelDbfs < referenceLevelDbfs
        && plotColorPreset >= 0 && plotColorPreset <= 3
        && plotLineWidth >= 1 && plotLineWidth <= 4
        && plotTheme >= 0 && plotTheme <= 1
        && traceMode >= 0 && traceMode <= 3
        && averageCount >= 1 && averageCount <= 1024
        && displayViewMode >= 0 && displayViewMode <= 2
        && waterfallColormap >= 0 && waterfallColormap <= 4
        && waterfallHistoryDepth >= 64 && waterfallHistoryDepth <= 2048;
}

SettingsLoadResult ConfigurationStore::load(QSettings& storage)
{
    SettingsLoadResult result;
    const AppSettings defaults;

    const bool hasVersion = storage.contains(QStringLiteral("schemaVersion"));
    const bool hasLegacySpectrum = storage.childGroups().contains(QStringLiteral("Spectrum"));
    if (!hasVersion && !hasLegacySpectrum) {
        result.warning = statusMessage(storage.status());
        return result;
    }

    int version = 0;
    if (hasVersion) {
        bool versionOk = false;
        version = storage.value(QStringLiteral("schemaVersion")).toInt(&versionOk);
        if (!versionOk || version < 1 || version > AppSettings::schemaVersion) {
            result.warning = QStringLiteral("配置版本不受支持，已使用安全默认值。");
            return result;
        }
        result.migratedLegacy = version < AppSettings::schemaVersion;
    } else {
        result.migratedLegacy = true;
    }

    AppSettings candidate;
    storage.beginGroup(QStringLiteral("MainWindow"));
    candidate.windowGeometry = storage.value(QStringLiteral("geometry")).toByteArray();
    storage.endGroup();

    bool conversionOk = true;
    storage.beginGroup(QStringLiteral("Spectrum"));
    conversionOk = readDouble(storage, QStringLiteral("centerFrequencyMHz"),
                              defaults.centerFrequencyMHz, candidate.centerFrequencyMHz)
        && readDouble(storage, QStringLiteral("spanMHz"),
                      defaults.spanMHz, candidate.spanMHz)
        && readInt(storage, QStringLiteral("binCount"),
                   defaults.binCount, candidate.binCount)
        && readDouble(storage, QStringLiteral("sourceFrameRate"),
                      defaults.sourceFrameRate, candidate.sourceFrameRate)
        && readBool(storage, QStringLiteral("unthrottled"),
                    defaults.unthrottled, candidate.unthrottled)
        && readDouble(storage, QStringLiteral("noiseFloorDbfs"),
                      defaults.noiseFloorDbfs, candidate.noiseFloorDbfs)
        && readDouble(storage, QStringLiteral("noiseDeviationDb"),
                      defaults.noiseDeviationDb, candidate.noiseDeviationDb)
        && readBool(storage, QStringLiteral("tone1Enabled"),
                    defaults.tone1Enabled, candidate.tone1Enabled)
        && readDouble(storage, QStringLiteral("tone1FrequencyMHz"),
                      defaults.tone1FrequencyMHz, candidate.tone1FrequencyMHz)
        && readDouble(storage, QStringLiteral("tone1AmplitudeDbfs"),
                      defaults.tone1AmplitudeDbfs, candidate.tone1AmplitudeDbfs)
        && (storage.contains(QStringLiteral("tone1WidthMHz"))
                ? readDouble(storage, QStringLiteral("tone1WidthMHz"),
                             defaults.tone1WidthMHz, candidate.tone1WidthMHz)
                : readDouble(storage, QStringLiteral("tone1WidthBins"),
                             defaults.tone1WidthMHz, candidate.tone1WidthMHz))
        && readBool(storage, QStringLiteral("tone2Enabled"),
                    defaults.tone2Enabled, candidate.tone2Enabled)
        && readDouble(storage, QStringLiteral("tone2FrequencyMHz"),
                      defaults.tone2FrequencyMHz, candidate.tone2FrequencyMHz)
        && readDouble(storage, QStringLiteral("tone2AmplitudeDbfs"),
                      defaults.tone2AmplitudeDbfs, candidate.tone2AmplitudeDbfs)
        && (storage.contains(QStringLiteral("tone2WidthMHz"))
                ? readDouble(storage, QStringLiteral("tone2WidthMHz"),
                             defaults.tone2WidthMHz, candidate.tone2WidthMHz)
                : readDouble(storage, QStringLiteral("tone2WidthBins"),
                             defaults.tone2WidthMHz, candidate.tone2WidthMHz))
        && readBool(storage, QStringLiteral("sweepEnabled"),
                    defaults.sweepEnabled, candidate.sweepEnabled)
        && readDouble(storage, QStringLiteral("sweepStartFrequencyMHz"),
                      defaults.sweepStartFrequencyMHz, candidate.sweepStartFrequencyMHz)
        && readDouble(storage, QStringLiteral("sweepStopFrequencyMHz"),
                      defaults.sweepStopFrequencyMHz, candidate.sweepStopFrequencyMHz)
        && readInt(storage, QStringLiteral("sweepDirection"),
                   defaults.sweepDirection, candidate.sweepDirection)
        && readDouble(storage, QStringLiteral("sweepPeriodSeconds"),
                      defaults.sweepPeriodSeconds, candidate.sweepPeriodSeconds)
        && readDouble(storage, QStringLiteral("sweepAmplitudeDbfs"),
                      defaults.sweepAmplitudeDbfs, candidate.sweepAmplitudeDbfs)
        && readDouble(storage, QStringLiteral("transientProbabilityPercent"),
                      defaults.transientProbabilityPercent,
                      candidate.transientProbabilityPercent)
        && readDouble(storage, QStringLiteral("transientAmplitudeDbfs"),
                      defaults.transientAmplitudeDbfs, candidate.transientAmplitudeDbfs)
        && readDouble(storage, QStringLiteral("transientDurationSeconds"),
                      defaults.transientDurationSeconds,
                      candidate.transientDurationSeconds)
        && readDouble(storage, QStringLiteral("referenceLevelDbfs"),
                      defaults.referenceLevelDbfs, candidate.referenceLevelDbfs)
        && readDouble(storage, QStringLiteral("bottomLevelDbfs"),
                      defaults.bottomLevelDbfs, candidate.bottomLevelDbfs)
        && readInt(storage, QStringLiteral("plotColorPreset"),
                   defaults.plotColorPreset, candidate.plotColorPreset)
        && readInt(storage, QStringLiteral("plotLineWidth"),
                   defaults.plotLineWidth, candidate.plotLineWidth)
        && readBool(storage, QStringLiteral("plotGridVisible"),
                    defaults.plotGridVisible, candidate.plotGridVisible)
        && readInt(storage, QStringLiteral("plotTheme"),
                   defaults.plotTheme, candidate.plotTheme)
        && readInt(storage, QStringLiteral("traceMode"),
                   defaults.traceMode, candidate.traceMode)
        && readInt(storage, QStringLiteral("averageCount"),
                   defaults.averageCount, candidate.averageCount)
        && readInt(storage, QStringLiteral("displayViewMode"),
                   defaults.displayViewMode, candidate.displayViewMode)
        && readInt(storage, QStringLiteral("waterfallColormap"),
                   defaults.waterfallColormap, candidate.waterfallColormap)
        && readInt(storage, QStringLiteral("waterfallHistoryDepth"),
                   defaults.waterfallHistoryDepth, candidate.waterfallHistoryDepth);
    storage.endGroup();

    const QString storageError = statusMessage(storage.status());
    if (!storageError.isEmpty()) {
        result.warning = storageError;
        return result;
    }
    if (!conversionOk || !candidate.isValid()) {
        result.warning = QStringLiteral("配置包含无效值，已整体回退到安全默认值。");
        return result;
    }

    result.settings = candidate;
    result.loadedFromStorage = true;
    return result;
}

SettingsSaveResult ConfigurationStore::save(QSettings& storage,
                                             const AppSettings& settings)
{
    if (!settings.isValid()) {
        return SettingsSaveResult { false, QStringLiteral("拒绝保存无效配置。") };
    }

    storage.setAtomicSyncRequired(true);
    storage.setValue(QStringLiteral("schemaVersion"), AppSettings::schemaVersion);
    storage.beginGroup(QStringLiteral("MainWindow"));
    storage.setValue(QStringLiteral("geometry"), settings.windowGeometry);
    storage.endGroup();

    storage.beginGroup(QStringLiteral("Spectrum"));
    storage.setValue(QStringLiteral("centerFrequencyMHz"), settings.centerFrequencyMHz);
    storage.setValue(QStringLiteral("spanMHz"), settings.spanMHz);
    storage.setValue(QStringLiteral("binCount"), settings.binCount);
    storage.setValue(QStringLiteral("sourceFrameRate"), settings.sourceFrameRate);
    storage.setValue(QStringLiteral("unthrottled"), settings.unthrottled);
    storage.setValue(QStringLiteral("noiseFloorDbfs"), settings.noiseFloorDbfs);
    storage.setValue(QStringLiteral("noiseDeviationDb"), settings.noiseDeviationDb);
    storage.setValue(QStringLiteral("tone1Enabled"), settings.tone1Enabled);
    storage.setValue(QStringLiteral("tone1FrequencyMHz"), settings.tone1FrequencyMHz);
    storage.setValue(QStringLiteral("tone1AmplitudeDbfs"), settings.tone1AmplitudeDbfs);
    storage.setValue(QStringLiteral("tone1WidthMHz"), settings.tone1WidthMHz);
    storage.setValue(QStringLiteral("tone2Enabled"), settings.tone2Enabled);
    storage.setValue(QStringLiteral("tone2FrequencyMHz"), settings.tone2FrequencyMHz);
    storage.setValue(QStringLiteral("tone2AmplitudeDbfs"), settings.tone2AmplitudeDbfs);
    storage.setValue(QStringLiteral("tone2WidthMHz"), settings.tone2WidthMHz);
    storage.setValue(QStringLiteral("sweepEnabled"), settings.sweepEnabled);
    storage.setValue(QStringLiteral("sweepStartFrequencyMHz"), settings.sweepStartFrequencyMHz);
    storage.setValue(QStringLiteral("sweepStopFrequencyMHz"), settings.sweepStopFrequencyMHz);
    storage.setValue(QStringLiteral("sweepDirection"), settings.sweepDirection);
    storage.setValue(QStringLiteral("sweepPeriodSeconds"), settings.sweepPeriodSeconds);
    storage.setValue(QStringLiteral("sweepAmplitudeDbfs"), settings.sweepAmplitudeDbfs);
    storage.setValue(QStringLiteral("transientProbabilityPercent"),
                     settings.transientProbabilityPercent);
    storage.setValue(QStringLiteral("transientAmplitudeDbfs"),
                     settings.transientAmplitudeDbfs);
    storage.setValue(QStringLiteral("transientDurationSeconds"),
                     settings.transientDurationSeconds);
    storage.setValue(QStringLiteral("referenceLevelDbfs"), settings.referenceLevelDbfs);
    storage.setValue(QStringLiteral("bottomLevelDbfs"), settings.bottomLevelDbfs);
    storage.setValue(QStringLiteral("plotColorPreset"), settings.plotColorPreset);
    storage.setValue(QStringLiteral("plotLineWidth"), settings.plotLineWidth);
    storage.setValue(QStringLiteral("plotGridVisible"), settings.plotGridVisible);
    storage.setValue(QStringLiteral("plotTheme"), settings.plotTheme);
    storage.setValue(QStringLiteral("traceMode"), settings.traceMode);
    storage.setValue(QStringLiteral("averageCount"), settings.averageCount);
    storage.setValue(QStringLiteral("displayViewMode"), settings.displayViewMode);
    storage.setValue(QStringLiteral("waterfallColormap"), settings.waterfallColormap);
    storage.setValue(QStringLiteral("waterfallHistoryDepth"), settings.waterfallHistoryDepth);
    storage.endGroup();
    storage.sync();

    const QString error = statusMessage(storage.status());
    return SettingsSaveResult { error.isEmpty(), error };
}

} // namespace rtsa
