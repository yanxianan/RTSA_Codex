#include "ui/MainWindow.h"

#include "core/AmplitudeUnits.h"
#include "core/SpectrumMeasurements.h"
#include "plot/SpectrumPlotWidget.h"
#include "plot/WaterfallPlotWidget.h"
#include "services/ConfigurationStore.h"
#include "services/SpectrumExporter.h"
#include "sources/SimulationScenarioWriter.h"
#include "ui/FrequencySpinBox.h"
#include "ui/UnfocusedWheelFilter.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QKeySequence>
#include <QMenuBar>
#include <QMenu>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace rtsa {
namespace {

constexpr std::array<QRgb, 6> kClassicTraceRgb = {
    0xFF00EBB4, // 0: 经典翠绿 (Emerald Green)
    0xFF00A6FF, // 1: 科技青蓝 (Cyan Blue)
    0xFFFFCD37, // 2: 琥珀明黄 (Amber Yellow)
    0xFFEBF0F5, // 3: 纯净亮白 (Pure White)
    0xFFFF6D00, // 4: 工业烈橙 (Industrial Orange)
    0xFFFF4081  // 5: 荧光洋红 (Neon Magenta)
};

constexpr std::array<QRgb, 4> kClassicThemeRgb = {
    0xFF04090E, // 0: 经典深黑 (Classic Dark)
    0xFFFAFCFD, // 1: 明亮浅色 (High Contrast Light)
    0xFF07131E, // 2: 深海科技 (Deep Navy)
    0xFF000000  // 3: 复古纯黑 (Pitch Black)
};

[[maybe_unused]] QString sourceStateText(const SourceState state)
{
    switch (state) {
    case SourceState::Initialized:
        return QObject::tr("已初始化");
    case SourceState::Starting:
        return QObject::tr("正在启动");
    case SourceState::Running:
        return QObject::tr("运行中");
    case SourceState::Paused:
        return QObject::tr("已暂停");
    case SourceState::Error:
        return QObject::tr("错误");
    case SourceState::Stopped:
    default:
        return QObject::tr("已停止");
    }
}

QString formatFrequencyDelta(const double frequencyHz)
{
    const double absolute = std::abs(frequencyHz);
    if (absolute >= 1.0e9) {
        return QStringLiteral("%1 GHz").arg(frequencyHz / 1.0e9, 0, 'f', 6);
    }
    if (absolute >= 1.0e6) {
        return QStringLiteral("%1 MHz").arg(frequencyHz / 1.0e6, 0, 'f', 6);
    }
    if (absolute >= 1.0e3) {
        return QStringLiteral("%1 kHz").arg(frequencyHz / 1.0e3, 0, 'f', 3);
    }
    return QStringLiteral("%1 Hz").arg(frequencyHz, 0, 'f', 1);
}

} // namespace

MainWindow::MainWindow(std::unique_ptr<ISpectrumSource> source,
                       QWidget* parent,
                       const bool settingsEnabled,
                       const SimulationConfig* initialSimulation)
    : QMainWindow(parent)
    , source_(std::move(source))
    , settingsEnabled_(settingsEnabled)
{
    if (!source_) {
        throw std::invalid_argument("MainWindow requires a spectrum source.");
    }
    simulationControl_ = dynamic_cast<ISimulationConfigurable*>(source_.get());
    setWindowTitle(tr("RFSoC ZU47DR 实时频谱分析仪"));
    resize(1440, 860);
    buildUi();
    loadSettings();
    if (initialSimulation && simulationControl_) {
        simulationControl_->configure(*initialSimulation);
        loadSimulationConfiguration(*initialSimulation);
    }
    fullRangeCenterHz_ = centerFrequencySpin_->frequencyHz();
    fullRangeSpanHz_ = spanSpin_->frequencyHz();
    connectUi();

    source_->setFrameSink([this](const SpectrumFramePtr& frame) {
        return pipeline_.submit(frame);
    });
    applyTraceConfiguration();
    applyAmplitudeScale();
    applyPlotAppearance();
    applyDisplayViewMode();
    applyWaterfallSettings();
    configureSourceFromUi();

    exportWatcher_ = new QFutureWatcher<ExportResult>(this);
    connect(exportWatcher_, &QFutureWatcher<ExportResult>::finished,
            this, &MainWindow::handleExportFinished);

    renderTimer_ = new QTimer(this);
    renderTimer_->setTimerType(Qt::PreciseTimer);
    renderTimer_->setInterval(10);
    connect(renderTimer_, &QTimer::timeout, this, &MainWindow::refreshDisplay);
    renderTimer_->start();

    statisticsTimer_ = new QTimer(this);
    statisticsTimer_->setInterval(500);
    connect(statisticsTimer_, &QTimer::timeout, this, &MainWindow::refreshStatistics);
    statisticsTimer_->start();

    displayRateTimer_.start();
    updateButtonStates(source_->state());
    refreshStatistics();
}

MainWindow::~MainWindow()
{
    source_->stop();
    if (exportWatcher_ && exportWatcher_->isRunning()) {
        exportWatcher_->waitForFinished();
    }
    saveSettings();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && fullScreenButton_) {
        fullScreenButton_->setText(isFullScreen() ? tr("退出全屏") : tr("进入全屏"));
    }
}

void MainWindow::startAcquisition()
{
    if (source_->state() == SourceState::Paused) {
        source_->resume();
        return;
    }
    if (source_->state() == SourceState::Running || source_->state() == SourceState::Starting) {
        return;
    }
    if (source_->state() == SourceState::Error) {
        // Join the failed worker before attempting a clean restart.
        source_->stop();
    }

    configureSourceFromUi();
    pipeline_.clear();
    plot_->setFrame({});
    lastDisplayedPublicationSequence_ = 0;
    lastLatencyPublicationSequence_ = 0;
    displayLatencyCount_ = 0;
    displayLatencyWriteIndex_ = 0;
    if (!source_->start()) {
        QMessageBox::warning(this, tr("启动失败"), tr("无法启动模拟数据源。"));
    }
}

void MainWindow::pauseAcquisition()
{
    source_->pause();
}

void MainWindow::stopAcquisition()
{
    source_->stop();
}

void MainWindow::singleAcquisition()
{
    source_->stop();
    configureSourceFromUi();
    pipeline_.clear();
    plot_->setFrame({});
    lastDisplayedPublicationSequence_ = 0;
    lastLatencyPublicationSequence_ = 0;
    displayLatencyCount_ = 0;
    displayLatencyWriteIndex_ = 0;
    if (!source_->start(AcquisitionMode::SingleFrame)) {
        QMessageBox::warning(this, tr("单次采集失败"), tr("无法启动模拟数据源。"));
    }
}

void MainWindow::toggleFullScreen()
{
    if (isFullScreen()) {
        showNormal();
        fullScreenButton_->setText(tr("进入全屏"));
    } else {
        showFullScreen();
        fullScreenButton_->setText(tr("退出全屏"));
    }
}

void MainWindow::refreshDisplay()
{
    const auto frame = pipeline_.latest();
    if (!frame
        || frame->metadata.publicationSequence == lastDisplayedPublicationSequence_) {
        return;
    }

    if (lastDisplayedPublicationSequence_ != 0
        && frame->metadata.publicationSequence > lastDisplayedPublicationSequence_ + 1U) {
        displaySkippedFrames_ += frame->metadata.publicationSequence
            - lastDisplayedPublicationSequence_ - 1U;
    }
    lastDisplayedPublicationSequence_ = frame->metadata.publicationSequence;
    ++displayedFramesInWindow_;
    ++displayedFramesTotal_;
    plot_->setFrame(frame);
    if (waterfallPlot_ && waterfallPlot_->isVisible()) {
        waterfallPlot_->addFrame(frame);
    }
}

void MainWindow::refreshStatistics()
{
    const auto sourceStats = source_->statistics();
    const auto pipelineStats = pipeline_.statistics();
    const qint64 displayElapsedMs = std::max<qint64>(1, displayRateTimer_.restart());
    displayFramesPerSecond_ = static_cast<double>(displayedFramesInWindow_) * 1000.0
        / static_cast<double>(displayElapsedMs);
    displayedFramesInWindow_ = 0;
    sourceStateLabel_->setText(sourceStateText(source_->state()));
    inputRateLabel_->setText(formatRate(sourceStats.intervalFrameRate, QStringLiteral("FPS")));
    displayRateLabel_->setText(formatRate(displayFramesPerSecond_, QStringLiteral("FPS")));
    dataRateLabel_->setText(formatRate(sourceStats.bytesPerSecond, QStringLiteral("B/s")));
    sourceDropLabel_->setText(QString::number(sourceStats.droppedFrames));
    invalidFrameLabel_->setText(QString::number(pipelineStats.invalidFrames));
    processingDropLabel_->setText(QString::number(pipelineStats.processingDrops));
    publishedFrameLabel_->setText(QStringLiteral("%1 / %2")
        .arg(pipelineStats.publishedFrames)
        .arg(pipelineStats.submittedFrames));
    displaySkippedLabel_->setText(QStringLiteral("%1 / %2")
        .arg(displaySkippedFrames_)
        .arg(displayedFramesTotal_));
    uptimeLabel_->setText(QStringLiteral("%1 s").arg(sourceStats.uptimeSeconds, 0, 'f', 1));
    processingTimeLabel_->setText(QStringLiteral("%1 ms")
        .arg(pipelineStats.lastProcessingMilliseconds, 0, 'f', 3));
    renderTimeLabel_->setText(QStringLiteral("%1 ms")
        .arg(plot_->lastPaintMilliseconds(), 0, 'f', 3));
    queueDepthLabel_->setText(QString::number(pipelineStats.queueDepth));
    latencyP95Label_->setText(displayLatencyCount_ > 0U
        ? QStringLiteral("%1 ms").arg(displayLatencyP95(), 0, 'f', 1)
        : QStringLiteral("--"));

    const auto latest = pipeline_.latest();
    if (latest) {
        fftSizeLabel_->setText(QString::number(latest->metadata.binCount));
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const auto nowNs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
        const double ageMs = nowNs >= latest->metadata.timestampNs
            ? static_cast<double>(nowNs - latest->metadata.timestampNs) / 1.0e6
            : 0.0;
        latencyLabel_->setText(QStringLiteral("%1 ms").arg(ageMs, 0, 'f', 1));
    } else {
        latencyLabel_->setText(QStringLiteral("--"));
        fftSizeLabel_->setText(fftSizeCombo_->currentText());
    }

    if (statusMetricsLabel_) {
        statusMetricsLabel_->setText(tr("显示: %1 | 渲染: %2 ms | 处理: %3 ms | P95延时: %4 | 队列: %5")
            .arg(displayRateLabel_->text())
            .arg(plot_->lastPaintMilliseconds(), 0, 'f', 1)
            .arg(pipelineStats.lastProcessingMilliseconds, 0, 'f', 2)
            .arg(latencyP95Label_->text())
            .arg(pipelineStats.queueDepth));
    }
}

void MainWindow::applySourceConfiguration()
{
    synchronizeStartStopFromCenterSpan();
    fullRangeCenterHz_ = centerFrequencySpin_->frequencyHz();
    fullRangeSpanHz_ = spanSpin_->frequencyHz();
    configureSourceFromUi();
}

void MainWindow::applyNonFrequencySourceConfiguration()
{
    if (simulationControl_
        && sweepStartFrequencySpin_->frequencyHz() >= sweepStopFrequencySpin_->frequencyHz()) {
        const QSignalBlocker blocker(sweepStopFrequencySpin_);
        sweepStopFrequencySpin_->setFrequencyHz(sweepStartFrequencySpin_->frequencyHz() + 1.0e3);
    }
    configureSourceFromUi();
}

void MainWindow::applyStartStopConfiguration()
{
    double startHz = startFrequencySpin_->frequencyHz();
    double stopHz = stopFrequencySpin_->frequencyHz();
    if (stopHz <= startHz) {
        stopHz = startHz + 1.0e3;
    }
    const double spanHz = std::clamp(stopHz - startHz,
                                      spanSpin_->minimumFrequencyHz(), spanSpin_->maximumFrequencyHz());
    const double centerHz = std::clamp((startHz + stopHz) * 0.5,
                                        centerFrequencySpin_->minimumFrequencyHz(),
                                        centerFrequencySpin_->maximumFrequencyHz());
    {
        const QSignalBlocker centerBlocker(centerFrequencySpin_);
        const QSignalBlocker spanBlocker(spanSpin_);
        centerFrequencySpin_->setFrequencyHz(centerHz);
        spanSpin_->setFrequencyHz(spanHz);
    }
    applySourceConfiguration();
}

void MainWindow::configureSourceFromUi()
{
    if (simulationControl_) {
        simulationControl_->configure(configurationFromUi());
    }
}

void MainWindow::applyTraceConfiguration()
{
    const auto mode = static_cast<TraceMode>(traceModeCombo_->currentData().toInt());
    pipeline_.setAverageCount(static_cast<std::size_t>(averageCountSpin_->value()));
    pipeline_.setTraceMode(mode);
    averageCountSpin_->setEnabled(mode == TraceMode::Average);
}

