#pragma once

#include "core/SpectrumPipeline.h"
#include "sources/ISimulationConfigurable.h"
#include "sources/ISpectrumSource.h"

#include <QColor>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QMainWindow>

#include <cstdint>
#include <array>
#include <memory>

class QAction;
class QActionGroup;
class QComboBox;
class QCheckBox;
class QCloseEvent;
class QDialog;
class QEvent;
class QDoubleSpinBox;
class FrequencySpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QSplitter;
class QTabWidget;
class QTimer;

namespace rtsa {

class SpectrumPlotWidget;
class WaterfallPlotWidget;
struct ExportResult;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(std::unique_ptr<ISpectrumSource> source,
                        QWidget* parent = nullptr,
                        bool settingsEnabled = true,
                        const SimulationConfig* initialSimulation = nullptr);
    ~MainWindow() override;

public slots:
    void startAcquisition();
    void pauseAcquisition();
    void stopAcquisition();
    void singleAcquisition();
    void toggleFullScreen();
    void autoRangeAmplitude();
    void resetFrequencyRange();
    void saveScreenshot();
    void saveSimulationScenario();
    void exportCsv();
    void handleExportFinished();
    void showAboutDialog();
    void showShortcutsDialog();
    void showUserGuideDialog();
    void showTelemetryDialog();
    void setPlotTheme(int themeIndex);
    void chooseCustomThemeColor();
    void setTraceColorPreset(int presetIndex);
    void chooseCustomTraceColor();
    void setTraceLineWidth(int width);
    void setGridVisible(bool visible);
    void setDisplayViewMode(int mode);
    void setWaterfallColormap(int colormap);

private slots:
    void refreshDisplay();
    void refreshStatistics();
    void applySourceConfiguration();
    void applyNonFrequencySourceConfiguration();
    void applyStartStopConfiguration();
    void applyTraceConfiguration();
    void applyAmplitudeScale();
    void applyVerticalScale();
    void applyPlotAppearance();
    void applyDisplayViewMode();
    void applyWaterfallSettings();
    void measureRangePeak();
    void measureChannelPower();
    void handleSourceState(int stateValue);
    void handleMarkerChanged(double frequencyHz, float amplitude,
                             const QString& unitText, bool calibrated);
    void handleSpanScaleRequested(double scaleFactor, double anchorFrequencyHz);
    void handleFrequencyPanRequested(double centerShiftHz);
    void handleFrequencyRangeSelected(double startFrequencyHz, double stopFrequencyHz);
    void recordPaintedFrameLatency(std::uint64_t publicationSequence,
                                   std::uint64_t timestampNs);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void buildUi();
    void buildMenuBar();
    void buildStatusBar();
    QWidget* buildControlPanel();
    QWidget* buildSourceGroup();
    QWidget* buildDisplayGroup();
    QWidget* buildWaterfallGroup();
    QWidget* buildSimulationGroup();
    QWidget* buildTraceGroup();
    QWidget* buildMarkerGroup();
    QWidget* buildMeasurementGroup();
    QWidget* buildFileGroup();
    QWidget* buildTelemetryGroup();
    void connectUi();
    void loadSettings();
    void loadSimulationConfiguration(const SimulationConfig& config);
    void saveSettings() const;
    void updateButtonStates(SourceState state);
    void refreshMarkerLabels();
    void configureSourceFromUi();
    void synchronizeStartStopFromCenterSpan();
    SimulationConfig configurationFromUi() const;
    static QString formatRate(double value, const QString& suffix);
    double displayLatencyP95() const;

    SpectrumPipeline pipeline_;
    std::unique_ptr<ISpectrumSource> source_;
    ISimulationConfigurable* simulationControl_ = nullptr;
    SpectrumPlotWidget* plot_ = nullptr;
    WaterfallPlotWidget* waterfallPlot_ = nullptr;
    QSplitter* plotSplitter_ = nullptr;
    QTimer* renderTimer_ = nullptr;
    QTimer* statisticsTimer_ = nullptr;

