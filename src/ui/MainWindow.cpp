#include "ui/MainWindow.h"

#include "core/AmplitudeUnits.h"
#include "core/SpectrumMeasurements.h"
#include "plot/SpectrumPlotWidget.h"
#include "services/ConfigurationStore.h"
#include "services/SpectrumExporter.h"
#include "sources/SimulationScenarioWriter.h"

#include <QCloseEvent>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace rtsa {
namespace {

QString sourceStateText(const SourceState state)
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
    fullRangeCenterHz_ = centerFrequencySpin_->value() * 1.0e6;
    fullRangeSpanHz_ = spanSpin_->value() * 1.0e6;
    connectUi();

    source_->setFrameSink([this](const SpectrumFramePtr& frame) {
        return pipeline_.submit(frame);
    });
    applyTraceConfiguration();
    applyAmplitudeScale();
    applyPlotAppearance();
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

    statusBar()->showMessage(
        tr("%1 | 输入 %2 | 显示 %3 | 数据 %4 | 源丢帧 %5 | 无效 %6 | 显示跳过 %7 | 延迟 %8 | 绘制 %9 ms")
            .arg(sourceStateLabel_->text())
            .arg(inputRateLabel_->text())
            .arg(displayRateLabel_->text())
            .arg(dataRateLabel_->text())
            .arg(sourceDropLabel_->text())
            .arg(invalidFrameLabel_->text())
            .arg(displaySkippedLabel_->text())
            .arg(latencyLabel_->text())
            .arg(plot_->lastPaintMilliseconds(), 0, 'f', 2));
}

void MainWindow::applySourceConfiguration()
{
    synchronizeStartStopFromCenterSpan();
    fullRangeCenterHz_ = centerFrequencySpin_->value() * 1.0e6;
    fullRangeSpanHz_ = spanSpin_->value() * 1.0e6;
    configureSourceFromUi();
}

void MainWindow::applyNonFrequencySourceConfiguration()
{
    if (simulationControl_
        && sweepStartOffsetSpin_->value() >= sweepStopOffsetSpin_->value()) {
        const QSignalBlocker blocker(sweepStopOffsetSpin_);
        sweepStopOffsetSpin_->setValue(sweepStartOffsetSpin_->value() + 0.001);
    }
    configureSourceFromUi();
}

void MainWindow::applyStartStopConfiguration()
{
    double startMHz = startFrequencySpin_->value();
    double stopMHz = stopFrequencySpin_->value();
    if (stopMHz <= startMHz) {
        stopMHz = startMHz + 0.001;
    }
    const double spanMHz = std::clamp(stopMHz - startMHz,
                                      spanSpin_->minimum(), spanSpin_->maximum());
    const double centerMHz = std::clamp((startMHz + stopMHz) * 0.5,
                                        centerFrequencySpin_->minimum(),
                                        centerFrequencySpin_->maximum());
    {
        const QSignalBlocker centerBlocker(centerFrequencySpin_);
        const QSignalBlocker spanBlocker(spanSpin_);
        centerFrequencySpin_->setValue(centerMHz);
        spanSpin_->setValue(spanMHz);
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
    const QColor color = plotColorCombo_->currentData().value<QColor>();
    plot_->setAppearance(color,
                         plotLineWidthSpin_->value(),
                         plotGridCheck_->isChecked(),
                         plotThemeCombo_->currentData().toInt() == 1);
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
        measurementStartSpin_->value() * 1.0e6,
        measurementStopSpin_->value() * 1.0e6);
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
        measurementStartSpin_->value() * 1.0e6,
        measurementStopSpin_->value() * 1.0e6);
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
    const double centerMHz = centerFrequencySpin_->value();
    const double halfSpanMHz = spanSpin_->value() * 0.5;
    const QSignalBlocker startBlocker(startFrequencySpin_);
    const QSignalBlocker stopBlocker(stopFrequencySpin_);
    startFrequencySpin_->setValue(centerMHz - halfSpanMHz);
    stopFrequencySpin_->setValue(centerMHz + halfSpanMHz);
    if (measurementStartSpin_ && measurementStopSpin_) {
        const QSignalBlocker measurementStartBlocker(measurementStartSpin_);
        const QSignalBlocker measurementStopBlocker(measurementStopSpin_);
        measurementStartSpin_->setValue(centerMHz - halfSpanMHz);
        measurementStopSpin_->setValue(centerMHz + halfSpanMHz);
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
    const double oldCenterHz = centerFrequencySpin_->value() * 1.0e6;
    const double oldSpanHz = spanSpin_->value() * 1.0e6;
    const double newSpanHz = std::clamp(oldSpanHz * scaleFactor, 1.0e3, 10.0e9);
    const double newCenterHz = anchorFrequencyHz + (oldCenterHz - anchorFrequencyHz) * scaleFactor;

    {
        const QSignalBlocker centerBlocker(centerFrequencySpin_);
        const QSignalBlocker spanBlocker(spanSpin_);
        centerFrequencySpin_->setValue(newCenterHz / 1.0e6);
        spanSpin_->setValue(newSpanHz / 1.0e6);
    }
    synchronizeStartStopFromCenterSpan();
    configureSourceFromUi();
}