void MainWindow::applyAmplitudeScale()
{
    if (bottomLevelSpin_->value() >= referenceLevelSpin_->value()) {
        const QSignalBlocker blocker(bottomLevelSpin_);
        bottomLevelSpin_->setValue(referenceLevelSpin_->value() - 10.0);
    }
    {
        const QSignalBlocker blocker(verticalScaleSpin_);
        verticalScaleSpin_->setValue(
            (referenceLevelSpin_->value() - bottomLevelSpin_->value()) / 10.0);
    }
    plot_->setAmplitudeScale(static_cast<float>(referenceLevelSpin_->value()),
                             static_cast<float>(bottomLevelSpin_->value()));
    if (waterfallPlot_) {
        waterfallPlot_->setAmplitudeScale(static_cast<float>(referenceLevelSpin_->value()),
                                          static_cast<float>(bottomLevelSpin_->value()));
    }
}

void MainWindow::applyVerticalScale()
{
    const double requestedBottom = referenceLevelSpin_->value()
        - verticalScaleSpin_->value() * 10.0;
    {
        const QSignalBlocker blocker(bottomLevelSpin_);
        bottomLevelSpin_->setValue(requestedBottom);
    }
    applyAmplitudeScale();
}

void MainWindow::applyPlotAppearance()
{
    if (!plot_) {
        return;
    }
    const QColor traceColor = (plotColorPreset_ >= 0 && plotColorPreset_ < static_cast<int>(kClassicTraceRgb.size()))
        ? QColor(kClassicTraceRgb[plotColorPreset_])
        : (customTraceColor_.isValid() ? customTraceColor_ : QColor(0, 235, 180));

    plot_->setAppearance(traceColor,
                         plotLineWidth_,
                         plotGridVisible_,
                         plotTheme_,
                         customThemeColor_);
}

void MainWindow::setPlotTheme(const int themeIndex)
{
    plotTheme_ = themeIndex;
    if (themeActionGroup_) {
        for (auto* action : themeActionGroup_->actions()) {
            if (action->data().toInt() == themeIndex) {
                const QSignalBlocker blocker(themeActionGroup_);
                action->setChecked(true);
                break;
            }
        }
    }
    applyPlotAppearance();
}

void MainWindow::chooseCustomThemeColor()
{
    const QColor initial = customThemeColor_.isValid() ? customThemeColor_ : QColor(4, 9, 14);
    const QColor chosen = QColorDialog::getColor(initial, this, tr("选择绘图区自定义背景颜色"));
    if (chosen.isValid()) {
        customThemeColor_ = chosen;
        plotTheme_ = 4; // Custom
        if (customThemeAction_) {
            QPixmap pix(14, 14);
            pix.fill(chosen);
            customThemeAction_->setIcon(QIcon(pix));
            const QSignalBlocker blocker(themeActionGroup_);
            customThemeAction_->setChecked(true);
        }
        applyPlotAppearance();
    } else {
        setPlotTheme(plotTheme_);
    }
}

void MainWindow::setTraceColorPreset(const int presetIndex)
{
    plotColorPreset_ = presetIndex;
    if (traceColorActionGroup_) {
        for (auto* action : traceColorActionGroup_->actions()) {
            if (action->data().toInt() == presetIndex) {
                const QSignalBlocker blocker(traceColorActionGroup_);
                action->setChecked(true);
                break;
            }
        }
    }
    applyPlotAppearance();
}

void MainWindow::chooseCustomTraceColor()
{
    const QColor initial = customTraceColor_.isValid() ? customTraceColor_ : QColor(0, 235, 180);
    const QColor chosen = QColorDialog::getColor(initial, this, tr("选择自定义曲线颜色"));
    if (chosen.isValid()) {
        customTraceColor_ = chosen;
        plotColorPreset_ = 6; // Custom
        if (customTraceColorAction_) {
            QPixmap pix(14, 14);
            pix.fill(chosen);
            customTraceColorAction_->setIcon(QIcon(pix));
            const QSignalBlocker blocker(traceColorActionGroup_);
            customTraceColorAction_->setChecked(true);
        }
        applyPlotAppearance();
    } else {
        setTraceColorPreset(plotColorPreset_);
    }
}

void MainWindow::setTraceLineWidth(const int width)
{
    plotLineWidth_ = std::clamp(width, 1, 4);
    if (lineWidthActionGroup_) {
        for (auto* action : lineWidthActionGroup_->actions()) {
            if (action->data().toInt() == plotLineWidth_) {
                const QSignalBlocker blocker(lineWidthActionGroup_);
                action->setChecked(true);
                break;
            }
        }
    }
    applyPlotAppearance();
}

void MainWindow::setGridVisible(const bool visible)
{
    plotGridVisible_ = visible;
    if (gridAction_ && gridAction_->isChecked() != visible) {
        const QSignalBlocker blocker(gridAction_);
        gridAction_->setChecked(visible);
    }
    applyPlotAppearance();
}

void MainWindow::setDisplayViewMode(const int mode)
{
    if (displayViewModeCombo_) {
        const int index = displayViewModeCombo_->findData(mode);
        if (index >= 0 && displayViewModeCombo_->currentIndex() != index) {
            const QSignalBlocker blocker(displayViewModeCombo_);
            displayViewModeCombo_->setCurrentIndex(index);
        }
    }
    if (viewModeActionGroup_) {
        for (auto* action : viewModeActionGroup_->actions()) {
            if (action->data().toInt() == mode) {
                const QSignalBlocker blocker(viewModeActionGroup_);
                action->setChecked(true);
                break;
            }
        }
    }
    applyDisplayViewMode();
}

void MainWindow::setWaterfallColormap(const int colormap)
{
    if (waterfallColormapCombo_) {
        const int index = waterfallColormapCombo_->findData(colormap);
        if (index >= 0 && waterfallColormapCombo_->currentIndex() != index) {
            const QSignalBlocker blocker(waterfallColormapCombo_);
            waterfallColormapCombo_->setCurrentIndex(index);
        }
    }
    if (colormapActionGroup_) {
        for (auto* action : colormapActionGroup_->actions()) {
            if (action->data().toInt() == colormap) {
                const QSignalBlocker blocker(colormapActionGroup_);
                action->setChecked(true);
                break;
            }
        }
    }
    applyWaterfallSettings();
}

void MainWindow::applyDisplayViewMode()
{
    if (!plot_ || !waterfallPlot_ || !displayViewModeCombo_) {
        return;
    }
    const int mode = displayViewModeCombo_->currentData().toInt();
    switch (mode) {
    case 0:
        plot_->show();
        waterfallPlot_->hide();
        break;
    case 1:
        plot_->hide();
        waterfallPlot_->show();
        break;
    case 2:
    default:
        plot_->show();
        waterfallPlot_->show();
        break;
    }
}

void MainWindow::applyWaterfallSettings()
{
    if (!waterfallPlot_ || !waterfallColormapCombo_ || !waterfallHistorySpin_) {
        return;
    }
    waterfallPlot_->setColormap(static_cast<ColormapPreset>(
        waterfallColormapCombo_->currentData().toInt()));
    waterfallPlot_->setHistoryDepth(waterfallHistorySpin_->value());
}

void MainWindow::autoRangeAmplitude()
{
    const auto frame = pipeline_.latest();
    if (!frame || frame->bins.empty()) {
        return;
    }
    const auto extrema = std::minmax_element(frame->bins.cbegin(), frame->bins.cend());
    const double minimum = static_cast<double>(*extrema.first);
    const double maximum = static_cast<double>(*extrema.second);
    const double dataRange = std::max(20.0, maximum - minimum);
    const double margin = std::max(3.0, dataRange * 0.1);
    double reference = std::ceil((maximum + margin) / 10.0) * 10.0;
    double bottom = std::floor((minimum - margin) / 10.0) * 10.0;
    reference = std::clamp(reference,
                           referenceLevelSpin_->minimum(), referenceLevelSpin_->maximum());
    bottom = std::clamp(bottom,
                        bottomLevelSpin_->minimum(), bottomLevelSpin_->maximum());
    if (bottom >= reference) {
        bottom = std::max(bottomLevelSpin_->minimum(), reference - 10.0);
    }
    {
        const QSignalBlocker referenceBlocker(referenceLevelSpin_);
        const QSignalBlocker bottomBlocker(bottomLevelSpin_);
        referenceLevelSpin_->setValue(reference);
        bottomLevelSpin_->setValue(bottom);
    }
    applyAmplitudeScale();
}

void MainWindow::measureRangePeak()
{
    const auto frame = pipeline_.latest();
    if (!frame) {
        measurementResultLabel_->setText(tr("无有效频谱帧"));
        return;
    }
    const RangePeakMeasurement result = SpectrumMeasurements::peakInRange(
        *frame,
        measurementStartSpin_->frequencyHz(),
        measurementStopSpin_->frequencyHz());
    if (!result.valid) {
        measurementResultLabel_->setText(tr("区间无效或不含有效频点"));
        return;
    }
    measurementResultLabel_->setText(tr("峰值 %1 MHz, %2 %3%4")
        .arg(result.frequencyHz / 1.0e6, 0, 'f', 6)
        .arg(result.amplitude, 0, 'f', 2)
        .arg(QString::fromLatin1(amplitudeUnitSymbol(result.unit)))
        .arg(result.calibrated ? tr("（已校准）") : tr("（未校准）")));
}

void MainWindow::measureChannelPower()
{
    const auto frame = pipeline_.latest();
    if (!frame) {
        measurementResultLabel_->setText(tr("无有效频谱帧"));
        return;
    }
    const ChannelPowerMeasurement result = SpectrumMeasurements::channelPowerInRange(
        *frame,
        measurementStartSpin_->frequencyHz(),
        measurementStopSpin_->frequencyHz());
    if (!result.valid) {
        measurementResultLabel_->setText(tr("区间无效或功率数据不可积分"));
        return;
    }
    measurementResultLabel_->setText(tr("信道功率 %1 %2，%3 点%4")
        .arg(result.value, 0, 'f', 2)
        .arg(QString::fromLatin1(amplitudeUnitSymbol(result.unit)))
        .arg(result.integratedBins)
        .arg(result.calibrated ? tr("（已校准）") : tr("（未校准）")));
}

void MainWindow::synchronizeStartStopFromCenterSpan()
{
    const double centerHz = centerFrequencySpin_->frequencyHz();
    const double halfSpanHz = spanSpin_->frequencyHz() * 0.5;
    const QSignalBlocker startBlocker(startFrequencySpin_);
    const QSignalBlocker stopBlocker(stopFrequencySpin_);
    startFrequencySpin_->setFrequencyHz(centerHz - halfSpanHz, false);
    stopFrequencySpin_->setFrequencyHz(centerHz + halfSpanHz, false);
    if (measurementStartSpin_ && measurementStopSpin_) {
        const QSignalBlocker measurementStartBlocker(measurementStartSpin_);
        const QSignalBlocker measurementStopBlocker(measurementStopSpin_);
        measurementStartSpin_->setFrequencyHz(centerHz - halfSpanHz, false);
        measurementStopSpin_->setFrequencyHz(centerHz + halfSpanHz, false);
    }
}

void MainWindow::handleSourceState(const int stateValue)
{
    updateButtonStates(static_cast<SourceState>(stateValue));
}

void MainWindow::handleMarkerChanged(const double,
                                     const float,
                                     const QString&,
                                     const bool)
{
    refreshMarkerLabels();
}

void MainWindow::refreshMarkerLabels()
{
    if (!plot_ || !markerLabel_ || !deltaMarkerLabel_) {
        return;
    }
    const std::size_t active = plot_->activeMarker();
    const MarkerMeasurement marker = plot_->markerMeasurement(active);
    const auto frame = plot_->frame();
    const QString unit = frame
        ? QString::fromLatin1(amplitudeUnitSymbol(frame->metadata.unit))
        : QStringLiteral("dBFS");
    if (marker.valid) {
        markerLabel_->setText(tr("M%1  %2 MHz, %3 %4%5")
            .arg(active + 1U)
            .arg(marker.frequencyHz / 1.0e6, 0, 'f', 6)
            .arg(marker.amplitude, 0, 'f', 2)
            .arg(unit)
            .arg(frame && frame->metadata.calibrated ? QString() : tr("（未校准）")));
    } else {
        markerLabel_->setText(tr("M%1 未启用").arg(active + 1U));
    }

    if (!plot_->deltaMarkerEnabled()) {
        deltaMarkerLabel_->setText(tr("Delta 已关闭"));
        return;
    }
    const DeltaMarkerMeasurement delta = plot_->deltaMarkerMeasurement();
    if (!delta.valid) {
        deltaMarkerLabel_->setText(active == 0U
            ? tr("请选择 M2～M4 作为活动标记")
            : tr("需要同时设置 M1 和活动标记"));
        return;
    }
    deltaMarkerLabel_->setText(tr("M%1-M1  %2, %3 %4")
        .arg(active + 1U)
        .arg(formatFrequencyDelta(delta.frequencyDeltaHz))
        .arg(delta.amplitudeDelta, 0, 'f', 2)
        .arg(unit));
}

void MainWindow::handleSpanScaleRequested(const double scaleFactor,
                                          const double anchorFrequencyHz)
{
    const double oldCenterHz = centerFrequencySpin_->frequencyHz();
    const double oldSpanHz = spanSpin_->frequencyHz();
    const double newSpanHz = std::clamp(oldSpanHz * scaleFactor, 1.0e3, 10.0e9);
    const double newCenterHz = anchorFrequencyHz + (oldCenterHz - anchorFrequencyHz) * scaleFactor;

    {
        const QSignalBlocker centerBlocker(centerFrequencySpin_);
        const QSignalBlocker spanBlocker(spanSpin_);
        centerFrequencySpin_->setFrequencyHz(newCenterHz);
        spanSpin_->setFrequencyHz(newSpanHz);
    }
    synchronizeStartStopFromCenterSpan();
    configureSourceFromUi();
}