    FrequencySpinBox* centerFrequencySpin_ = nullptr;
    FrequencySpinBox* spanSpin_ = nullptr;
    FrequencySpinBox* startFrequencySpin_ = nullptr;
    FrequencySpinBox* stopFrequencySpin_ = nullptr;
    QComboBox* fftSizeCombo_ = nullptr;
    QDoubleSpinBox* sourceFrameRateSpin_ = nullptr;
    QCheckBox* unthrottledCheck_ = nullptr;
    QDoubleSpinBox* noiseFloorSpin_ = nullptr;
    QDoubleSpinBox* noiseDeviationSpin_ = nullptr;
    QCheckBox* tone1EnabledCheck_ = nullptr;
    FrequencySpinBox* tone1FrequencySpin_ = nullptr;
    QDoubleSpinBox* tone1AmplitudeSpin_ = nullptr;
    FrequencySpinBox* tone1WidthSpin_ = nullptr;
    QCheckBox* tone2EnabledCheck_ = nullptr;
    FrequencySpinBox* tone2FrequencySpin_ = nullptr;
    QDoubleSpinBox* tone2AmplitudeSpin_ = nullptr;
    FrequencySpinBox* tone2WidthSpin_ = nullptr;
    QCheckBox* sweepEnabledCheck_ = nullptr;
    FrequencySpinBox* sweepStartFrequencySpin_ = nullptr;
    FrequencySpinBox* sweepStopFrequencySpin_ = nullptr;
    QComboBox* sweepDirectionCombo_ = nullptr;
    QDoubleSpinBox* sweepPeriodSpin_ = nullptr;
    QDoubleSpinBox* sweepAmplitudeSpin_ = nullptr;
    QDoubleSpinBox* transientProbabilitySpin_ = nullptr;
    QDoubleSpinBox* transientAmplitudeSpin_ = nullptr;
    QDoubleSpinBox* transientDurationSpin_ = nullptr;
    QDoubleSpinBox* referenceLevelSpin_ = nullptr;
    QDoubleSpinBox* bottomLevelSpin_ = nullptr;
    QDoubleSpinBox* verticalScaleSpin_ = nullptr;
    QComboBox* displayViewModeCombo_ = nullptr;
    QComboBox* waterfallColormapCombo_ = nullptr;
    QSpinBox* waterfallHistorySpin_ = nullptr;
    QPushButton* waterfallClearButton_ = nullptr;
    QComboBox* traceModeCombo_ = nullptr;
    QComboBox* activeMarkerCombo_ = nullptr;
    QSpinBox* averageCountSpin_ = nullptr;
    QDoubleSpinBox* peakThresholdSpin_ = nullptr;
    FrequencySpinBox* measurementStartSpin_ = nullptr;
    FrequencySpinBox* measurementStopSpin_ = nullptr;
    QCheckBox* deltaMarkerCheck_ = nullptr;

    int plotColorPreset_ = 0;
    QColor customTraceColor_ { 0, 235, 180 };
    int plotLineWidth_ = 1;
    bool plotGridVisible_ = true;
    int plotTheme_ = 0;
    QColor customThemeColor_ { 4, 9, 14 };

    QActionGroup* viewModeActionGroup_ = nullptr;
    QActionGroup* themeActionGroup_ = nullptr;
    QActionGroup* traceColorActionGroup_ = nullptr;
    QActionGroup* lineWidthActionGroup_ = nullptr;
    QAction* gridAction_ = nullptr;
    QActionGroup* colormapActionGroup_ = nullptr;
    QAction* customThemeAction_ = nullptr;
    QAction* customTraceColorAction_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* pauseButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* singleButton_ = nullptr;
    QPushButton* fullScreenButton_ = nullptr;
    QPushButton* autoRangeButton_ = nullptr;
    QPushButton* peakButton_ = nullptr;
    QPushButton* nextPeakButton_ = nullptr;
    QPushButton* previousPeakButton_ = nullptr;
    QPushButton* clearMarkerButton_ = nullptr;
    QPushButton* clearAllMarkersButton_ = nullptr;
    QPushButton* rangePeakButton_ = nullptr;
    QPushButton* channelPowerButton_ = nullptr;
    QPushButton* resetTraceButton_ = nullptr;
    QPushButton* screenshotButton_ = nullptr;
    QPushButton* exportCsvButton_ = nullptr;
    QPushButton* saveScenarioButton_ = nullptr;
    QLabel* markerLabel_ = nullptr;
    QLabel* deltaMarkerLabel_ = nullptr;
    QLabel* measurementResultLabel_ = nullptr;
    QLabel* sourceStateLabel_ = nullptr;
    QLabel* inputRateLabel_ = nullptr;
    QLabel* displayRateLabel_ = nullptr;
    QLabel* dataRateLabel_ = nullptr;
    QLabel* sourceDropLabel_ = nullptr;
    QLabel* invalidFrameLabel_ = nullptr;
    QLabel* processingDropLabel_ = nullptr;
    QLabel* publishedFrameLabel_ = nullptr;
    QLabel* displaySkippedLabel_ = nullptr;
    QLabel* uptimeLabel_ = nullptr;
    QLabel* fftSizeLabel_ = nullptr;
    QLabel* lastErrorLabel_ = nullptr;
    QLabel* latencyLabel_ = nullptr;
    QLabel* latencyP95Label_ = nullptr;
    QLabel* processingTimeLabel_ = nullptr;
    QLabel* renderTimeLabel_ = nullptr;
    QLabel* queueDepthLabel_ = nullptr;
    QLabel* fileOperationLabel_ = nullptr;
    QLabel* statusStateChip_ = nullptr;
    QLabel* statusMetricsLabel_ = nullptr;
    QTabWidget* mainTabWidget_ = nullptr;
    QDialog* telemetryDialog_ = nullptr;

    QFutureWatcher<ExportResult>* exportWatcher_ = nullptr;
    bool settingsEnabled_ = true;
    double fullRangeCenterHz_ = 1.0e9;
    double fullRangeSpanHz_ = 200.0e6;

    std::uint64_t lastDisplayedPublicationSequence_ = 0;
    std::uint64_t lastLatencyPublicationSequence_ = 0;
    std::uint64_t displayedFramesInWindow_ = 0;
    std::uint64_t displayedFramesTotal_ = 0;
    std::uint64_t displaySkippedFrames_ = 0;
    double displayFramesPerSecond_ = 0.0;
    std::array<double, 256> displayLatenciesMs_ {};
    std::size_t displayLatencyCount_ = 0;
    std::size_t displayLatencyWriteIndex_ = 0;
    QElapsedTimer displayRateTimer_;
    QString lastSourceError_;
    QString lastSourceErrorTime_;
};

} // namespace rtsa
