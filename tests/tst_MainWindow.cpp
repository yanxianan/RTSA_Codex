#include "plot/SpectrumPlotWidget.h"
#include "sources/SimulatedSpectrumSource.h"
#include "ui/ApplicationTheme.h"
#include "ui/MainWindow.h"

#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>

namespace rtsa {

class FakeSpectrumSource final : public ISpectrumSource {
public:
    FakeSpectrumSource()
        : ISpectrumSource(nullptr)
    {
    }

    void setFrameSink(FrameSink sink) override { sink_ = std::move(sink); }
    bool start(AcquisitionMode = AcquisitionMode::Continuous) override
    {
        state_ = SourceState::Running;
        emit stateChanged(static_cast<int>(state_));
        return true;
    }
    void pause() override { state_ = SourceState::Paused; }
    void resume() override { state_ = SourceState::Running; }
    void stop() override { state_ = SourceState::Stopped; }
    SourceState state() const noexcept override { return state_; }
    SourceStatistics statistics() const override { return {}; }
    bool deliver(const SpectrumFramePtr& frame) { return sink_ && sink_(frame); }

private:
    FrameSink sink_;
    SourceState state_ = SourceState::Initialized;
};

class MainWindowTests final : public QObject {
    Q_OBJECT

private slots:
    void applicationThemeMaintainsReadableControlContrast();
    void centerSpanAndStartStopStaySynchronized();
    void zoomAndResetRestoreUserRange();
    void nonFrequencyChangeDoesNotReplaceResetRange();
    void boxZoomAndResetRestoreUserRange();
    void repeatedExternalSequenceDisplaysNewSessionFrame();
    void displaySkipUsesPublicationSequence();
    void singleAcquisitionDisplaysExactlyOneSequence();
    void fullScreenStateUpdatesButtonText();
    void fullHdRasterEndToEndPerformance();
    void simulatorControlsUpdateInjectedSource();
    void nonSimulationSourceHidesSimulatorControls();
    void initialScenarioPopulatesControlsAndSource();
    void invalidSweepOffsetsAreCorrected();
    void markerControlsSupportFourMarkersAndDelta();
    void rangeMeasurementsUseLatestFullFrame();
    void telemetryShowsProcessingRenderAndQueueDepth();
};

namespace {

double linearColorComponent(int component)
{
    const double normalized = static_cast<double>(component) / 255.0;
    return normalized <= 0.04045
        ? normalized / 12.92
        : std::pow((normalized + 0.055) / 1.055, 2.4);
}

double relativeLuminance(const QColor& color)
{
    return 0.2126 * linearColorComponent(color.red())
        + 0.7152 * linearColorComponent(color.green())
        + 0.0722 * linearColorComponent(color.blue());
}

double contrastRatio(const QColor& first, const QColor& second)
{
    const double firstLuminance = relativeLuminance(first);
    const double secondLuminance = relativeLuminance(second);
    const double lighter = std::max(firstLuminance, secondLuminance);
    const double darker = std::min(firstLuminance, secondLuminance);
    return (lighter + 0.05) / (darker + 0.05);
}

} // namespace

void MainWindowTests::applicationThemeMaintainsReadableControlContrast()
{
    const QPalette palette = createApplicationPalette();
    for (const QPalette::ColorGroup group : {
             QPalette::Active, QPalette::Inactive, QPalette::Disabled }) {
        const double inputContrast = contrastRatio(
            palette.color(group, QPalette::Text),
            palette.color(group, QPalette::Base));
        const double buttonContrast = contrastRatio(
            palette.color(group, QPalette::ButtonText),
            palette.color(group, QPalette::Button));
        const double labelContrast = contrastRatio(
            palette.color(group, QPalette::WindowText),
            palette.color(group, QPalette::Window));
        QVERIFY2(inputContrast >= 4.5, "Input text contrast is below 4.5:1");
        QVERIFY2(buttonContrast >= 4.5, "Button text contrast is below 4.5:1");
        QVERIFY2(labelContrast >= 4.5, "Label text contrast is below 4.5:1");
    }
}

void MainWindowTests::centerSpanAndStartStopStaySynchronized()
{
    MainWindow window(std::make_unique<SimulatedSpectrumSource>(), nullptr, false);
    auto* center = window.findChild<QDoubleSpinBox*>(QStringLiteral("centerFrequencyMHz"));
    auto* span = window.findChild<QDoubleSpinBox*>(QStringLiteral("spanMHz"));
    auto* start = window.findChild<QDoubleSpinBox*>(QStringLiteral("startFrequencyMHz"));
    auto* stop = window.findChild<QDoubleSpinBox*>(QStringLiteral("stopFrequencyMHz"));
    QVERIFY(center && span && start && stop);

    center->setValue(1200.0);
    span->setValue(100.0);
    QVERIFY(QMetaObject::invokeMethod(&window, "applySourceConfiguration"));
    QCOMPARE(start->value(), 1150.0);
    QCOMPARE(stop->value(), 1250.0);

    start->setValue(900.0);
    stop->setValue(1100.0);
    QVERIFY(QMetaObject::invokeMethod(&window, "applyStartStopConfiguration"));
    QCOMPARE(center->value(), 1000.0);
    QCOMPARE(span->value(), 200.0);
}

void MainWindowTests::zoomAndResetRestoreUserRange()
{
    MainWindow window(std::make_unique<SimulatedSpectrumSource>(), nullptr, false);
    auto* center = window.findChild<QDoubleSpinBox*>(QStringLiteral("centerFrequencyMHz"));
    auto* span = window.findChild<QDoubleSpinBox*>(QStringLiteral("spanMHz"));
    QVERIFY(center && span);
    center->setValue(1000.0);
    span->setValue(200.0);
    QVERIFY(QMetaObject::invokeMethod(&window, "applySourceConfiguration"));

    QVERIFY(QMetaObject::invokeMethod(&window,
                                      "handleSpanScaleRequested",
                                      Q_ARG(double, 0.5),
                                      Q_ARG(double, 1.0e9)));
    QCOMPARE(span->value(), 100.0);
    QVERIFY(QMetaObject::invokeMethod(&window, "resetFrequencyRange"));
    QCOMPARE(center->value(), 1000.0);
    QCOMPARE(span->value(), 200.0);
}

void MainWindowTests::nonFrequencyChangeDoesNotReplaceResetRange()
{
    MainWindow window(std::make_unique<SimulatedSpectrumSource>(), nullptr, false);
    auto* span = window.findChild<QDoubleSpinBox*>(QStringLiteral("spanMHz"));
    auto* noise = window.findChild<QDoubleSpinBox*>(QStringLiteral("noiseDeviationDb"));
    QVERIFY(span && noise);
    span->setValue(200.0);
    QVERIFY(QMetaObject::invokeMethod(&window, "applySourceConfiguration"));
    QVERIFY(QMetaObject::invokeMethod(&window,
                                      "handleSpanScaleRequested",
                                      Q_ARG(double, 0.5),
                                      Q_ARG(double, 1.0e9)));
    QCOMPARE(span->value(), 100.0);

    noise->setValue(2.5);
    QVERIFY(QMetaObject::invokeMethod(&window,
                                      "applyNonFrequencySourceConfiguration"));
    QVERIFY(QMetaObject::invokeMethod(&window, "resetFrequencyRange"));
    QCOMPARE(span->value(), 200.0);
}

void MainWindowTests::boxZoomAndResetRestoreUserRange()
{
    MainWindow window(std::make_unique<SimulatedSpectrumSource>(), nullptr, false);
    auto* center = window.findChild<QDoubleSpinBox*>(QStringLiteral("centerFrequencyMHz"));
    auto* span = window.findChild<QDoubleSpinBox*>(QStringLiteral("spanMHz"));
    QVERIFY(center && span);
    center->setValue(1000.0);
    span->setValue(200.0);
    QVERIFY(QMetaObject::invokeMethod(&window, "applySourceConfiguration"));

    QVERIFY(QMetaObject::invokeMethod(&window,
                                      "handleFrequencyRangeSelected",
                                      Q_ARG(double, 950.0e6),
                                      Q_ARG(double, 1050.0e6)));
    QCOMPARE(span->value(), 100.0);
    QVERIFY(QMetaObject::invokeMethod(&window, "resetFrequencyRange"));
    QCOMPARE(center->value(), 1000.0);
    QCOMPARE(span->value(), 200.0);
}

void MainWindowTests::repeatedExternalSequenceDisplaysNewSessionFrame()
{
    auto source = std::make_unique<FakeSpectrumSource>();
    auto* observer = source.get();
    MainWindow window(std::move(source), nullptr, false);
    auto* plot = window.findChild<SpectrumPlotWidget*>(QStringLiteral("spectrumPlot"));
    QVERIFY(plot);

    window.singleAcquisition();
    auto first = std::make_shared<SpectrumFrame>();
    first->metadata.sequence = 1;
    first->metadata.configurationEpoch = 1;
    first->metadata.binCount = 2;
    first->bins = { -10.0F, -20.0F };
    QVERIFY(observer->deliver(first));
    QVERIFY(QMetaObject::invokeMethod(&window, "refreshDisplay"));
    QCOMPARE(plot->frame()->bins[0], -10.0F);

    window.singleAcquisition();
    auto second = std::make_shared<SpectrumFrame>(*first);
    second->bins[0] = -3.0F;
    QVERIFY(observer->deliver(second));
    QVERIFY(QMetaObject::invokeMethod(&window, "refreshDisplay"));
    QCOMPARE(plot->frame()->bins[0], -3.0F);
}

void MainWindowTests::displaySkipUsesPublicationSequence()
{
    auto source = std::make_unique<FakeSpectrumSource>();
    auto* observer = source.get();
    MainWindow window(std::move(source), nullptr, false);
    auto* skipped = window.findChild<QLabel*>(QStringLiteral("displaySkipped"));
    QVERIFY(skipped);
    window.startAcquisition();

    for (const std::uint64_t sourceSequence : { 1U, 100U }) {
        auto frame = std::make_shared<SpectrumFrame>();
        frame->metadata.sequence = sourceSequence;
        frame->metadata.binCount = 2;
        frame->bins = { -10.0F, -20.0F };
        QVERIFY(observer->deliver(frame));
        QVERIFY(QMetaObject::invokeMethod(&window, "refreshDisplay"));
    }
    QVERIFY(QMetaObject::invokeMethod(&window, "refreshStatistics"));
    QCOMPARE(skipped->text(), QStringLiteral("0 / 2"));
}

void MainWindowTests::singleAcquisitionDisplaysExactlyOneSequence()
{
    MainWindow window(std::make_unique<SimulatedSpectrumSource>(), nullptr, false);
    auto* plot = window.findChild<SpectrumPlotWidget*>(QStringLiteral("spectrumPlot"));
    QVERIFY(plot);
    window.singleAcquisition();
    QTRY_VERIFY_WITH_TIMEOUT(plot->frame() != nullptr, 1000);
    const std::uint64_t sequence = plot->frame()->metadata.sequence;
    QTest::qWait(100);
    QCOMPARE(plot->frame()->metadata.sequence, sequence);
    QCOMPARE(sequence, std::uint64_t(1));
}

void MainWindowTests::fullScreenStateUpdatesButtonText()
{
    MainWindow window(std::make_unique<SimulatedSpectrumSource>(), nullptr, false);
    auto* button = window.findChild<QPushButton*>(QStringLiteral("fullScreenButton"));
    QVERIFY(button);
    window.showFullScreen();
    QTRY_COMPARE_WITH_TIMEOUT(button->text(), QStringLiteral("退出全屏"), 500);
    window.showNormal();
    QTRY_COMPARE_WITH_TIMEOUT(button->text(), QStringLiteral("进入全屏"), 500);
}

void MainWindowTests::fullHdRasterEndToEndPerformance()
{
    MainWindow window(std::make_unique<SimulatedSpectrumSource>(), nullptr, false);
    window.resize(1920, 1080);
    window.show();
    auto* plot = window.findChild<SpectrumPlotWidget*>(QStringLiteral("spectrumPlot"));
    auto* latencyP95 = window.findChild<QLabel*>(QStringLiteral("latencyP95"));
    QVERIFY(plot && latencyP95);
    window.startAcquisition();
    QTRY_VERIFY_WITH_TIMEOUT(plot->frame() != nullptr, 1000);

    QElapsedTimer timer;
    timer.start();
    std::uint64_t lastSequence = plot->frame()->metadata.sequence;
    int distinctDisplayFrames = 0;
    while (timer.elapsed() < 2000) {
        QTest::qWait(16);
        const auto frame = plot->frame();
        if (frame && frame->metadata.sequence != lastSequence) {
            lastSequence = frame->metadata.sequence;
            ++distinctDisplayFrames;
        }
    }
    window.stopAcquisition();

    const double viewFps = static_cast<double>(distinctDisplayFrames) * 1000.0
        / static_cast<double>(timer.elapsed());
    QVERIFY2(viewFps >= 30.0, "Full-HD end-to-end display fell below 30 FPS");
    QVERIFY2(plot->lastPaintMilliseconds() < 100.0,
             "Full-HD CPU Raster paint is unexpectedly slow");
    bool latencyOk = false;
    const double latency = latencyP95->text().left(
        latencyP95->text().size() - 3).toDouble(&latencyOk);
    QVERIFY(latencyOk);
    qInfo().nospace() << "MainWindow 1920x1080 Raster: view=" << viewFps
                      << " FPS, last paint=" << plot->lastPaintMilliseconds()
                      << " ms, painted-visible latency P95=" << latency << " ms";
    QVERIFY2(latency < 150.0,
             "Full-HD continuous visible latency P95 exceeded 150 ms");
}

void MainWindowTests::simulatorControlsUpdateInjectedSource()
{
    auto source = std::make_unique<SimulatedSpectrumSource>();
    auto* sourceObserver = source.get();
    MainWindow window(std::move(source), nullptr, false);
    auto* enabled = window.findChild<QCheckBox*>(QStringLiteral("tone1Enabled"));
    auto* offset = window.findChild<QDoubleSpinBox*>(QStringLiteral("tone1OffsetMHz"));
    auto* amplitude = window.findChild<QDoubleSpinBox*>(QStringLiteral("tone1AmplitudeDbfs"));
    auto* sweepDirection = window.findChild<QComboBox*>(QStringLiteral("sweepDirection"));
    auto* transientDuration = window.findChild<QDoubleSpinBox*>(
        QStringLiteral("transientDurationSeconds"));
    QVERIFY(enabled && offset && amplitude && sweepDirection && transientDuration);

    enabled->setChecked(false);
    offset->setValue(-12.5);
    amplitude->setValue(-44.0);
    sweepDirection->setCurrentIndex(sweepDirection->findData(
        static_cast<int>(SweepDirection::Down)));
    transientDuration->setValue(0.75);
    QVERIFY(QMetaObject::invokeMethod(&window, "applyNonFrequencySourceConfiguration"));
    const SimulationConfig config = sourceObserver->configuration();
    QCOMPARE(config.tones.size(), std::size_t(2));
    QVERIFY(!config.tones[0].enabled);
    QCOMPARE(config.tones[0].frequencyHz, config.centerFrequencyHz - 12.5e6);
    QCOMPARE(config.tones[0].amplitudeDbfs, -44.0F);
    QCOMPARE(static_cast<int>(config.sweepDirection),
             static_cast<int>(SweepDirection::Down));
    QCOMPARE(config.transientDurationSeconds, 0.75);
}

void MainWindowTests::nonSimulationSourceHidesSimulatorControls()
{
    MainWindow window(std::make_unique<FakeSpectrumSource>(), nullptr, false);
    QVERIFY(!window.findChild<QDoubleSpinBox*>(QStringLiteral("noiseDeviationDb")));
    QVERIFY(!window.findChild<QPushButton*>(QStringLiteral("saveScenarioButton")));
    window.startAcquisition();
    window.stopAcquisition();
}

void MainWindowTests::initialScenarioPopulatesControlsAndSource()
{
    auto source = std::make_unique<SimulatedSpectrumSource>();
    auto* sourceObserver = source.get();
    SimulationConfig scenario;
    scenario.binCount = 32768;
    scenario.frameRate = 777.0;
    scenario.unthrottled = true;
    scenario.centerFrequencyHz = 2.4e9;
    scenario.spanHz = 80.0e6;
    scenario.noiseFloorDbfs = -125.0F;
    scenario.noiseDeviationDb = 0.75F;
    scenario.randomSeed = 12345U;
    scenario.tones = {
        ToneConfig { true, 2.39e9, -30.0F, 4.5F },
        ToneConfig { false, 2.42e9, -15.0F, 6.5F }
    };
    scenario.sweepEnabled = true;
    scenario.sweepStartHz = 2.37e9;
    scenario.sweepStopHz = 2.43e9;
    scenario.sweepDirection = SweepDirection::PingPong;
    scenario.sweepPeriodSeconds = 7.0;
    scenario.transientDurationSeconds = 0.4;

    MainWindow window(std::move(source), nullptr, false, &scenario);
    const SimulationConfig applied = sourceObserver->configuration();
    QCOMPARE(applied.binCount, scenario.binCount);
    QCOMPARE(applied.randomSeed, scenario.randomSeed);
    QCOMPARE(applied.unthrottled, true);
    QCOMPARE(applied.tones.size(), std::size_t(2));
    QCOMPARE(applied.tones[0].widthBins, 4.5F);
    QCOMPARE(applied.sweepStartHz, scenario.sweepStartHz);
    QCOMPARE(applied.sweepStopHz, scenario.sweepStopHz);

    auto* unthrottled = window.findChild<QCheckBox*>(QStringLiteral("unthrottled"));
    auto* frameRate = window.findChild<QDoubleSpinBox*>(QStringLiteral("sourceFrameRate"));
    auto* sweepStart = window.findChild<QDoubleSpinBox*>(
        QStringLiteral("sweepStartOffsetMHz"));
    auto* toneWidth = window.findChild<QDoubleSpinBox*>(QStringLiteral("tone1WidthBins"));
    QVERIFY(unthrottled && sweepStart && toneWidth);
    QVERIFY(window.findChild<QPushButton*>(QStringLiteral("saveScenarioButton")));
    QVERIFY(unthrottled->isChecked());
    if (frameRate) {
        QVERIFY(!frameRate->isEnabled());
    }
    QCOMPARE(sweepStart->value(), -30.0);
    QCOMPARE(toneWidth->value(), 4.5);
}

void MainWindowTests::invalidSweepOffsetsAreCorrected()
{
    auto source = std::make_unique<SimulatedSpectrumSource>();
    auto* sourceObserver = source.get();
    MainWindow window(std::move(source), nullptr, false);
    auto* sweepStart = window.findChild<QDoubleSpinBox*>(
        QStringLiteral("sweepStartOffsetMHz"));
    auto* sweepStop = window.findChild<QDoubleSpinBox*>(
        QStringLiteral("sweepStopOffsetMHz"));
    QVERIFY(sweepStart && sweepStop);

    sweepStart->setValue(25.0);
    sweepStop->setValue(10.0);
    QVERIFY(QMetaObject::invokeMethod(&window,
                                      "applyNonFrequencySourceConfiguration"));
    QVERIFY(sweepStop->value() > sweepStart->value());
    const SimulationConfig config = sourceObserver->configuration();
    QVERIFY(config.sweepStopHz > config.sweepStartHz);
}

void MainWindowTests::markerControlsSupportFourMarkersAndDelta()
{
    MainWindow window(std::make_unique<SimulatedSpectrumSource>(), nullptr, false);
    auto* plot = window.findChild<SpectrumPlotWidget*>(QStringLiteral("spectrumPlot"));
    auto* active = window.findChild<QComboBox*>(QStringLiteral("activeMarker"));
    auto* threshold = window.findChild<QDoubleSpinBox*>(QStringLiteral("peakThreshold"));
    auto* delta = window.findChild<QCheckBox*>(QStringLiteral("deltaMarker"));
    QVERIFY(plot && active && threshold && delta);
    QCOMPARE(active->count(), 4);

    window.singleAcquisition();
    QTRY_VERIFY_WITH_TIMEOUT(plot->frame() != nullptr, 1000);
    threshold->setValue(-180.0);
    active->setCurrentIndex(0);
    plot->peakSearch();
    QVERIFY(plot->markerBin(0).has_value());
    active->setCurrentIndex(1);
    plot->peakSearch();
    plot->nextPeak();
    QVERIFY(plot->markerBin(1).has_value());
    QVERIFY(plot->markerBin(0).has_value());
    delta->setChecked(true);
    QVERIFY(plot->deltaMarkerMeasurement().valid);
}

void MainWindowTests::rangeMeasurementsUseLatestFullFrame()
{
    MainWindow window(std::make_unique<SimulatedSpectrumSource>(), nullptr, false);
    auto* plot = window.findChild<SpectrumPlotWidget*>(QStringLiteral("spectrumPlot"));
    auto* start = window.findChild<QDoubleSpinBox*>(QStringLiteral("measurementStartMHz"));
    auto* stop = window.findChild<QDoubleSpinBox*>(QStringLiteral("measurementStopMHz"));
    auto* result = window.findChild<QLabel*>(QStringLiteral("measurementResult"));
    QVERIFY(plot && start && stop && result);

    window.singleAcquisition();
    QTRY_VERIFY_WITH_TIMEOUT(plot->frame() != nullptr, 1000);
    start->setValue(900.0);
    stop->setValue(1100.0);
    QVERIFY(QMetaObject::invokeMethod(&window, "measureRangePeak"));
    QVERIFY(result->text().contains(QStringLiteral("峰值")));
    QVERIFY(result->text().contains(QStringLiteral("dBFS")));
    QVERIFY(result->text().contains(QStringLiteral("未校准")));

    QVERIFY(QMetaObject::invokeMethod(&window, "measureChannelPower"));
    QVERIFY(result->text().contains(QStringLiteral("信道功率")));
    QVERIFY(result->text().contains(QStringLiteral("dBFS")));
    QVERIFY(result->text().contains(QStringLiteral("未校准")));
}

void MainWindowTests::telemetryShowsProcessingRenderAndQueueDepth()
{
    MainWindow window(std::make_unique<SimulatedSpectrumSource>(), nullptr, false);
    window.show();
    auto* processing = window.findChild<QLabel*>(QStringLiteral("processingTime"));
    auto* render = window.findChild<QLabel*>(QStringLiteral("renderTime"));
    auto* queue = window.findChild<QLabel*>(QStringLiteral("queueDepth"));
    auto* latencyP95 = window.findChild<QLabel*>(QStringLiteral("latencyP95"));
    QVERIFY(processing && render && queue && latencyP95);
    window.singleAcquisition();
    QTRY_VERIFY_WITH_TIMEOUT(latencyP95->text().endsWith(QStringLiteral(" ms")), 1000);
    QVERIFY(processing->text().endsWith(QStringLiteral(" ms")));
    QVERIFY(render->text().endsWith(QStringLiteral(" ms")));
    QCOMPARE(queue->text(), QStringLiteral("0"));
    bool latencyOk = false;
    const double latency = latencyP95->text().left(
        latencyP95->text().size() - 3).toDouble(&latencyOk);
    QVERIFY(latencyOk);
    QVERIFY2(latency < 150.0, "single-frame visible latency P95 exceeded 150 ms");
}

} // namespace rtsa

QTEST_MAIN(rtsa::MainWindowTests)

#include "tst_MainWindow.moc"