void MainWindow::handleFrequencyPanRequested(const double centerShiftHz)
{
    const QSignalBlocker blocker(centerFrequencySpin_);
    centerFrequencySpin_->setFrequencyHz(
        centerFrequencySpin_->frequencyHz() + centerShiftHz);
    synchronizeStartStopFromCenterSpan();
    configureSourceFromUi();
}

void MainWindow::handleFrequencyRangeSelected(const double startFrequencyHz,
                                              const double stopFrequencyHz)
{
    if (!std::isfinite(startFrequencyHz) || !std::isfinite(stopFrequencyHz)
        || stopFrequencyHz <= startFrequencyHz) {
        return;
    }
    const double spanHz = std::clamp(
        stopFrequencyHz - startFrequencyHz,
        spanSpin_->minimumFrequencyHz(), spanSpin_->maximumFrequencyHz());
    const double centerHz = std::clamp(
        (startFrequencyHz + stopFrequencyHz) * 0.5,
        centerFrequencySpin_->minimumFrequencyHz(), centerFrequencySpin_->maximumFrequencyHz());
    {
        const QSignalBlocker centerBlocker(centerFrequencySpin_);
        const QSignalBlocker spanBlocker(spanSpin_);
        centerFrequencySpin_->setFrequencyHz(centerHz);
        spanSpin_->setFrequencyHz(spanHz);
    }
    synchronizeStartStopFromCenterSpan();
    configureSourceFromUi();
}

void MainWindow::recordPaintedFrameLatency(const std::uint64_t publicationSequence,
                                           const std::uint64_t timestampNs)
{
    if (publicationSequence == 0U
        || publicationSequence == lastLatencyPublicationSequence_) {
        return;
    }
    lastLatencyPublicationSequence_ = publicationSequence;
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const auto nowNs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    const double latencyMs = nowNs >= timestampNs
        ? static_cast<double>(nowNs - timestampNs) / 1.0e6
        : 0.0;
    displayLatenciesMs_[displayLatencyWriteIndex_] = latencyMs;
    displayLatencyWriteIndex_ = (displayLatencyWriteIndex_ + 1U)
        % displayLatenciesMs_.size();
    displayLatencyCount_ = std::min(displayLatencyCount_ + 1U,
                                    displayLatenciesMs_.size());
}

void MainWindow::resetFrequencyRange()
{
    {
        const QSignalBlocker centerBlocker(centerFrequencySpin_);
        const QSignalBlocker spanBlocker(spanSpin_);
        centerFrequencySpin_->setFrequencyHz(fullRangeCenterHz_);
        spanSpin_->setFrequencyHz(fullRangeSpanHz_);
    }
    synchronizeStartStopFromCenterSpan();
    configureSourceFromUi();
}

void MainWindow::saveScreenshot()
{
    const QString suggested = QDir::home().filePath(
        QStringLiteral("rtsa-spectrum-%1.png")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    const QString filePath = QFileDialog::getSaveFileName(
        this, tr("保存频谱截图"), suggested, tr("PNG 图像 (*.png)"));
    if (filePath.isEmpty()) {
        return;
    }

    const bool saved = plot_->grab().save(filePath, "PNG");
    fileOperationLabel_->setText(saved ? tr("截图已保存") : tr("截图保存失败"));
    if (!saved) {
        QMessageBox::warning(this, tr("保存失败"), tr("无法将截图写入：%1").arg(filePath));
    }
}

void MainWindow::saveSimulationScenario()
{
    if (!simulationControl_) {
        return;
    }
    const QString suggested = QDir::home().filePath(
        QStringLiteral("rtsa-scenario-%1.json")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    QString filePath = QFileDialog::getSaveFileName(
        this, tr("保存模拟场景"), suggested, tr("JSON 场景 (*.json)"));
    if (filePath.isEmpty()) {
        return;
    }
    const QString scenarioName = QFileInfo(filePath).completeBaseName();
    const SimulationScenarioSaveResult result = SimulationScenarioWriter::saveFile(
        filePath, scenarioName, configurationFromUi());
    if (result.success) {
        fileOperationLabel_->setText(tr("模拟场景已保存"));
        statusBar()->showMessage(tr("场景已保存：%1").arg(filePath), 5000);
        return;
    }
    fileOperationLabel_->setText(tr("模拟场景保存失败"));
    QMessageBox::warning(this, tr("保存失败"), result.errorMessage);
}

void MainWindow::exportCsv()
{
    if (exportWatcher_->isRunning()) {
        return;
    }
    const auto frame = pipeline_.latest();
    if (!frame) {
        QMessageBox::information(this, tr("没有数据"), tr("请先启动采集并等待频谱数据。"));
        return;
    }

    const QString suggested = QDir::home().filePath(
        QStringLiteral("rtsa-spectrum-%1.csv")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    const QString filePath = QFileDialog::getSaveFileName(
        this, tr("导出当前频谱"), suggested, tr("CSV 数据 (*.csv)"));
    if (filePath.isEmpty()) {
        return;
    }

    exportCsvButton_->setEnabled(false);
    fileOperationLabel_->setText(tr("正在后台导出 %1 个频点…").arg(frame->bins.size()));
    exportWatcher_->setFuture(QtConcurrent::run([frame, filePath] {
        return SpectrumExporter::writeCsv(frame, filePath);
    }));
}

void MainWindow::handleExportFinished()
{
    exportCsvButton_->setEnabled(true);
    const ExportResult result = exportWatcher_->result();
    if (result.success) {
        fileOperationLabel_->setText(tr("已导出 %1 个频点").arg(result.exportedBins));
        statusBar()->showMessage(tr("CSV 已保存：%1").arg(result.filePath), 5000);
        return;
    }

    fileOperationLabel_->setText(tr("CSV 导出失败"));
    QMessageBox::warning(this, tr("导出失败"),
                         tr("无法导出频谱：%1").arg(result.errorMessage));
}

void MainWindow::showShortcutsDialog()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("快捷键说明"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr(
        "<h3>⌨️ RTSA 快捷键参考</h3>"
        "<table border='0' cellpadding='4' cellspacing='0' style='font-size: 12px; color: #cfd8dc;'>"
        "<tr><td><b>F5</b></td><td>开始 / 继续连续采集</td></tr>"
        "<tr><td><b>F6</b></td><td>暂停采集</td></tr>"
        "<tr><td><b>F7</b></td><td>单次扫描采集</td></tr>"
        "<tr><td><b>F8</b></td><td>停止采集</td></tr>"
        "<tr><td><b>F11</b></td><td>全屏模式切换 (ESC 退出)</td></tr>"
        "<tr><td><b>Ctrl + R</b></td><td>自动幅度刻度 (Auto Range)</td></tr>"
        "<tr><td><b>Ctrl + G</b></td><td>显示 / 隐藏网格 (Toggle Grid)</td></tr>"
        "<tr><td><b>Ctrl + 0</b></td><td>重置频率范围 (Reset Span)</td></tr>"
        "<tr><td><b>M</b></td><td>活动标记峰值搜索 (Peak Search)</td></tr>"
        "<tr><td><b>Ctrl + E</b></td><td>导出当前频谱为 CSV 数据</td></tr>"
        "<tr><td><b>Ctrl + S</b></td><td>保存当前频谱分析仪截图</td></tr>"
        "<tr><td><b>Alt + F4</b></td><td>退出软件</td></tr>"
        "</table>"
        "<hr>"
        "<h4>🖱️ 鼠标与手势交互</h4>"
        "<ul>"
        "<li><b>滚轮滚动</b>：以鼠标所在频点为中心缩放 Span</li>"
        "<li><b>左键拖拽</b>：平移中心频率 (Pan)</li>"
        "<li><b>左键框选</b>：局部放大选中频段与幅度矩形</li>"
        "<li><b>双击画布</b>：重置频率范围与幅度刻度</li>"
        "</ul>"
    ));
    box.setIcon(QMessageBox::Information);
    box.exec();
}

void MainWindow::showUserGuideDialog()
{
    QMessageBox box(this);
    box.setWindowTitle(tr("RTSA 实时频谱分析仪操作指南"));
    box.setTextFormat(Qt::RichText);
    box.setText(tr(
        "<h3>📖 RTSA 快速操作指南</h3>"
        "<ol style='font-size: 12px; line-height: 1.6; color: #cfd8dc;'>"
        "<li><b>数据源与频率设置</b>：<br>"
        "在【频率与幅度】选项卡中设置中心频率、Span 频宽、FFT 频点数与输入帧率。支持通过滚轮或键盘微调，未聚焦时滚轮不误触。</li>"
        "<li><b>幅度与坐标刻度</b>：<br>"
        "设置参考电平 (Ref Level)、底电平 (Bottom Level) 或直接使用 <code>Ctrl+R</code> 自动适配最佳动态范围。</li>"
        "<li><b>显示与视觉定制</b>：<br>"
        "通过顶部【显示】菜单，可实时切换经典深黑/深海科技/复古纯黑/明亮浅色主题或自定义绘图区背景色，并提供 6 种经典荧光曲线颜色、自定义色及 1~4 px 线宽与网格开关。</li>"
        "<li><b>时频瀑布图分析</b>：<br>"
        "在【迹线与瀑布】中可切换频谱、瀑布图或双屏联动。瀑布图严格按时间自顶向底连续流动，支持 Turbo、Viridis、Jet 等专业色谱。</li>"
        "<li><b>标记与差分测量</b>：<br>"
        "在【标记与测量】中支持 M1~M4 四组标记与 Delta 差分模式，按 <code>M</code> 键快速锁定最高峰值。</li>"
        "<li><b>信道功率与选段分析</b>：<br>"
        "设定测量起始与终止频点，点击“区间峰值”或“信道功率”实时计算选段总积分功率。</li>"
        "</ol>"
    ));
    box.setIcon(QMessageBox::Information);
    box.exec();
}

void MainWindow::showAboutDialog()
{
    QMessageBox aboutBox(this);
    aboutBox.setWindowTitle(tr("关于 RTSA 实时频谱分析系统"));
    aboutBox.setTextFormat(Qt::RichText);
    aboutBox.setText(tr(
        "<h3>RTSA 实时频谱分析仪 (Real-Time Spectrum Analyzer)</h3>"
        "<p><b>版本：</b>v2.0 Industrial Edition</p>"
        "<p><b>渲染引擎：</b>Qt 6 / 高性能 CPU Raster 零拷贝双缓冲</p>"
        "<p><b>时频架构：</b>无锁环形队列 (Lock-Free SPSC) + 连续瀑布图谱 (Spectrogram)</p>"
        "<p><b>工业标准：</b>对标 Keysight / Rohde & Schwarz / Tektronix 射频仪器规范</p>"
        "<hr>"
        "<p style='color: #888;'>Copyright &copy; 2026 RTSA Team. All rights reserved.</p>"
    ));
    aboutBox.setIcon(QMessageBox::Information);
    aboutBox.exec();
}

void MainWindow::showTelemetryDialog()
{
    if (telemetryDialog_) {
        telemetryDialog_->show();
        telemetryDialog_->raise();
        telemetryDialog_->activateWindow();
    }
}

void MainWindow::buildMenuBar()
{
    auto* bar = menuBar();
    bar->clear();

    // 1. File Menu
    auto* fileMenu = bar->addMenu(tr("文件 (&F)"));
    fileMenu->addAction(tr("导出频谱 CSV (&E)..."), QKeySequence(Qt::CTRL | Qt::Key_E), this, &MainWindow::exportCsv);
    fileMenu->addAction(tr("保存屏幕截图 (&S)..."), QKeySequence(Qt::CTRL | Qt::Key_S), this, &MainWindow::saveScreenshot);
    if (simulationControl_) {
        fileMenu->addAction(tr("保存模拟场景 (&C)..."), this, &MainWindow::saveSimulationScenario);
    }
    fileMenu->addSeparator();
    fileMenu->addAction(tr("退出 (&X)"), QKeySequence(Qt::ALT | Qt::Key_F4), this, &QWidget::close);

    // 2. Display Menu (显示菜单)
    auto* displayMenu = bar->addMenu(tr("显示 (&D)"));

    // 2.1 视图模式
    auto* viewModeMenu = displayMenu->addMenu(tr("视图模式 (&V)"));
    viewModeActionGroup_ = new QActionGroup(this);

    auto* dualViewAction = viewModeMenu->addAction(tr("双视图分屏 (Dual View)"), [this] {
        setDisplayViewMode(2);
    });
    dualViewAction->setCheckable(true);
    dualViewAction->setChecked(true);
    dualViewAction->setData(2);
    viewModeActionGroup_->addAction(dualViewAction);

    auto* spectrumOnlyAction = viewModeMenu->addAction(tr("仅频谱图 (Spectrum Only)"), [this] {
        setDisplayViewMode(0);
    });
    spectrumOnlyAction->setCheckable(true);
    spectrumOnlyAction->setData(0);
    viewModeActionGroup_->addAction(spectrumOnlyAction);

    auto* waterfallOnlyAction = viewModeMenu->addAction(tr("仅瀑布图 (Waterfall Only)"), [this] {
        setDisplayViewMode(1);
    });
    waterfallOnlyAction->setCheckable(true);
    waterfallOnlyAction->setData(1);
    viewModeActionGroup_->addAction(waterfallOnlyAction);

    displayMenu->addSeparator();

    // 2.2 绘图区主题 (Plot Area Theme)
    auto* themeMenu = displayMenu->addMenu(tr("绘图区主题 (&T)"));
    themeActionGroup_ = new QActionGroup(this);

    struct ThemeEntry {
        QString name;
        int index;
        QRgb rgb;
    };
    const std::array<ThemeEntry, 4> themeEntries {{
        { tr("经典深黑 (Classic Dark)"), 0, kClassicThemeRgb[0] },
        { tr("深海科技 (Deep Navy)"), 2, kClassicThemeRgb[2] },
        { tr("复古纯黑 (Pitch Black)"), 3, kClassicThemeRgb[3] },
        { tr("明亮浅色 (High Contrast Light)"), 1, kClassicThemeRgb[1] }
    }};

    for (const auto& entry : themeEntries) {
        QPixmap pix(14, 14);
        pix.fill(QColor(entry.rgb));
        auto* action = themeMenu->addAction(QIcon(pix), entry.name, [this, idx = entry.index] {
            setPlotTheme(idx);
        });
        action->setCheckable(true);
        action->setData(entry.index);
        if (entry.index == 0) action->setChecked(true);
        themeActionGroup_->addAction(action);
    }
    themeMenu->addSeparator();
    customThemeAction_ = themeMenu->addAction(tr("自定义背景颜色 (&B)..."), this, &MainWindow::chooseCustomThemeColor);
    customThemeAction_->setCheckable(true);
    customThemeAction_->setData(4);
    themeActionGroup_->addAction(customThemeAction_);

    // 2.3 曲线颜色 (Trace Color)
    auto* colorMenu = displayMenu->addMenu(tr("曲线颜色 (&C)"));
    traceColorActionGroup_ = new QActionGroup(this);

    struct ColorEntry {
        QString name;
        int index;
        QRgb rgb;
    };
    const std::array<ColorEntry, 6> colorEntries {{
        { tr("经典翠绿 (Emerald Green)"), 0, kClassicTraceRgb[0] },
        { tr("科技青蓝 (Cyan Blue)"), 1, kClassicTraceRgb[1] },
        { tr("琥珀明黄 (Amber Yellow)"), 2, kClassicTraceRgb[2] },
        { tr("纯净亮白 (Pure White)"), 3, kClassicTraceRgb[3] },
        { tr("工业烈橙 (Industrial Orange)"), 4, kClassicTraceRgb[4] },
        { tr("荧光洋红 (Neon Magenta)"), 5, kClassicTraceRgb[5] }
    }};

    for (const auto& entry : colorEntries) {
        QPixmap pix(14, 14);
        pix.fill(QColor(entry.rgb));
        auto* action = colorMenu->addAction(QIcon(pix), entry.name, [this, idx = entry.index] {
            setTraceColorPreset(idx);
        });
        action->setCheckable(true);
        action->setData(entry.index);
        if (entry.index == 0) action->setChecked(true);
        traceColorActionGroup_->addAction(action);
    }
    colorMenu->addSeparator();
    customTraceColorAction_ = colorMenu->addAction(tr("自定义曲线颜色 (&U)..."), this, &MainWindow::chooseCustomTraceColor);
    customTraceColorAction_->setCheckable(true);
    customTraceColorAction_->setData(6);
    traceColorActionGroup_->addAction(customTraceColorAction_);

    // 2.4 曲线线宽 (Trace Line Width)
    auto* widthMenu = displayMenu->addMenu(tr("曲线线宽 (&W)"));
    lineWidthActionGroup_ = new QActionGroup(this);
    const QString widthLabels[4] = {
        tr("1 px（精细）"),
        tr("2 px（标准）"),
        tr("3 px（加粗）"),
        tr("4 px（高亮）")
    };
    for (int w = 1; w <= 4; ++w) {
        auto* action = widthMenu->addAction(widthLabels[w - 1], [this, w] {
            setTraceLineWidth(w);
        });
        action->setCheckable(true);
        action->setData(w);
        if (w == 1) action->setChecked(true);
        lineWidthActionGroup_->addAction(action);
    }

    // 2.5 显示网格 (Grid)
    gridAction_ = displayMenu->addAction(tr("显示网格 (&G)"), [this](bool checked) {
        setGridVisible(checked);
    });
    gridAction_->setCheckable(true);
    gridAction_->setChecked(true);
    gridAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));

    displayMenu->addSeparator();

    // 2.6 瀑布图色图 (Waterfall Colormap)
    auto* colormapMenu = displayMenu->addMenu(tr("瀑布图色图 (&M)"));
    colormapActionGroup_ = new QActionGroup(this);
    const struct ColormapEntry {
        QString name;
        int mapIndex;
    } colormapEntries[] = {
        { tr("工业标准彩虹 (Classic Rainbow)"), static_cast<int>(ColormapPreset::ClassicRainbow) },
        { tr("高对比深海 (Rohde & Schwarz)"), static_cast<int>(ColormapPreset::RohdeSchwarz) },
        { tr("铁红热力 (Ironbow)"), static_cast<int>(ColormapPreset::Ironbow) },
        { tr("深海冰蓝 (Deep Ocean)"), static_cast<int>(ColormapPreset::DeepOcean) },
        { tr("单通道灰度 (Grayscale)"), static_cast<int>(ColormapPreset::Grayscale) }
    };
    for (const auto& entry : colormapEntries) {
        auto* action = colormapMenu->addAction(entry.name, [this, idx = entry.mapIndex] {
            setWaterfallColormap(idx);
        });
        action->setCheckable(true);
        action->setData(entry.mapIndex);
        if (entry.mapIndex == 0) action->setChecked(true);
        colormapActionGroup_->addAction(action);
    }

    displayMenu->addSeparator();

    // 2.7 自动量程与全屏
    displayMenu->addAction(tr("自动幅度刻度 (&Auto Range)"), QKeySequence(Qt::CTRL | Qt::Key_R), this, &MainWindow::autoRangeAmplitude);
    displayMenu->addAction(tr("全屏切换 (&Full Screen)"), QKeySequence(Qt::Key_F11), this, &MainWindow::toggleFullScreen);

    // 3. Help Menu
    auto* helpMenu = bar->addMenu(tr("帮助 (&H)"));
    helpMenu->addAction(tr("快捷键设置 (&S)..."), this, &MainWindow::showShortcutsDialog);
    helpMenu->addAction(tr("操作指南 (&G)..."), this, &MainWindow::showUserGuideDialog);
    helpMenu->addSeparator();
    helpMenu->addAction(tr("关于软件 (&A)..."), this, &MainWindow::showAboutDialog);
}