void MainWindow::handleFrequencyPanRequested(const double centerShiftHz)
{
    const QSignalBlocker blocker(centerFrequencySpin_);
    centerFrequencySpin_->setValue(
        centerFrequencySpin_->value() + centerShiftHz / 1.0e6);
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
    const double spanMHz = std::clamp(
        (stopFrequencyHz - startFrequencyHz) / 1.0e6,
        spanSpin_->minimum(), spanSpin_->maximum());
    const double centerMHz = std::clamp(
        (startFrequencyHz + stopFrequencyHz) * 0.5 / 1.0e6,
        centerFrequencySpin_->minimum(), centerFrequencySpin_->maximum());
    {
        const QSignalBlocker centerBlocker(centerFrequencySpin_);
        const QSignalBlocker spanBlocker(spanSpin_);
        centerFrequencySpin_->setValue(centerMHz);
        spanSpin_->setValue(spanMHz);
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
        centerFrequencySpin_->setValue(fullRangeCenterHz_ / 1.0e6);
        spanSpin_->setValue(fullRangeSpanHz_ / 1.0e6);
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

void MainWindow::buildUi()
{
    auto* central = new QWidget(this);
    auto* layout = new QHBoxLayout(central);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    plot_ = new SpectrumPlotWidget(central);
    plot_->setObjectName(QStringLiteral("spectrumPlot"));
    plot_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(plot_, 1);
    layout->addWidget(buildControlPanel());
    setCentralWidget(central);
}

QWidget* MainWindow::buildControlPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(buildSourceGroup());
    if (simulationControl_) {
        layout->addWidget(buildSimulationGroup());
    }
    layout->addWidget(buildDisplayGroup());
    layout->addWidget(buildTraceGroup());
    layout->addWidget(buildMarkerGroup());
    layout->addWidget(buildMeasurementGroup());
    layout->addWidget(buildFileGroup());
    layout->addWidget(buildTelemetryGroup());
    layout->addStretch(1);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setFixedWidth(330);
    scrollArea->setWidget(panel);
    return scrollArea;
}

QWidget* MainWindow::buildSourceGroup()
{
    auto* group = new QGroupBox(tr("数据源与频率"), this);
    auto* form = new QFormLayout(group);

    centerFrequencySpin_ = new QDoubleSpinBox(group);
    centerFrequencySpin_->setObjectName(QStringLiteral("centerFrequencyMHz"));
    centerFrequencySpin_->setRange(0.0, 20000.0);
    centerFrequencySpin_->setDecimals(6);
    centerFrequencySpin_->setValue(1000.0);
    centerFrequencySpin_->setSuffix(tr(" MHz"));

    spanSpin_ = new QDoubleSpinBox(group);
    spanSpin_->setObjectName(QStringLiteral("spanMHz"));
    spanSpin_->setRange(0.001, 10000.0);
    spanSpin_->setDecimals(3);
    spanSpin_->setValue(200.0);
    spanSpin_->setSuffix(tr(" MHz"));

    startFrequencySpin_ = new QDoubleSpinBox(group);
    startFrequencySpin_->setObjectName(QStringLiteral("startFrequencyMHz"));
    startFrequencySpin_->setRange(-5000.0, 25000.0);
    startFrequencySpin_->setDecimals(6);
    startFrequencySpin_->setValue(900.0);
    startFrequencySpin_->setSuffix(tr(" MHz"));

    stopFrequencySpin_ = new QDoubleSpinBox(group);
    stopFrequencySpin_->setObjectName(QStringLiteral("stopFrequencyMHz"));
    stopFrequencySpin_->setRange(-4999.999, 25000.0);
    stopFrequencySpin_->setDecimals(6);
    stopFrequencySpin_->setValue(1100.0);
    stopFrequencySpin_->setSuffix(tr(" MHz"));

    fftSizeCombo_ = new QComboBox(group);
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
    noiseFloorSpin_->setRange(-180.0, -10.0);
    noiseFloorSpin_->setValue(-110.0);
    noiseFloorSpin_->setSuffix(tr(" dBFS"));

    form->addRow(tr("中心频率"), centerFrequencySpin_);
    form->addRow(tr("频宽（Span）"), spanSpin_);
    form->addRow(tr("起始频率"), startFrequencySpin_);
    form->addRow(tr("终止频率"), stopFrequencySpin_);
    form->addRow(tr("频点数"), fftSizeCombo_);
    form->addRow(tr("输入帧率"), sourceFrameRateSpin_);
    form->addRow(tr("噪声底"), noiseFloorSpin_);

    auto* buttons = new QWidget(group);
    auto* buttonLayout = new QGridLayout(buttons);
    buttonLayout->setContentsMargins(0, 4, 0, 0);
    startButton_ = new QPushButton(tr("开始/继续"), buttons);
    pauseButton_ = new QPushButton(tr("暂停"), buttons);
    stopButton_ = new QPushButton(tr("停止"), buttons);
    singleButton_ = new QPushButton(tr("单次"), buttons);
    buttonLayout->addWidget(startButton_, 0, 0, 1, 2);
    buttonLayout->addWidget(pauseButton_, 1, 0);
    buttonLayout->addWidget(stopButton_, 1, 1);
    buttonLayout->addWidget(singleButton_, 2, 0, 1, 2);
    form->addRow(buttons);
    return group;
}

QWidget* MainWindow::buildDisplayGroup()
{
    auto* group = new QGroupBox(tr("幅度显示"), this);
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

    plotColorCombo_ = new QComboBox(group);
    plotColorCombo_->setObjectName(QStringLiteral("plotColorPreset"));
    plotColorCombo_->addItem(tr("翠绿"), QColor(0, 235, 180));
    plotColorCombo_->addItem(tr("青蓝"), QColor(0, 166, 255));
    plotColorCombo_->addItem(tr("明黄"), QColor(255, 205, 55));
    plotColorCombo_->addItem(tr("白色"), QColor(235, 240, 245));
    plotLineWidthSpin_ = new QSpinBox(group);
    plotLineWidthSpin_->setObjectName(QStringLiteral("plotLineWidth"));
    plotLineWidthSpin_->setRange(1, 4);
    plotLineWidthSpin_->setValue(1);
    plotLineWidthSpin_->setSuffix(tr(" px"));
    plotGridCheck_ = new QCheckBox(tr("显示网格"), group);
    plotGridCheck_->setObjectName(QStringLiteral("plotGridVisible"));
    plotGridCheck_->setChecked(true);
    plotThemeCombo_ = new QComboBox(group);
    plotThemeCombo_->setObjectName(QStringLiteral("plotTheme"));
    plotThemeCombo_->addItem(tr("深色"), 0);
    plotThemeCombo_->addItem(tr("浅色"), 1);

    fullScreenButton_ = new QPushButton(tr("进入全屏"), group);
    fullScreenButton_->setObjectName(QStringLiteral("fullScreenButton"));
    autoRangeButton_ = new QPushButton(tr("自动量程"), group);

    form->addRow(tr("参考电平"), referenceLevelSpin_);
    form->addRow(tr("底部电平"), bottomLevelSpin_);
    form->addRow(tr("垂直刻度"), verticalScaleSpin_);
    form->addRow(tr("曲线颜色"), plotColorCombo_);
    form->addRow(tr("曲线线宽"), plotLineWidthSpin_);
    form->addRow(tr("网格"), plotGridCheck_);
    form->addRow(tr("绘图区主题"), plotThemeCombo_);
    form->addRow(autoRangeButton_);
    form->addRow(fullScreenButton_);
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
    tone1OffsetSpin_ = new QDoubleSpinBox(group);
    tone1OffsetSpin_->setObjectName(QStringLiteral("tone1OffsetMHz"));
    tone1OffsetSpin_->setRange(-5000.0, 5000.0);
    tone1OffsetSpin_->setDecimals(6);
    tone1OffsetSpin_->setValue(-20.0);
    tone1OffsetSpin_->setSuffix(tr(" MHz"));
    tone1AmplitudeSpin_ = new QDoubleSpinBox(group);
    tone1AmplitudeSpin_->setObjectName(QStringLiteral("tone1AmplitudeDbfs"));
    tone1AmplitudeSpin_->setRange(-180.0, 0.0);
    tone1AmplitudeSpin_->setValue(-35.0);
    tone1AmplitudeSpin_->setSuffix(tr(" dBFS"));
    tone1WidthSpin_ = new QDoubleSpinBox(group);
    tone1WidthSpin_->setObjectName(QStringLiteral("tone1WidthBins"));
    tone1WidthSpin_->setRange(0.1, 1024.0);
    tone1WidthSpin_->setDecimals(2);
    tone1WidthSpin_->setValue(1.5);
    tone1WidthSpin_->setSuffix(tr(" bins"));

    tone2EnabledCheck_ = new QCheckBox(tr("启用"), group);
    tone2EnabledCheck_->setObjectName(QStringLiteral("tone2Enabled"));
    tone2EnabledCheck_->setChecked(true);
    tone2OffsetSpin_ = new QDoubleSpinBox(group);
    tone2OffsetSpin_->setObjectName(QStringLiteral("tone2OffsetMHz"));
    tone2OffsetSpin_->setRange(-5000.0, 5000.0);
    tone2OffsetSpin_->setDecimals(6);
    tone2OffsetSpin_->setValue(35.0);
    tone2OffsetSpin_->setSuffix(tr(" MHz"));
    tone2AmplitudeSpin_ = new QDoubleSpinBox(group);
    tone2AmplitudeSpin_->setObjectName(QStringLiteral("tone2AmplitudeDbfs"));
    tone2AmplitudeSpin_->setRange(-180.0, 0.0);
    tone2AmplitudeSpin_->setValue(-18.0);
    tone2AmplitudeSpin_->setSuffix(tr(" dBFS"));
    tone2WidthSpin_ = new QDoubleSpinBox(group);
    tone2WidthSpin_->setObjectName(QStringLiteral("tone2WidthBins"));
    tone2WidthSpin_->setRange(0.1, 1024.0);
    tone2WidthSpin_->setDecimals(2);
    tone2WidthSpin_->setValue(2.5);
    tone2WidthSpin_->setSuffix(tr(" bins"));

    sweepEnabledCheck_ = new QCheckBox(tr("启用"), group);
    sweepEnabledCheck_->setObjectName(QStringLiteral("sweepEnabled"));
    sweepEnabledCheck_->setChecked(true);
    sweepDirectionCombo_ = new QComboBox(group);
    sweepDirectionCombo_->setObjectName(QStringLiteral("sweepDirection"));
    sweepDirectionCombo_->addItem(tr("向上"), static_cast<int>(SweepDirection::Up));
    sweepDirectionCombo_->addItem(tr("向下"), static_cast<int>(SweepDirection::Down));
    sweepDirectionCombo_->addItem(tr("往返"), static_cast<int>(SweepDirection::PingPong));
    sweepStartOffsetSpin_ = new QDoubleSpinBox(group);
    sweepStartOffsetSpin_->setObjectName(QStringLiteral("sweepStartOffsetMHz"));
    sweepStartOffsetSpin_->setRange(-5000.0, 4999.999);
    sweepStartOffsetSpin_->setDecimals(6);
    sweepStartOffsetSpin_->setValue(-70.0);
    sweepStartOffsetSpin_->setSuffix(tr(" MHz"));
    sweepStopOffsetSpin_ = new QDoubleSpinBox(group);
    sweepStopOffsetSpin_->setObjectName(QStringLiteral("sweepStopOffsetMHz"));
    sweepStopOffsetSpin_->setRange(-4999.999, 5000.0);
    sweepStopOffsetSpin_->setDecimals(6);
    sweepStopOffsetSpin_->setValue(70.0);
    sweepStopOffsetSpin_->setSuffix(tr(" MHz"));
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
    form->addRow(tr("单音 1 偏移"), tone1OffsetSpin_);
    form->addRow(tr("单音 1 幅度"), tone1AmplitudeSpin_);
    form->addRow(tr("单音 1 带宽"), tone1WidthSpin_);
    form->addRow(tr("单音 2"), tone2EnabledCheck_);
    form->addRow(tr("单音 2 偏移"), tone2OffsetSpin_);
    form->addRow(tr("单音 2 幅度"), tone2AmplitudeSpin_);
    form->addRow(tr("单音 2 带宽"), tone2WidthSpin_);
    form->addRow(tr("扫频"), sweepEnabledCheck_);
    form->addRow(tr("扫频方向"), sweepDirectionCombo_);
    form->addRow(tr("扫频起点偏移"), sweepStartOffsetSpin_);
    form->addRow(tr("扫频终点偏移"), sweepStopOffsetSpin_);
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
    measurementStartSpin_ = new QDoubleSpinBox(group);
    measurementStartSpin_->setObjectName(QStringLiteral("measurementStartMHz"));
    measurementStartSpin_->setRange(-5000.0, 25000.0);
    measurementStartSpin_->setDecimals(6);
    measurementStartSpin_->setValue(900.0);
    measurementStartSpin_->setSuffix(tr(" MHz"));
    measurementStopSpin_ = new QDoubleSpinBox(group);
    measurementStopSpin_->setObjectName(QStringLiteral("measurementStopMHz"));
    measurementStopSpin_->setRange(-4999.999, 25000.0);
    measurementStopSpin_->setDecimals(6);
    measurementStopSpin_->setValue(1100.0);
    measurementStopSpin_->setSuffix(tr(" MHz"));
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
    form->addRow(tr("起始频率"), measurementStartSpin_);
    form->addRow(tr("终止频率"), measurementStopSpin_);
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
        for (QDoubleSpinBox* spin : { noiseDeviationSpin_, tone1OffsetSpin_,
                                     tone1AmplitudeSpin_, tone1WidthSpin_,
                                     tone2OffsetSpin_, tone2AmplitudeSpin_,
                                     tone2WidthSpin_, sweepStartOffsetSpin_,
                                     sweepStopOffsetSpin_, sweepPeriodSpin_,
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
    connect(plotColorCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::applyPlotAppearance);
    connect(plotLineWidthSpin_, qOverload<int>(&QSpinBox::valueChanged),
            this, &MainWindow::applyPlotAppearance);
    connect(plotGridCheck_, &QCheckBox::toggled,
            this, &MainWindow::applyPlotAppearance);
    connect(plotThemeCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &MainWindow::applyPlotAppearance);

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

    centerFrequencySpin_->setValue(settings.centerFrequencyMHz);
    spanSpin_->setValue(settings.spanMHz);
    synchronizeStartStopFromCenterSpan();
    sourceFrameRateSpin_->setValue(settings.sourceFrameRate);
    noiseFloorSpin_->setValue(settings.noiseFloorDbfs);
    if (simulationControl_) {
        unthrottledCheck_->setChecked(settings.unthrottled);
        sourceFrameRateSpin_->setEnabled(!settings.unthrottled);
        noiseDeviationSpin_->setValue(settings.noiseDeviationDb);
        tone1EnabledCheck_->setChecked(settings.tone1Enabled);
        tone1OffsetSpin_->setValue(settings.tone1OffsetMHz);
        tone1AmplitudeSpin_->setValue(settings.tone1AmplitudeDbfs);
        tone1WidthSpin_->setValue(settings.tone1WidthBins);
        tone2EnabledCheck_->setChecked(settings.tone2Enabled);
        tone2OffsetSpin_->setValue(settings.tone2OffsetMHz);
        tone2AmplitudeSpin_->setValue(settings.tone2AmplitudeDbfs);
        tone2WidthSpin_->setValue(settings.tone2WidthBins);
        sweepEnabledCheck_->setChecked(settings.sweepEnabled);
        sweepStartOffsetSpin_->setValue(settings.sweepStartOffsetMHz);
        sweepStopOffsetSpin_->setValue(settings.sweepStopOffsetMHz);
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
    plotColorCombo_->setCurrentIndex(settings.plotColorPreset);
    plotLineWidthSpin_->setValue(settings.plotLineWidth);
    plotGridCheck_->setChecked(settings.plotGridVisible);
    const int themeIndex = plotThemeCombo_->findData(settings.plotTheme);
    if (themeIndex >= 0) {
        plotThemeCombo_->setCurrentIndex(themeIndex);
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
}

void MainWindow::saveSettings() const
{
    if (!settingsEnabled_ || !centerFrequencySpin_) {
        return;
    }

    AppSettings settings;
    settings.windowGeometry = saveGeometry();
    settings.centerFrequencyMHz = centerFrequencySpin_->value();
    settings.spanMHz = spanSpin_->value();
    settings.binCount = fftSizeCombo_->currentData().toInt();
    settings.sourceFrameRate = sourceFrameRateSpin_->value();
    settings.noiseFloorDbfs = noiseFloorSpin_->value();
    if (simulationControl_) {
        settings.unthrottled = unthrottledCheck_->isChecked();
        settings.noiseDeviationDb = noiseDeviationSpin_->value();
        settings.tone1Enabled = tone1EnabledCheck_->isChecked();
        settings.tone1OffsetMHz = tone1OffsetSpin_->value();
        settings.tone1AmplitudeDbfs = tone1AmplitudeSpin_->value();
        settings.tone1WidthBins = tone1WidthSpin_->value();
        settings.tone2Enabled = tone2EnabledCheck_->isChecked();
        settings.tone2OffsetMHz = tone2OffsetSpin_->value();
        settings.tone2AmplitudeDbfs = tone2AmplitudeSpin_->value();
        settings.tone2WidthBins = tone2WidthSpin_->value();
        settings.sweepEnabled = sweepEnabledCheck_->isChecked();
        settings.sweepStartOffsetMHz = sweepStartOffsetSpin_->value();
        settings.sweepStopOffsetMHz = sweepStopOffsetSpin_->value();
        settings.sweepDirection = sweepDirectionCombo_->currentData().toInt();
        settings.sweepPeriodSeconds = sweepPeriodSpin_->value();
        settings.sweepAmplitudeDbfs = sweepAmplitudeSpin_->value();
        settings.transientProbabilityPercent = transientProbabilitySpin_->value();
        settings.transientAmplitudeDbfs = transientAmplitudeSpin_->value();
        settings.transientDurationSeconds = transientDurationSpin_->value();
    }
    settings.referenceLevelDbfs = referenceLevelSpin_->value();
    settings.bottomLevelDbfs = bottomLevelSpin_->value();
    settings.plotColorPreset = plotColorCombo_->currentIndex();
    settings.plotLineWidth = plotLineWidthSpin_->value();
    settings.plotGridVisible = plotGridCheck_->isChecked();
    settings.plotTheme = plotThemeCombo_->currentData().toInt();
    settings.traceMode = traceModeCombo_->currentData().toInt();
    settings.averageCount = averageCountSpin_->value();

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

    centerFrequencySpin_->setValue(config.centerFrequencyHz / 1.0e6);
    spanSpin_->setValue(config.spanHz / 1.0e6);
    synchronizeStartStopFromCenterSpan();
    sourceFrameRateSpin_->setValue(config.frameRate);
    unthrottledCheck_->setChecked(config.unthrottled);
    sourceFrameRateSpin_->setEnabled(!config.unthrottled);
    noiseFloorSpin_->setValue(config.noiseFloorDbfs);
    noiseDeviationSpin_->setValue(config.noiseDeviationDb);

    const ToneConfig emptyTone { false, config.centerFrequencyHz, -20.0F, 1.0F };
    const ToneConfig& tone1 = config.tones.empty() ? emptyTone : config.tones[0];
    const ToneConfig& tone2 = config.tones.size() < 2 ? emptyTone : config.tones[1];
    tone1EnabledCheck_->setChecked(tone1.enabled);
    tone1OffsetSpin_->setValue((tone1.frequencyHz - config.centerFrequencyHz) / 1.0e6);
    tone1AmplitudeSpin_->setValue(tone1.amplitudeDbfs);
    tone1WidthSpin_->setValue(tone1.widthBins);
    tone2EnabledCheck_->setChecked(tone2.enabled);
    tone2OffsetSpin_->setValue((tone2.frequencyHz - config.centerFrequencyHz) / 1.0e6);
    tone2AmplitudeSpin_->setValue(tone2.amplitudeDbfs);
    tone2WidthSpin_->setValue(tone2.widthBins);

    sweepEnabledCheck_->setChecked(config.sweepEnabled);
    sweepStartOffsetSpin_->setValue(
        (config.sweepStartHz - config.centerFrequencyHz) / 1.0e6);
    sweepStopOffsetSpin_->setValue(
        (config.sweepStopHz - config.centerFrequencyHz) / 1.0e6);
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
}

SimulationConfig MainWindow::configurationFromUi() const
{
    SimulationConfig config = simulationControl_
        ? simulationControl_->configuration()
        : SimulationConfig {};
    config.centerFrequencyHz = centerFrequencySpin_->value() * 1.0e6;
    config.spanHz = spanSpin_->value() * 1.0e6;
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
                         config.centerFrequencyHz + tone1OffsetSpin_->value() * 1.0e6,
                         static_cast<float>(tone1AmplitudeSpin_->value()),
                         static_cast<float>(tone1WidthSpin_->value()) },
            ToneConfig { tone2EnabledCheck_->isChecked(),
                         config.centerFrequencyHz + tone2OffsetSpin_->value() * 1.0e6,
                         static_cast<float>(tone2AmplitudeSpin_->value()),
                         static_cast<float>(tone2WidthSpin_->value()) }
        };
        config.sweepEnabled = sweepEnabledCheck_->isChecked();
        config.sweepStartHz = config.centerFrequencyHz
            + sweepStartOffsetSpin_->value() * 1.0e6;
        config.sweepStopHz = config.centerFrequencyHz
            + sweepStopOffsetSpin_->value() * 1.0e6;
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
