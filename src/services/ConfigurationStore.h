#pragma once

#include <QByteArray>
#include <QString>

class QSettings;

namespace rtsa {

struct AppSettings {
    static constexpr int schemaVersion = 4;

    QByteArray windowGeometry;
    double centerFrequencyMHz = 1000.0;
    double spanMHz = 200.0;
    int binCount = 16384;
    double sourceFrameRate = 200.0;
    bool unthrottled = false;
    double noiseFloorDbfs = -110.0;
    double noiseDeviationDb = 1.5;
    bool tone1Enabled = true;
    double tone1FrequencyMHz = 980.0;
    double tone1AmplitudeDbfs = -35.0;
    double tone1WidthBins = 1.5;
    bool tone2Enabled = true;
    double tone2FrequencyMHz = 1035.0;
    double tone2AmplitudeDbfs = -18.0;
    double tone2WidthBins = 2.5;
    bool sweepEnabled = true;
    double sweepStartFrequencyMHz = 930.0;
    double sweepStopFrequencyMHz = 1070.0;
    int sweepDirection = 0;
    double sweepPeriodSeconds = 4.0;
    double sweepAmplitudeDbfs = -45.0;
    double transientProbabilityPercent = 0.2;
    double transientAmplitudeDbfs = -12.0;
    double transientDurationSeconds = 0.1;
    double referenceLevelDbfs = 0.0;
    double bottomLevelDbfs = -140.0;
    int plotColorPreset = 0;
    int plotLineWidth = 1;
    bool plotGridVisible = true;
    int plotTheme = 0;
    int traceMode = 0;
    int averageCount = 16;
    int displayViewMode = 2; // 0: Spectrum, 1: Waterfall, 2: Split (Spectrum + Waterfall)
    int waterfallColormap = 0; // 0: Turbo, 1: Viridis, 2: Jet, 3: Hot, 4: Grayscale
    int waterfallHistoryDepth = 512;

    bool isValid() const noexcept;
};

struct SettingsLoadResult {
    AppSettings settings;
    bool loadedFromStorage = false;
    bool migratedLegacy = false;
    QString warning;
};

struct SettingsSaveResult {
    bool success = false;
    QString errorMessage;
};

class ConfigurationStore final {
public:
    ConfigurationStore() = delete;

    static SettingsLoadResult load(QSettings& storage);
    static SettingsSaveResult save(QSettings& storage, const AppSettings& settings);
};

} // namespace rtsa