void MainWindow::buildStatusBar()
{
    auto* bar = statusBar();
    bar->setSizeGripEnabled(false);

    statusStateChip_ = new QLabel(this);
    statusStateChip_->setObjectName(QStringLiteral("statusStateChip"));
    statusStateChip_->setStyleSheet(
        QStringLiteral("QLabel { font-weight: bold; padding: 2px 8px; border-radius: 3px; background: #263238; color: #b0bec5; }"));
    statusStateChip_->setText(tr("【■ 已停止】"));
    bar->addWidget(statusStateChip_);

    statusMetricsLabel_ = new QLabel(this);
    statusMetricsLabel_->setObjectName(QStringLiteral("statusMetrics"));
    statusMetricsLabel_->setStyleSheet(QStringLiteral("QLabel { color: #90a4ae; font-family: monospace; font-size: 11px; }"));
    bar->addPermanentWidget(statusMetricsLabel_);
}

void MainWindow::buildUi()
{
    buildMenuBar();
    buildStatusBar();

    auto* central = new QWidget(this);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    plotSplitter_ = new QSplitter(Qt::Vertical, central);
    plotSplitter_->setObjectName(QStringLiteral("plotSplitter"));

    plot_ = new SpectrumPlotWidget(plotSplitter_);
    plot_->setObjectName(QStringLiteral("spectrumPlot"));
    plot_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    waterfallPlot_ = new WaterfallPlotWidget(plotSplitter_);
    waterfallPlot_->setObjectName(QStringLiteral("waterfallPlot"));
    waterfallPlot_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    plotSplitter_->addWidget(plot_);
    plotSplitter_->addWidget(waterfallPlot_);
    plotSplitter_->setStretchFactor(0, 1);
    plotSplitter_->setStretchFactor(1, 1);

    layout->addWidget(plotSplitter_, 1);
    layout->addWidget(buildControlPanel());
    setCentralWidget(central);
}

QWidget* MainWindow::buildControlPanel()
{
    auto* panel = new QWidget(this);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    // 1. Top Instrument Acquisition Control Bar (Styled exactly per screenshot)
    auto* acqBox = new QGroupBox(tr("采集控制"), panel);
    auto* acqLayout = new QGridLayout(acqBox);
    acqLayout->setContentsMargins(6, 8, 6, 6);
    acqLayout->setSpacing(6);

    startButton_ = new QPushButton(tr("▶  开始 / 连续"), acqBox);
    startButton_->setObjectName(QStringLiteral("startButton"));
    startButton_->setMinimumHeight(38);
    startButton_->setStyleSheet(QStringLiteral(
        "QPushButton { "
        "  font-size: 13px; font-weight: bold; color: #00e676; "
        "  background-color: #0d2315; border: 1.5px solid #00e676; "
        "  border-radius: 3px; padding: 6px 12px; "
        "} "
        "QPushButton:hover { background-color: #153822; border-color: #69f0ae; color: #69f0ae; } "
        "QPushButton:pressed { background-color: #1f4f30; } "
        "QPushButton:disabled { color: #5a7364; border-color: #2e4737; background-color: #121c15; }"));

    stopButton_ = new QPushButton(tr("■  停止"), acqBox);
    stopButton_->setObjectName(QStringLiteral("stopButton"));
    stopButton_->setMinimumHeight(32);
    stopButton_->setStyleSheet(QStringLiteral(
        "QPushButton { "
        "  font-size: 12px; font-weight: bold; color: #cfd8dc; "
        "  background-color: #1e2631; border: 1px solid #37474f; "
        "  border-radius: 3px; padding: 4px 8px; "
        "} "
        "QPushButton:hover { background-color: #2b3644; border-color: #546e7a; color: #eceff1; } "
        "QPushButton:pressed { background-color: #161c24; } "
        "QPushButton:disabled { color: #607d8b; border-color: #263238; background-color: #161c22; }"));

    singleButton_ = new QPushButton(tr("○  单次"), acqBox);
    singleButton_->setObjectName(QStringLiteral("singleButton"));
    singleButton_->setMinimumHeight(32);
    singleButton_->setStyleSheet(QStringLiteral(
        "QPushButton { "
        "  font-size: 12px; font-weight: bold; color: #cfd8dc; "
        "  background-color: #1e2631; border: 1px solid #37474f; "
        "  border-radius: 3px; padding: 4px 8px; "
        "} "
        "QPushButton:hover { background-color: #2b3644; border-color: #546e7a; color: #eceff1; } "
        "QPushButton:pressed { background-color: #161c24; } "
        "QPushButton:disabled { color: #607d8b; border-color: #263238; background-color: #161c22; }"));

    pauseButton_ = new QPushButton(tr("❚❚ 暂停"), acqBox);
    pauseButton_->setObjectName(QStringLiteral("pauseButton"));
    pauseButton_->setVisible(false);

    acqLayout->addWidget(startButton_, 0, 0, 1, 2);
    acqLayout->addWidget(stopButton_, 1, 0);
    acqLayout->addWidget(singleButton_, 1, 1);
    layout->addWidget(acqBox);

    // 2. Tab Widget with 4 Clean Functional Pages
    mainTabWidget_ = new QTabWidget(panel);
    mainTabWidget_->setObjectName(QStringLiteral("mainTabWidget"));

    // Tab 1: 频率与幅度 (Freq & Amplitude)
    auto* freqAmpPage = new QWidget;
    auto* freqAmpLayout = new QVBoxLayout(freqAmpPage);
    freqAmpLayout->setContentsMargins(2, 4, 2, 4);
    freqAmpLayout->setSpacing(6);
    freqAmpLayout->addWidget(buildSourceGroup());
    freqAmpLayout->addWidget(buildDisplayGroup());
    freqAmpLayout->addStretch(1);

    auto* freqAmpScroll = new QScrollArea;
    freqAmpScroll->setWidgetResizable(true);
    freqAmpScroll->setFrameShape(QFrame::NoFrame);
    freqAmpScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    freqAmpScroll->setWidget(freqAmpPage);
    mainTabWidget_->addTab(freqAmpScroll, tr("频率与幅度"));

    // Tab 2: 模拟信号源 (Simulated Source) - if supported
    if (simulationControl_) {
        auto* simPage = new QWidget;
        auto* simLayout = new QVBoxLayout(simPage);
        simLayout->setContentsMargins(2, 4, 2, 4);
        simLayout->setSpacing(6);
        simLayout->addWidget(buildSimulationGroup());
        simLayout->addStretch(1);

        auto* simScroll = new QScrollArea;
        simScroll->setWidgetResizable(true);
        simScroll->setFrameShape(QFrame::NoFrame);
        simScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        simScroll->setWidget(simPage);
        mainTabWidget_->addTab(simScroll, tr("模拟信号源"));
    }

    // Tab 3: 迹线与瀑布 (Trace & Waterfall)
    auto* traceViewPage = new QWidget;
    auto* traceViewLayout = new QVBoxLayout(traceViewPage);
    traceViewLayout->setContentsMargins(2, 4, 2, 4);
    traceViewLayout->setSpacing(6);
    traceViewLayout->addWidget(buildTraceGroup());
    traceViewLayout->addWidget(buildWaterfallGroup());
    traceViewLayout->addStretch(1);

    auto* traceViewScroll = new QScrollArea;
    traceViewScroll->setWidgetResizable(true);
    traceViewScroll->setFrameShape(QFrame::NoFrame);
    traceViewScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    traceViewScroll->setWidget(traceViewPage);
    mainTabWidget_->addTab(traceViewScroll, tr("迹线与瀑布"));

    // Tab 4: 标记与测量 (Markers & Measure)
    auto* markerMeasurePage = new QWidget;
    auto* markerMeasureLayout = new QVBoxLayout(markerMeasurePage);
    markerMeasureLayout->setContentsMargins(2, 4, 2, 4);
    markerMeasureLayout->setSpacing(6);
    markerMeasureLayout->addWidget(buildMarkerGroup());
    markerMeasureLayout->addWidget(buildMeasurementGroup());
    markerMeasureLayout->addStretch(1);

    auto* markerMeasureScroll = new QScrollArea;
    markerMeasureScroll->setWidgetResizable(true);
    markerMeasureScroll->setFrameShape(QFrame::NoFrame);
    markerMeasureScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    markerMeasureScroll->setWidget(markerMeasurePage);
    mainTabWidget_->addTab(markerMeasureScroll, tr("标记与测量"));

    layout->addWidget(mainTabWidget_, 1);

    // Hidden host for file operation widgets so actions & unit tests retain object access
    auto* fileHost = new QWidget(panel);
    fileHost->setVisible(false);
    auto* fileHostLayout = new QVBoxLayout(fileHost);
    fileHostLayout->addWidget(buildFileGroup());
    layout->addWidget(fileHost);

    // Pre-create telemetryDialog_ containing telemetry group so telemetry widgets always exist
    telemetryDialog_ = new QDialog(this);
    telemetryDialog_->setObjectName(QStringLiteral("telemetryDialog"));
    telemetryDialog_->setWindowTitle(tr("RTSA 引擎实时遥测监控"));
    telemetryDialog_->resize(420, 520);
    auto* dlgLayout = new QVBoxLayout(telemetryDialog_);
    dlgLayout->addWidget(buildTelemetryGroup());
    auto* closeBtn = new QPushButton(tr("关闭"), telemetryDialog_);
    connect(closeBtn, &QPushButton::clicked, telemetryDialog_, &QDialog::accept);
    dlgLayout->addWidget(closeBtn);

    panel->setFixedWidth(340);
    UnfocusedWheelFilter::installRecursively(panel);
    return panel;
}

QWidget* MainWindow::buildSourceGroup()
{
    auto* group = new QGroupBox(tr("数据源与频率"), this);
    auto* form = new QFormLayout(group);

    centerFrequencySpin_ = new FrequencySpinBox(group);
    centerFrequencySpin_->setObjectName(QStringLiteral("centerFrequencyMHz"));
    centerFrequencySpin_->setFrequencyRangeHz(0.0, 20.0e9);
    centerFrequencySpin_->setFrequencyHz(1000.0e6);

    spanSpin_ = new FrequencySpinBox(group);
    spanSpin_->setObjectName(QStringLiteral("spanMHz"));
    spanSpin_->setFrequencyRangeHz(1.0e3, 10.0e9);
    spanSpin_->setFrequencyHz(200.0e6);

    startFrequencySpin_ = new FrequencySpinBox(group);
    startFrequencySpin_->setObjectName(QStringLiteral("startFrequencyMHz"));
    startFrequencySpin_->setFrequencyRangeHz(-5.0e9, 25.0e9);
    startFrequencySpin_->setFrequencyHz(900.0e6);

    stopFrequencySpin_ = new FrequencySpinBox(group);
    stopFrequencySpin_->setObjectName(QStringLiteral("stopFrequencyMHz"));
    stopFrequencySpin_->setFrequencyRangeHz(-5.0e9, 25.0e9);
    stopFrequencySpin_->setFrequencyHz(1100.0e6);

    fftSizeCombo_ = new QComboBox(group);
    fftSizeCombo_->setObjectName(QStringLiteral("fftSize"));
    for (const int size : { 1024, 2048, 4096, 8192, 16384, 32768, 65536 }) {
        fftSizeCombo_->addItem(QString::number(size), size);
    }
    fftSizeCombo_->setCurrentText(QStringLiteral("16384"));

    sourceFrameRateSpin_ = new QDoubleSpinBox(group);
    sourceFrameRateSpin_->setObjectName(QStringLiteral("sourceFrameRate"));
    sourceFrameRateSpin_->setRange(1.0, 1000.0);
    sourceFrameRateSpin_->setDecimals(0);
    sourceFrameRateSpin_->setValue(200.0);
    sourceFrameRateSpin_->setSuffix(tr(" 帧/s"));

    noiseFloorSpin_ = new QDoubleSpinBox(group);
    noiseFloorSpin_->setObjectName(QStringLiteral("noiseFloorDbfs"));
    noiseFloorSpin_->setRange(-180.0, -10.0);
    noiseFloorSpin_->setValue(-110.0);
    noiseFloorSpin_->setSuffix(tr(" dBFS"));

    form->addRow(tr("中心频率"), centerFrequencySpin_->createCompoundWidget(group));
    form->addRow(tr("频宽（Span）"), spanSpin_->createCompoundWidget(group));
    form->addRow(tr("起始频率"), startFrequencySpin_->createCompoundWidget(group));
    form->addRow(tr("终止频率"), stopFrequencySpin_->createCompoundWidget(group));
    form->addRow(tr("频点数"), fftSizeCombo_);
    form->addRow(tr("输入帧率"), sourceFrameRateSpin_);
    form->addRow(tr("噪声底"), noiseFloorSpin_);

    return group;
}

QWidget* MainWindow::buildDisplayGroup()
{
    auto* group = new QGroupBox(tr("幅度与刻度"), this);
    auto* form = new QFormLayout(group);

    referenceLevelSpin_ = new QDoubleSpinBox(group);
    referenceLevelSpin_->setObjectName(QStringLiteral("referenceLevel"));
    referenceLevelSpin_->setRange(-100.0, 50.0);
    referenceLevelSpin_->setValue(0.0);
    referenceLevelSpin_->setSuffix(tr(" dBFS"));

    bottomLevelSpin_ = new QDoubleSpinBox(group);
    bottomLevelSpin_->setObjectName(QStringLiteral("bottomLevel"));
    bottomLevelSpin_->setRange(-200.0, 40.0);
    bottomLevelSpin_->setValue(-140.0);
    bottomLevelSpin_->setSuffix(tr(" dBFS"));

    verticalScaleSpin_ = new QDoubleSpinBox(group);
    verticalScaleSpin_->setObjectName(QStringLiteral("verticalScale"));
    verticalScaleSpin_->setRange(0.1, 25.0);
    verticalScaleSpin_->setDecimals(1);
    verticalScaleSpin_->setValue(14.0);
    verticalScaleSpin_->setSuffix(tr(" dB/格"));

    autoRangeButton_ = new QPushButton(tr("自动量程 (Ctrl+R)"), group);
    autoRangeButton_->setObjectName(QStringLiteral("autoRangeButton"));
    fullScreenButton_ = new QPushButton(tr("进入全屏 (F11)"), group);
    fullScreenButton_->setObjectName(QStringLiteral("fullScreenButton"));

    form->addRow(tr("参考电平"), referenceLevelSpin_);
    form->addRow(tr("底部电平"), bottomLevelSpin_);
    form->addRow(tr("垂直刻度"), verticalScaleSpin_);
    form->addRow(autoRangeButton_);
    form->addRow(fullScreenButton_);
    return group;
}

QWidget* MainWindow::buildWaterfallGroup()
{
    auto* group = new QGroupBox(tr("瀑布图"), this);
    auto* form = new QFormLayout(group);

    displayViewModeCombo_ = new QComboBox(group);
    displayViewModeCombo_->setObjectName(QStringLiteral("displayViewMode"));
    displayViewModeCombo_->addItem(tr("仅频谱图"), 0);
    displayViewModeCombo_->addItem(tr("仅瀑布图"), 1);
    displayViewModeCombo_->addItem(tr("频谱+瀑布图"), 2);
    displayViewModeCombo_->setCurrentIndex(2);

    waterfallColormapCombo_ = new QComboBox(group);
    waterfallColormapCombo_->setObjectName(QStringLiteral("waterfallColormap"));
    waterfallColormapCombo_->addItem(tr("工业标准彩虹 (Keysight/DPX)"), static_cast<int>(ColormapPreset::ClassicRainbow));
    waterfallColormapCombo_->addItem(tr("高对比深海 (R&S)"), static_cast<int>(ColormapPreset::RohdeSchwarz));
    waterfallColormapCombo_->addItem(tr("铁红热力 (Ironbow)"), static_cast<int>(ColormapPreset::Ironbow));
    waterfallColormapCombo_->addItem(tr("深海冰蓝 (Deep Ocean)"), static_cast<int>(ColormapPreset::DeepOcean));
    waterfallColormapCombo_->addItem(tr("单通道灰度 (Grayscale)"), static_cast<int>(ColormapPreset::Grayscale));

    waterfallHistorySpin_ = new QSpinBox(group);
    waterfallHistorySpin_->setObjectName(QStringLiteral("waterfallHistoryDepth"));
    waterfallHistorySpin_->setRange(64, 2048);
    waterfallHistorySpin_->setValue(512);
    waterfallHistorySpin_->setSuffix(tr(" 行"));

    waterfallClearButton_ = new QPushButton(tr("清空瀑布图"), group);
    waterfallClearButton_->setObjectName(QStringLiteral("waterfallClearButton"));

    form->addRow(tr("显示视图"), displayViewModeCombo_);
    form->addRow(tr("调色板"), waterfallColormapCombo_);
    form->addRow(tr("历史深度"), waterfallHistorySpin_);
    form->addRow(waterfallClearButton_);
    return group;
}

QWidget* MainWindow::buildSimulationGroup()
{
    auto* group = new QGroupBox(tr("模拟信号"), this);
    auto* form = new QFormLayout(group);

    noiseDeviationSpin_ = new QDoubleSpinBox(group);
    noiseDeviationSpin_->setObjectName(QStringLiteral("noiseDeviationDb"));
    noiseDeviationSpin_->setRange(0.0, 30.0);
    noiseDeviationSpin_->setDecimals(2);
    noiseDeviationSpin_->setValue(1.5);
    noiseDeviationSpin_->setSuffix(tr(" dB"));

    tone1EnabledCheck_ = new QCheckBox(tr("启用"), group);
    tone1EnabledCheck_->setObjectName(QStringLiteral("tone1Enabled"));
    tone1EnabledCheck_->setChecked(true);
    tone1FrequencySpin_ = new FrequencySpinBox(group);
    tone1FrequencySpin_->setObjectName(QStringLiteral("tone1FrequencyMHz"));
    tone1FrequencySpin_->setFrequencyRangeHz(0.0, 25.0e9);
    tone1FrequencySpin_->setFrequencyHz(980.0e6);
    tone1AmplitudeSpin_ = new QDoubleSpinBox(group);
    tone1AmplitudeSpin_->setObjectName(QStringLiteral("tone1AmplitudeDbfs"));
    tone1AmplitudeSpin_->setRange(-180.0, 0.0);
    tone1AmplitudeSpin_->setValue(-35.0);
    tone1AmplitudeSpin_->setSuffix(tr(" dBFS"));
    tone1WidthSpin_ = new FrequencySpinBox(group);
    tone1WidthSpin_->setObjectName(QStringLiteral("tone1WidthMHz"));
    tone1WidthSpin_->setFrequencyRangeHz(1.0, 10.0e9);
    tone1WidthSpin_->setFrequencyHz(1.5e6);

    tone2EnabledCheck_ = new QCheckBox(tr("启用"), group);
    tone2EnabledCheck_->setObjectName(QStringLiteral("tone2Enabled"));
    tone2EnabledCheck_->setChecked(true);
    tone2FrequencySpin_ = new FrequencySpinBox(group);
    tone2FrequencySpin_->setObjectName(QStringLiteral("tone2FrequencyMHz"));
    tone2FrequencySpin_->setFrequencyRangeHz(0.0, 25.0e9);
    tone2FrequencySpin_->setFrequencyHz(1035.0e6);
    tone2AmplitudeSpin_ = new QDoubleSpinBox(group);
    tone2AmplitudeSpin_->setObjectName(QStringLiteral("tone2AmplitudeDbfs"));
    tone2AmplitudeSpin_->setRange(-180.0, 0.0);
    tone2AmplitudeSpin_->setValue(-18.0);
    tone2AmplitudeSpin_->setSuffix(tr(" dBFS"));
    tone2WidthSpin_ = new FrequencySpinBox(group);
    tone2WidthSpin_->setObjectName(QStringLiteral("tone2WidthMHz"));
    tone2WidthSpin_->setFrequencyRangeHz(1.0, 10.0e9);
    tone2WidthSpin_->setFrequencyHz(2.5e6);

    sweepEnabledCheck_ = new QCheckBox(tr("启用"), group);
    sweepEnabledCheck_->setObjectName(QStringLiteral("sweepEnabled"));
    sweepEnabledCheck_->setChecked(true);
    sweepDirectionCombo_ = new QComboBox(group);
    sweepDirectionCombo_->setObjectName(QStringLiteral("sweepDirection"));
    sweepDirectionCombo_->addItem(tr("向上"), static_cast<int>(SweepDirection::Up));
    sweepDirectionCombo_->addItem(tr("向下"), static_cast<int>(SweepDirection::Down));
    sweepDirectionCombo_->addItem(tr("往返"), static_cast<int>(SweepDirection::PingPong));
    sweepStartFrequencySpin_ = new FrequencySpinBox(group);
    sweepStartFrequencySpin_->setObjectName(QStringLiteral("sweepStartFrequencyMHz"));
    sweepStartFrequencySpin_->setFrequencyRangeHz(0.0, 24.999e9);
    sweepStartFrequencySpin_->setFrequencyHz(930.0e6);
    sweepStopFrequencySpin_ = new FrequencySpinBox(group);
    sweepStopFrequencySpin_->setObjectName(QStringLiteral("sweepStopFrequencyMHz"));
    sweepStopFrequencySpin_->setFrequencyRangeHz(1.0e3, 25.0e9);
    sweepStopFrequencySpin_->setFrequencyHz(1070.0e6);
    sweepPeriodSpin_ = new QDoubleSpinBox(group);
    sweepPeriodSpin_->setObjectName(QStringLiteral("sweepPeriodSeconds"));
    sweepPeriodSpin_->setRange(0.1, 3600.0);
    sweepPeriodSpin_->setDecimals(2);
    sweepPeriodSpin_->setValue(4.0);
    sweepPeriodSpin_->setSuffix(tr(" s"));
    sweepAmplitudeSpin_ = new QDoubleSpinBox(group);
    sweepAmplitudeSpin_->setObjectName(QStringLiteral("sweepAmplitudeDbfs"));
    sweepAmplitudeSpin_->setRange(-180.0, 0.0);
    sweepAmplitudeSpin_->setValue(-45.0);
    sweepAmplitudeSpin_->setSuffix(tr(" dBFS"));

    transientProbabilitySpin_ = new QDoubleSpinBox(group);
    transientProbabilitySpin_->setObjectName(QStringLiteral("transientProbabilityPercent"));
    transientProbabilitySpin_->setRange(0.0, 100.0);
    transientProbabilitySpin_->setDecimals(3);
    transientProbabilitySpin_->setValue(0.2);
    transientProbabilitySpin_->setSuffix(tr(" %/帧"));
    transientAmplitudeSpin_ = new QDoubleSpinBox(group);
    transientAmplitudeSpin_->setObjectName(QStringLiteral("transientAmplitudeDbfs"));
    transientAmplitudeSpin_->setRange(-180.0, 0.0);
    transientAmplitudeSpin_->setValue(-12.0);
    transientAmplitudeSpin_->setSuffix(tr(" dBFS"));
    transientDurationSpin_ = new QDoubleSpinBox(group);
    transientDurationSpin_->setObjectName(QStringLiteral("transientDurationSeconds"));
    transientDurationSpin_->setRange(0.001, 60.0);
    transientDurationSpin_->setDecimals(3);
    transientDurationSpin_->setValue(0.1);
    transientDurationSpin_->setSuffix(tr(" s"));

    unthrottledCheck_ = new QCheckBox(tr("启用"), group);
    unthrottledCheck_->setObjectName(QStringLiteral("unthrottled"));

    form->addRow(tr("无等待压测"), unthrottledCheck_);
    form->addRow(tr("噪声起伏"), noiseDeviationSpin_);
    form->addRow(tr("单音 1"), tone1EnabledCheck_);
    form->addRow(tr("单音 1 频率"), tone1FrequencySpin_->createCompoundWidget(group));
    form->addRow(tr("单音 1 幅度"), tone1AmplitudeSpin_);
    form->addRow(tr("单音 1 带宽"), tone1WidthSpin_->createCompoundWidget(group));
    form->addRow(tr("单音 2"), tone2EnabledCheck_);
    form->addRow(tr("单音 2 频率"), tone2FrequencySpin_->createCompoundWidget(group));
    form->addRow(tr("单音 2 幅度"), tone2AmplitudeSpin_);
    form->addRow(tr("单音 2 带宽"), tone2WidthSpin_->createCompoundWidget(group));
    form->addRow(tr("扫频"), sweepEnabledCheck_);
    form->addRow(tr("扫频方向"), sweepDirectionCombo_);
    form->addRow(tr("扫频起点"), sweepStartFrequencySpin_->createCompoundWidget(group));
    form->addRow(tr("扫频终点"), sweepStopFrequencySpin_->createCompoundWidget(group));
    form->addRow(tr("扫频周期"), sweepPeriodSpin_);
    form->addRow(tr("扫频幅度"), sweepAmplitudeSpin_);
    form->addRow(tr("瞬态概率"), transientProbabilitySpin_);
    form->addRow(tr("瞬态幅度"), transientAmplitudeSpin_);
    form->addRow(tr("瞬态持续"), transientDurationSpin_);
    return group;
}

QWidget* MainWindow::buildTraceGroup()
{
    auto* group = new QGroupBox(tr("轨迹"), this);
    auto* form = new QFormLayout(group);

    traceModeCombo_ = new QComboBox(group);
    traceModeCombo_->addItem(tr("实时刷新"), static_cast<int>(TraceMode::ClearWrite));
    traceModeCombo_->addItem(tr("平均"), static_cast<int>(TraceMode::Average));
    traceModeCombo_->addItem(tr("最大保持"), static_cast<int>(TraceMode::MaxHold));
    traceModeCombo_->addItem(tr("最小保持"), static_cast<int>(TraceMode::MinHold));

    averageCountSpin_ = new QSpinBox(group);
    averageCountSpin_->setRange(1, 1024);
    averageCountSpin_->setValue(16);
    averageCountSpin_->setEnabled(false);

    resetTraceButton_ = new QPushButton(tr("清除历史轨迹"), group);
    form->addRow(tr("模式"), traceModeCombo_);
    form->addRow(tr("平均次数"), averageCountSpin_);
    form->addRow(resetTraceButton_);
    return group;
}

QWidget* MainWindow::buildMarkerGroup()
{
    auto* group = new QGroupBox(tr("标记（Marker）"), this);
    auto* form = new QFormLayout(group);

    activeMarkerCombo_ = new QComboBox(group);
    activeMarkerCombo_->setObjectName(QStringLiteral("activeMarker"));
    for (std::size_t index = 0; index < kSpectrumMarkerCount; ++index) {
        activeMarkerCombo_->addItem(QStringLiteral("M%1").arg(index + 1U),
                                    static_cast<int>(index));
    }
    peakThresholdSpin_ = new QDoubleSpinBox(group);
    peakThresholdSpin_->setObjectName(QStringLiteral("peakThreshold"));
    peakThresholdSpin_->setRange(-180.0, 50.0);
    peakThresholdSpin_->setDecimals(1);
    peakThresholdSpin_->setValue(-100.0);
    peakThresholdSpin_->setSuffix(tr(" dBFS"));
    deltaMarkerCheck_ = new QCheckBox(tr("相对 M1"), group);
    deltaMarkerCheck_->setObjectName(QStringLiteral("deltaMarker"));

    markerLabel_ = new QLabel(tr("M1 未启用"), group);
    markerLabel_->setWordWrap(true);
    deltaMarkerLabel_ = new QLabel(tr("Delta 已关闭"), group);
    deltaMarkerLabel_->setWordWrap(true);
    peakButton_ = new QPushButton(tr("搜索峰值"), group);
    nextPeakButton_ = new QPushButton(tr("下一个峰值"), group);
    previousPeakButton_ = new QPushButton(tr("上一个峰值"), group);
    clearMarkerButton_ = new QPushButton(tr("清除当前"), group);
    clearAllMarkersButton_ = new QPushButton(tr("清除全部"), group);

    auto* peakButtons = new QWidget(group);
    auto* peakLayout = new QHBoxLayout(peakButtons);
    peakLayout->setContentsMargins(0, 0, 0, 0);
    peakLayout->addWidget(previousPeakButton_);
    peakLayout->addWidget(peakButton_);
    peakLayout->addWidget(nextPeakButton_);
    auto* clearButtons = new QWidget(group);
    auto* clearLayout = new QHBoxLayout(clearButtons);
    clearLayout->setContentsMargins(0, 0, 0, 0);
    clearLayout->addWidget(clearMarkerButton_);
    clearLayout->addWidget(clearAllMarkersButton_);

    form->addRow(tr("活动标记"), activeMarkerCombo_);
    form->addRow(tr("峰值门限"), peakThresholdSpin_);
    form->addRow(tr("Delta"), deltaMarkerCheck_);
    form->addRow(tr("当前读数"), markerLabel_);
    form->addRow(tr("相对读数"), deltaMarkerLabel_);
    form->addRow(peakButtons);
    form->addRow(clearButtons);
    return group;
}

QWidget* MainWindow::buildMeasurementGroup()
{
    auto* group = new QGroupBox(tr("区间测量"), this);
    auto* form = new QFormLayout(group);
    measurementStartSpin_ = new FrequencySpinBox(group);
    measurementStartSpin_->setObjectName(QStringLiteral("measurementStartMHz"));
    measurementStartSpin_->setFrequencyRangeHz(-5.0e9, 25.0e9);
    measurementStartSpin_->setFrequencyHz(900.0e6);

    measurementStopSpin_ = new FrequencySpinBox(group);
    measurementStopSpin_->setObjectName(QStringLiteral("measurementStopMHz"));
    measurementStopSpin_->setFrequencyRangeHz(-5.0e9, 25.0e9);
    measurementStopSpin_->setFrequencyHz(1100.0e6);

    rangePeakButton_ = new QPushButton(tr("区间峰值"), group);
    rangePeakButton_->setObjectName(QStringLiteral("rangePeakButton"));
    channelPowerButton_ = new QPushButton(tr("信道功率"), group);
    channelPowerButton_->setObjectName(QStringLiteral("channelPowerButton"));
    measurementResultLabel_ = new QLabel(tr("尚未测量"), group);
    measurementResultLabel_->setObjectName(QStringLiteral("measurementResult"));
    measurementResultLabel_->setWordWrap(true);
    auto* buttons = new QWidget(group);
    auto* layout = new QHBoxLayout(buttons);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(rangePeakButton_);
    layout->addWidget(channelPowerButton_);
    form->addRow(tr("起始频率"), measurementStartSpin_->createCompoundWidget(group));
    form->addRow(tr("终止频率"), measurementStopSpin_->createCompoundWidget(group));
    form->addRow(buttons);
    form->addRow(tr("结果"), measurementResultLabel_);
    return group;
}

QWidget* MainWindow::buildFileGroup()
{
    auto* group = new QGroupBox(tr("文件"), this);
    auto* layout = new QGridLayout(group);
    screenshotButton_ = new QPushButton(tr("保存 PNG 截图"), group);
    exportCsvButton_ = new QPushButton(tr("导出当前 CSV"), group);
    fileOperationLabel_ = new QLabel(tr("就绪"), group);
    fileOperationLabel_->setWordWrap(true);
    layout->addWidget(screenshotButton_, 0, 0);
    layout->addWidget(exportCsvButton_, 0, 1);
    int statusRow = 1;
    if (simulationControl_) {
        saveScenarioButton_ = new QPushButton(tr("保存模拟场景"), group);
        saveScenarioButton_->setObjectName(QStringLiteral("saveScenarioButton"));
        layout->addWidget(saveScenarioButton_, 1, 0, 1, 2);
        statusRow = 2;
    }
    layout->addWidget(fileOperationLabel_, statusRow, 0, 1, 2);
    return group;
}

QWidget* MainWindow::buildTelemetryGroup()
{
    auto* group = new QGroupBox(tr("运行状态"), this);
    auto* form = new QFormLayout(group);
    sourceStateLabel_ = new QLabel(tr("已停止"), group);
    inputRateLabel_ = new QLabel(QStringLiteral("0 FPS"), group);
    displayRateLabel_ = new QLabel(QStringLiteral("0 FPS"), group);
    dataRateLabel_ = new QLabel(QStringLiteral("0 B/s"), group);
    sourceDropLabel_ = new QLabel(QStringLiteral("0"), group);
    invalidFrameLabel_ = new QLabel(QStringLiteral("0"), group);
    processingDropLabel_ = new QLabel(QStringLiteral("0"), group);
    publishedFrameLabel_ = new QLabel(QStringLiteral("0 / 0"), group);
    displaySkippedLabel_ = new QLabel(QStringLiteral("0 / 0"), group);
    displaySkippedLabel_->setObjectName(QStringLiteral("displaySkipped"));
    uptimeLabel_ = new QLabel(QStringLiteral("0.0 s"), group);
    fftSizeLabel_ = new QLabel(QStringLiteral("--"), group);
    latencyLabel_ = new QLabel(QStringLiteral("--"), group);
    latencyP95Label_ = new QLabel(QStringLiteral("--"), group);
    latencyP95Label_->setObjectName(QStringLiteral("latencyP95"));
    processingTimeLabel_ = new QLabel(QStringLiteral("0.000 ms"), group);
    processingTimeLabel_->setObjectName(QStringLiteral("processingTime"));
    renderTimeLabel_ = new QLabel(QStringLiteral("0.000 ms"), group);
    renderTimeLabel_->setObjectName(QStringLiteral("renderTime"));
    queueDepthLabel_ = new QLabel(QStringLiteral("0"), group);
    queueDepthLabel_->setObjectName(QStringLiteral("queueDepth"));
    lastErrorLabel_ = new QLabel(tr("无"), group);
    lastErrorLabel_->setWordWrap(true);

    form->addRow(tr("状态"), sourceStateLabel_);
    form->addRow(tr("输入速率"), inputRateLabel_);
    form->addRow(tr("显示速率"), displayRateLabel_);
    form->addRow(tr("输入数据率"), dataRateLabel_);
    form->addRow(tr("源/传输丢帧"), sourceDropLabel_);
    form->addRow(tr("无效帧"), invalidFrameLabel_);
    form->addRow(tr("处理丢帧"), processingDropLabel_);
    form->addRow(tr("发布/提交"), publishedFrameLabel_);
    form->addRow(tr("显示跳过/已显示"), displaySkippedLabel_);
    form->addRow(tr("运行时间"), uptimeLabel_);
    form->addRow(tr("FFT 点数"), fftSizeLabel_);
    form->addRow(tr("帧龄"), latencyLabel_);
    form->addRow(tr("显示延迟 P95"), latencyP95Label_);
    form->addRow(tr("处理耗时"), processingTimeLabel_);
    form->addRow(tr("绘制耗时"), renderTimeLabel_);
    form->addRow(tr("处理队列深度"), queueDepthLabel_);
    form->addRow(tr("最近错误"), lastErrorLabel_);
    return group;
}

void MainWindow::connectUi()
{
    connect(startButton_, &QPushButton::clicked, this, &MainWindow::startAcquisition);
    connect(pauseButton_, &QPushButton::clicked, this, &MainWindow::pauseAcquisition);
    connect(stopButton_, &QPushButton::clicked, this, &MainWindow::stopAcquisition);
    connect(singleButton_, &QPushButton::clicked, this, &MainWindow::singleAcquisition);
    connect(fullScreenButton_, &QPushButton::clicked, this, &MainWindow::toggleFullScreen);
    connect(autoRangeButton_, &QPushButton::clicked, this, &MainWindow::autoRangeAmplitude);
    connect(screenshotButton_, &QPushButton::clicked, this, &MainWindow::saveScreenshot);
    connect(exportCsvButton_, &QPushButton::clicked, this, &MainWindow::exportCsv);
    if (saveScenarioButton_) {
        connect(saveScenarioButton_, &QPushButton::clicked,
                this, &MainWindow::saveSimulationScenario);
    }
    connect(peakButton_, &QPushButton::clicked, plot_, &SpectrumPlotWidget::peakSearch);
    connect(nextPeakButton_, &QPushButton::clicked,
            plot_, &SpectrumPlotWidget::nextPeak);
    connect(previousPeakButton_, &QPushButton::clicked,
            plot_, &SpectrumPlotWidget::previousPeak);
    connect(clearMarkerButton_, &QPushButton::clicked, plot_, &SpectrumPlotWidget::clearMarker);
    connect(clearAllMarkersButton_, &QPushButton::clicked,
            plot_, &SpectrumPlotWidget::clearAllMarkers);
    connect(activeMarkerCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](const int index) {
        plot_->setActiveMarker(static_cast<std::size_t>(std::max(index, 0)));
        refreshMarkerLabels();
    });
    connect(peakThresholdSpin_, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this](const double threshold) {
        plot_->setPeakThreshold(static_cast<float>(threshold));
    });
    connect(deltaMarkerCheck_, &QCheckBox::toggled, this, [this](const bool enabled) {
        plot_->setDeltaMarkerEnabled(enabled);
        refreshMarkerLabels();
    });
    connect(rangePeakButton_, &QPushButton::clicked,
            this, &MainWindow::measureRangePeak);
    connect(channelPowerButton_, &QPushButton::clicked,
            this, &MainWindow::measureChannelPower);
    const auto normalizeMeasurementRange = [this] {
        if (measurementStartSpin_->value() >= measurementStopSpin_->value()) {
            const QSignalBlocker blocker(measurementStopSpin_);
            measurementStopSpin_->setValue(measurementStartSpin_->value() + 0.001);
        }
    };
    connect(measurementStartSpin_, &QDoubleSpinBox::editingFinished,
            this, normalizeMeasurementRange);
    connect(measurementStopSpin_, &QDoubleSpinBox::editingFinished,
            this, normalizeMeasurementRange);
    connect(plot_, &SpectrumPlotWidget::markerCleared, this, [this] {
        refreshMarkerLabels();
    });
    connect(resetTraceButton_, &QPushButton::clicked, this, [this] {
        pipeline_.resetTrace();
    });

    connect(centerFrequencySpin_, &QDoubleSpinBox::editingFinished,
            this, &MainWindow::applySourceConfiguration);
    connect(spanSpin_, &QDoubleSpinBox::editingFinished,
            this, &MainWindow::applySourceConfiguration);
    connect(startFrequencySpin_, &QDoubleSpinBox::editingFinished,
            this, &MainWindow::applyStartStopConfiguration);
    connect(stopFrequencySpin_, &QDoubleSpinBox::editingFinished,
            this, &MainWindow::applyStartStopConfiguration);
    connect(sourceFrameRateSpin_, &QDoubleSpinBox::editingFinished,
            this, &MainWindow::applyNonFrequencySourceConfiguration);
    connect(noiseFloorSpin_, &QDoubleSpinBox::editingFinished,
            this, &MainWindow::applyNonFrequencySourceConfiguration);
    if (simulationControl_) {
        for (QDoubleSpinBox* spin : std::initializer_list<QDoubleSpinBox*>{
                 noiseDeviationSpin_, tone1FrequencySpin_,
                 tone1AmplitudeSpin_, tone1WidthSpin_,
                 tone2FrequencySpin_, tone2AmplitudeSpin_,
                 tone2WidthSpin_, sweepStartFrequencySpin_,
                 sweepStopFrequencySpin_, sweepPeriodSpin_,
                 sweepAmplitudeSpin_, transientProbabilitySpin_,
                 transientAmplitudeSpin_, transientDurationSpin_ }) {
            connect(spin, &QDoubleSpinBox::editingFinished,
                    this, &MainWindow::applyNonFrequencySourceConfiguration);
        }
        connect(tone1EnabledCheck_, &QCheckBox::toggled,
                this, &MainWindow::applyNonFrequencySourceConfiguration);
        connect(tone2EnabledCheck_, &QCheckBox::toggled,
                this, &MainWindow::applyNonFrequencySourceConfiguration);
        connect(sweepEnabledCheck_, &QCheckBox::toggled,
                this, &MainWindow::applyNonFrequencySourceConfiguration);
        connect(sweepDirectionCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &MainWindow::applyNonFrequencySourceConfiguration);
        connect(unthrottledCheck_, &QCheckBox::toggled, this, [this](const bool enabled) {
            sourceFrameRateSpin_->setEnabled(!enabled);
            applyNonFrequencySourceConfiguration();
        });
    }
    connect(fftSizeCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::applyNonFrequencySourceConfiguration);
    connect(traceModeCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::applyTraceConfiguration);
    connect(averageCountSpin_, qOverload<int>(&QSpinBox::valueChanged),
            this, &MainWindow::applyTraceConfiguration);
    connect(referenceLevelSpin_, &QDoubleSpinBox::editingFinished,
            this, &MainWindow::applyAmplitudeScale);
    connect(bottomLevelSpin_, &QDoubleSpinBox::editingFinished,
            this, &MainWindow::applyAmplitudeScale);
    connect(verticalScaleSpin_, &QDoubleSpinBox::editingFinished,
            this, &MainWindow::applyVerticalScale);

    connect(source_.get(), &ISpectrumSource::stateChanged,
            this, &MainWindow::handleSourceState);
    connect(source_.get(), &ISpectrumSource::errorOccurred, this, [this](const QString& message) {
        lastSourceError_ = message;
        lastSourceErrorTime_ = QDateTime::currentDateTime().toString(
            QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        lastErrorLabel_->setText(tr("%1：%2").arg(lastSourceErrorTime_, lastSourceError_));
        QMessageBox::critical(this, tr("数据源错误"), message);
    });
    connect(plot_, &SpectrumPlotWidget::markerChanged,
            this, &MainWindow::handleMarkerChanged);
    connect(plot_, &SpectrumPlotWidget::spanScaleRequested,
            this, &MainWindow::handleSpanScaleRequested);
    connect(plot_, &SpectrumPlotWidget::frequencyPanRequested,
            this, &MainWindow::handleFrequencyPanRequested);
    connect(plot_, &SpectrumPlotWidget::frequencyRangeSelected,
            this, &MainWindow::handleFrequencyRangeSelected);
    connect(plot_, &SpectrumPlotWidget::frequencyRangeResetRequested,
            this, &MainWindow::resetFrequencyRange);
    connect(plot_, &SpectrumPlotWidget::framePainted,
            this, &MainWindow::recordPaintedFrameLatency);

    connect(displayViewModeCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](const int index) {
        setDisplayViewMode(displayViewModeCombo_->itemData(index).toInt());
    });
    connect(waterfallColormapCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](const int index) {
        setWaterfallColormap(waterfallColormapCombo_->itemData(index).toInt());
    });
    connect(waterfallHistorySpin_, qOverload<int>(&QSpinBox::valueChanged),
            this, &MainWindow::applyWaterfallSettings);
    connect(waterfallClearButton_, &QPushButton::clicked,
            waterfallPlot_, &WaterfallPlotWidget::clear);

    connect(waterfallPlot_, &WaterfallPlotWidget::spanScaleRequested,
            this, &MainWindow::handleSpanScaleRequested);
    connect(waterfallPlot_, &WaterfallPlotWidget::frequencyPanRequested,
            this, &MainWindow::handleFrequencyPanRequested);
    connect(waterfallPlot_, &WaterfallPlotWidget::frequencyRangeSelected,
            this, &MainWindow::handleFrequencyRangeSelected);
    connect(waterfallPlot_, &WaterfallPlotWidget::frequencyRangeResetRequested,
            this, &MainWindow::resetFrequencyRange);

    auto* escapeShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escapeShortcut, &QShortcut::activated, this, [this] {
        if (isFullScreen()) {
            toggleFullScreen();
        }
    });
}

void MainWindow::loadSettings()
{
    if (!settingsEnabled_) {
        return;
    }

    QSettings storage;
    const SettingsLoadResult result = ConfigurationStore::load(storage);
    const AppSettings& settings = result.settings;
    if (!result.warning.isEmpty()) {
        qWarning().noquote() << result.warning;
    }
    if (!settings.windowGeometry.isEmpty() && !restoreGeometry(settings.windowGeometry)) {
        qWarning() << "Ignoring invalid saved window geometry.";
    }

    centerFrequencySpin_->setFrequencyHz(settings.centerFrequencyMHz * 1.0e6);
    spanSpin_->setFrequencyHz(settings.spanMHz * 1.0e6);
    synchronizeStartStopFromCenterSpan();
    sourceFrameRateSpin_->setValue(settings.sourceFrameRate);
    noiseFloorSpin_->setValue(settings.noiseFloorDbfs);
    if (simulationControl_) {
        unthrottledCheck_->setChecked(settings.unthrottled);
        sourceFrameRateSpin_->setEnabled(!settings.unthrottled);
        noiseDeviationSpin_->setValue(settings.noiseDeviationDb);
        tone1EnabledCheck_->setChecked(settings.tone1Enabled);
        tone1FrequencySpin_->setFrequencyHz(settings.tone1FrequencyMHz * 1.0e6);
        tone1AmplitudeSpin_->setValue(settings.tone1AmplitudeDbfs);
        tone1WidthSpin_->setFrequencyHz(settings.tone1WidthMHz * 1.0e6);
        tone2EnabledCheck_->setChecked(settings.tone2Enabled);
        tone2FrequencySpin_->setFrequencyHz(settings.tone2FrequencyMHz * 1.0e6);
        tone2AmplitudeSpin_->setValue(settings.tone2AmplitudeDbfs);
        tone2WidthSpin_->setFrequencyHz(settings.tone2WidthMHz * 1.0e6);
        sweepEnabledCheck_->setChecked(settings.sweepEnabled);
        sweepStartFrequencySpin_->setFrequencyHz(settings.sweepStartFrequencyMHz * 1.0e6);
        sweepStopFrequencySpin_->setFrequencyHz(settings.sweepStopFrequencyMHz * 1.0e6);
        const int sweepDirectionIndex = sweepDirectionCombo_->findData(settings.sweepDirection);
        if (sweepDirectionIndex >= 0) {
            sweepDirectionCombo_->setCurrentIndex(sweepDirectionIndex);
        }
        sweepPeriodSpin_->setValue(settings.sweepPeriodSeconds);
        sweepAmplitudeSpin_->setValue(settings.sweepAmplitudeDbfs);
        transientProbabilitySpin_->setValue(settings.transientProbabilityPercent);
        transientAmplitudeSpin_->setValue(settings.transientAmplitudeDbfs);
        transientDurationSpin_->setValue(settings.transientDurationSeconds);
    }
    referenceLevelSpin_->setValue(settings.referenceLevelDbfs);
    bottomLevelSpin_->setValue(settings.bottomLevelDbfs);
    setTraceColorPreset(settings.plotColorPreset);
    if (!settings.customTraceColorHex.isEmpty()) {
        customTraceColor_ = QColor(settings.customTraceColorHex);
        if (settings.plotColorPreset == 6 && customTraceColorAction_) {
            QPixmap pix(14, 14);
            pix.fill(customTraceColor_);
            customTraceColorAction_->setIcon(QIcon(pix));
            customTraceColorAction_->setChecked(true);
        }
    }
    setTraceLineWidth(settings.plotLineWidth);
    setGridVisible(settings.plotGridVisible);
    setPlotTheme(settings.plotTheme);
    if (!settings.customThemeColorHex.isEmpty()) {
        customThemeColor_ = QColor(settings.customThemeColorHex);
        if (settings.plotTheme == 4 && customThemeAction_) {
            QPixmap pix(14, 14);
            pix.fill(customThemeColor_);
            customThemeAction_->setIcon(QIcon(pix));
            customThemeAction_->setChecked(true);
        }
    }
    averageCountSpin_->setValue(settings.averageCount);

    const int fftIndex = fftSizeCombo_->findData(settings.binCount);
    if (fftIndex >= 0) {
        fftSizeCombo_->setCurrentIndex(fftIndex);
    }
    const int traceIndex = traceModeCombo_->findData(settings.traceMode);
    if (traceIndex >= 0) {
        traceModeCombo_->setCurrentIndex(traceIndex);
    }
    const int viewModeIndex = displayViewModeCombo_->findData(settings.displayViewMode);
    if (viewModeIndex >= 0) {
        displayViewModeCombo_->setCurrentIndex(viewModeIndex);
    }
    const int colormapIndex = waterfallColormapCombo_->findData(settings.waterfallColormap);
    if (colormapIndex >= 0) {
        waterfallColormapCombo_->setCurrentIndex(colormapIndex);
    }
    waterfallHistorySpin_->setValue(settings.waterfallHistoryDepth);
    applyDisplayViewMode();
    applyWaterfallSettings();
}

void MainWindow::saveSettings() const
{
    if (!settingsEnabled_ || !centerFrequencySpin_) {
        return;
    }

    AppSettings settings;
    settings.windowGeometry = saveGeometry();
    settings.centerFrequencyMHz = centerFrequencySpin_->valueMHz();
    settings.spanMHz = spanSpin_->valueMHz();
    settings.binCount = fftSizeCombo_->currentData().toInt();
    settings.sourceFrameRate = sourceFrameRateSpin_->value();
    settings.noiseFloorDbfs = noiseFloorSpin_->value();
    if (simulationControl_) {
        settings.unthrottled = unthrottledCheck_->isChecked();
        settings.noiseDeviationDb = noiseDeviationSpin_->value();
        settings.tone1Enabled = tone1EnabledCheck_->isChecked();
        settings.tone1FrequencyMHz = tone1FrequencySpin_->valueMHz();
        settings.tone1AmplitudeDbfs = tone1AmplitudeSpin_->value();
        settings.tone1WidthMHz = tone1WidthSpin_->valueMHz();
        settings.tone2Enabled = tone2EnabledCheck_->isChecked();
        settings.tone2FrequencyMHz = tone2FrequencySpin_->valueMHz();
        settings.tone2AmplitudeDbfs = tone2AmplitudeSpin_->value();
        settings.tone2WidthMHz = tone2WidthSpin_->valueMHz();
        settings.sweepEnabled = sweepEnabledCheck_->isChecked();
        settings.sweepStartFrequencyMHz = sweepStartFrequencySpin_->valueMHz();
        settings.sweepStopFrequencyMHz = sweepStopFrequencySpin_->valueMHz();
        settings.sweepDirection = sweepDirectionCombo_->currentData().toInt();
        settings.sweepPeriodSeconds = sweepPeriodSpin_->value();
        settings.sweepAmplitudeDbfs = sweepAmplitudeSpin_->value();
        settings.transientProbabilityPercent = transientProbabilitySpin_->value();
        settings.transientAmplitudeDbfs = transientAmplitudeSpin_->value();
        settings.transientDurationSeconds = transientDurationSpin_->value();
    }
    settings.referenceLevelDbfs = referenceLevelSpin_->value();
    settings.bottomLevelDbfs = bottomLevelSpin_->value();
    settings.plotColorPreset = plotColorPreset_;
    settings.customTraceColorHex = customTraceColor_.name(QColor::HexRgb);
    settings.plotLineWidth = plotLineWidth_;
    settings.plotGridVisible = plotGridVisible_;
    settings.plotTheme = plotTheme_;
    settings.customThemeColorHex = customThemeColor_.name(QColor::HexRgb);
    settings.traceMode = traceModeCombo_->currentData().toInt();
    settings.averageCount = averageCountSpin_->value();
    settings.displayViewMode = displayViewModeCombo_->currentData().toInt();
    settings.waterfallColormap = waterfallColormapCombo_->currentData().toInt();
    settings.waterfallHistoryDepth = waterfallHistorySpin_->value();

    QSettings storage;
    const SettingsSaveResult result = ConfigurationStore::save(storage, settings);
    if (!result.success) {
        qWarning().noquote() << result.errorMessage;
    }
}

void MainWindow::loadSimulationConfiguration(const SimulationConfig& config)
{
    if (!simulationControl_) {
        return;
    }

    centerFrequencySpin_->setFrequencyHz(config.centerFrequencyHz);
    spanSpin_->setFrequencyHz(config.spanHz);
    synchronizeStartStopFromCenterSpan();
    sourceFrameRateSpin_->setValue(config.frameRate);
    unthrottledCheck_->setChecked(config.unthrottled);
    sourceFrameRateSpin_->setEnabled(!config.unthrottled);
    noiseFloorSpin_->setValue(config.noiseFloorDbfs);
    noiseDeviationSpin_->setValue(config.noiseDeviationDb);

    const ToneConfig emptyTone { false, config.centerFrequencyHz, -20.0F, 1.0e6 };
    const ToneConfig& tone1 = config.tones.empty() ? emptyTone : config.tones[0];
    const ToneConfig& tone2 = config.tones.size() < 2 ? emptyTone : config.tones[1];
    tone1EnabledCheck_->setChecked(tone1.enabled);
    tone1FrequencySpin_->setFrequencyHz(tone1.frequencyHz);
    tone1AmplitudeSpin_->setValue(tone1.amplitudeDbfs);
    tone1WidthSpin_->setFrequencyHz(tone1.widthHz);
    tone2EnabledCheck_->setChecked(tone2.enabled);
    tone2FrequencySpin_->setFrequencyHz(tone2.frequencyHz);
    tone2AmplitudeSpin_->setValue(tone2.amplitudeDbfs);
    tone2WidthSpin_->setFrequencyHz(tone2.widthHz);

    sweepEnabledCheck_->setChecked(config.sweepEnabled);
    sweepStartFrequencySpin_->setFrequencyHz(config.sweepStartHz);
    sweepStopFrequencySpin_->setFrequencyHz(config.sweepStopHz);
    const int directionIndex = sweepDirectionCombo_->findData(
        static_cast<int>(config.sweepDirection));
    if (directionIndex >= 0) {
        sweepDirectionCombo_->setCurrentIndex(directionIndex);
    }
    sweepPeriodSpin_->setValue(config.sweepPeriodSeconds);
    sweepAmplitudeSpin_->setValue(config.sweepAmplitudeDbfs);
    transientProbabilitySpin_->setValue(config.transientProbability * 100.0F);
    transientAmplitudeSpin_->setValue(config.transientAmplitudeDbfs);
    transientDurationSpin_->setValue(config.transientDurationSeconds);

    const int fftIndex = fftSizeCombo_->findData(static_cast<int>(config.binCount));
    if (fftIndex >= 0) {
        fftSizeCombo_->setCurrentIndex(fftIndex);
    }
}

void MainWindow::updateButtonStates(const SourceState state)
{
    const bool startable = state == SourceState::Initialized
        || state == SourceState::Stopped || state == SourceState::Error;
    const bool running = state == SourceState::Running || state == SourceState::Starting;
    const bool paused = state == SourceState::Paused;
    startButton_->setEnabled(startable || paused);
    pauseButton_->setEnabled(running);
    stopButton_->setEnabled(running || paused);
    singleButton_->setEnabled(startable || paused);

    if (statusStateChip_) {
        switch (state) {
        case SourceState::Running:
        case SourceState::Starting:
            statusStateChip_->setText(tr("【● 运行中】"));
            statusStateChip_->setStyleSheet(
                QStringLiteral("QLabel { font-weight: bold; padding: 2px 8px; border-radius: 3px; background: #1b5e20; color: #a5d6a7; border: 1px solid #2e7d32; }"));
            break;
        case SourceState::Paused:
            statusStateChip_->setText(tr("【❚❚ 已暂停】"));
            statusStateChip_->setStyleSheet(
                QStringLiteral("QLabel { font-weight: bold; padding: 2px 8px; border-radius: 3px; background: #e65100; color: #ffe0b2; border: 1px solid #ef6c00; }"));
            break;
        case SourceState::Error:
            statusStateChip_->setText(tr("【⚠ 异常】"));
            statusStateChip_->setStyleSheet(
                QStringLiteral("QLabel { font-weight: bold; padding: 2px 8px; border-radius: 3px; background: #b71c1c; color: #ffcdd2; border: 1px solid #c62828; }"));
            break;
        default:
            statusStateChip_->setText(tr("【■ 已停止】"));
            statusStateChip_->setStyleSheet(
                QStringLiteral("QLabel { font-weight: bold; padding: 2px 8px; border-radius: 3px; background: #263238; color: #b0bec5; border: 1px solid #37474f; }"));
            break;
        }
    }
}

SimulationConfig MainWindow::configurationFromUi() const
{
    SimulationConfig config = simulationControl_
        ? simulationControl_->configuration()
        : SimulationConfig {};
    config.centerFrequencyHz = centerFrequencySpin_->frequencyHz();
    config.spanHz = spanSpin_->frequencyHz();
    config.binCount = static_cast<std::size_t>(fftSizeCombo_->currentData().toUInt());
    config.frameRate = sourceFrameRateSpin_->value();
    config.unthrottled = unthrottledCheck_ && unthrottledCheck_->isChecked();
    config.noiseFloorDbfs = static_cast<float>(noiseFloorSpin_->value());
    if (simulationControl_) {
        config.noiseDeviationDb = static_cast<float>(noiseDeviationSpin_->value());
    }

    const double startHz = config.centerFrequencyHz - config.spanHz * 0.5;
    config.sweepStartHz = startHz + config.spanHz * 0.15;
    config.sweepStopHz = startHz + config.spanHz * 0.85;
    if (simulationControl_) {
        config.tones = {
            ToneConfig { tone1EnabledCheck_->isChecked(),
                         tone1FrequencySpin_->frequencyHz(),
                         static_cast<float>(tone1AmplitudeSpin_->value()),
                         tone1WidthSpin_->frequencyHz() },
            ToneConfig { tone2EnabledCheck_->isChecked(),
                         tone2FrequencySpin_->frequencyHz(),
                         static_cast<float>(tone2AmplitudeSpin_->value()),
                         tone2WidthSpin_->frequencyHz() }
        };
        config.sweepEnabled = sweepEnabledCheck_->isChecked();
        config.sweepStartHz = sweepStartFrequencySpin_->frequencyHz();
        config.sweepStopHz = sweepStopFrequencySpin_->frequencyHz();
        config.sweepDirection = static_cast<SweepDirection>(
            sweepDirectionCombo_->currentData().toInt());
        config.sweepPeriodSeconds = sweepPeriodSpin_->value();
        config.sweepAmplitudeDbfs = static_cast<float>(sweepAmplitudeSpin_->value());
        config.transientProbability = static_cast<float>(
            transientProbabilitySpin_->value() / 100.0);
        config.transientAmplitudeDbfs = static_cast<float>(transientAmplitudeSpin_->value());
        config.transientDurationSeconds = transientDurationSpin_->value();
    }
    return config;
}

double MainWindow::displayLatencyP95() const
{
    if (displayLatencyCount_ == 0U) {
        return 0.0;
    }
    auto sorted = displayLatenciesMs_;
    std::sort(sorted.begin(), sorted.begin()
        + static_cast<std::ptrdiff_t>(displayLatencyCount_));
    const std::size_t rank = static_cast<std::size_t>(
        std::ceil(static_cast<double>(displayLatencyCount_) * 0.95)) - 1U;
    return sorted[std::min(rank, displayLatencyCount_ - 1U)];
}

QString MainWindow::formatRate(const double value, const QString& suffix)
{
    const double absolute = std::abs(value);
    if (absolute >= 1.0e9) {
        return QStringLiteral("%1 G%2").arg(value / 1.0e9, 0, 'f', 2).arg(suffix);
    }
    if (absolute >= 1.0e6) {
        return QStringLiteral("%1 M%2").arg(value / 1.0e6, 0, 'f', 2).arg(suffix);
    }
    if (absolute >= 1.0e3) {
        return QStringLiteral("%1 k%2").arg(value / 1.0e3, 0, 'f', 2).arg(suffix);
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', 1).arg(suffix);
}

} // namespace rtsa
