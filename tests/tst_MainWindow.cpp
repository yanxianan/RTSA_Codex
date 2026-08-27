#include "plot/SpectrumPlotWidget.h"
#include "sources/SimulatedSpectrumSource.h"
#include "ui/ApplicationTheme.h"
#include "ui/FrequencySpinBox.h"
#include "ui/MainWindow.h"

#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QTableWidget>
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
    void canvasSingleClickDoesNotPlaceMarkers();
    void telemetryShowsProcessingRenderAndQueueDepth();
    void unfocusedInputsIgnoreMouseWheelToPreventAccidentalChanges();
    void frequencySpinBoxSeparatesValueAndUnitWithAutoScaling();
    void frequencySpinBoxStepsDownFromKhzToHzBelowOneKilohertz();
    void displayMenuProvidesThemesTraceColorsLineWidthAndGrid();
    void displayAppearanceCustomizationsApplyToSpectrumPlot();
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
        QTest::qWait(5);
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
    auto* frequency = window.findChild<QDoubleSpinBox*>(QStringLiteral("tone1FrequencyMHz"));
    auto* amplitude = window.findChild<QDoubleSpinBox*>(QStringLiteral("tone1AmplitudeDbfs"));
    auto* sweepDirection = window.findChild<QComboBox*>(QStringLiteral("sweepDirection"));
    auto* transientDuration = window.findChild<QDoubleSpinBox*>(
        QStringLiteral("transientDurationSeconds"));
    QVERIFY(enabled && frequency && amplitude && sweepDirection && transientDuration);

    enabled->setChecked(false);
    frequency->setValue(987.5);
    amplitude->setValue(-44.0);
    sweepDirection->setCurrentIndex(sweepDirection->findData(
        static_cast<int>(SweepDirection::Down)));
    transientDuration->setValue(0.75);
    QVERIFY(QMetaObject::invokeMethod(&window, "applyNonFrequencySourceConfiguration"));
    const SimulationConfig config = sourceObserver->configuration();
    QCOMPARE(config.tones.size(), std::size_t(2));
    QVERIFY(!config.tones[0].enabled);
    QCOMPARE(config.tones[0].frequencyHz, 987.5e6);
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
        ToneConfig { true, 2.39e9, -30.0F, 4.5e6 },
        ToneConfig { false, 2.42e9, -15.0F, 6.5e6 }
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
    QCOMPARE(applied.tones[0].widthHz, 4.5e6);
    QCOMPARE(applied.sweepStartHz, scenario.sweepStartHz);
    QCOMPARE(applied.sweepStopHz, scenario.sweepStopHz);

    auto* unthrottled = window.findChild<QCheckBox*>(QStringLiteral("unthrottled"));
    auto* frameRate = window.findChild<QDoubleSpinBox*>(QStringLiteral("sourceFrameRate"));
    auto* sweepStart = window.findChild<QDoubleSpinBox*>(
        QStringLiteral("sweepStartFrequencyMHz"));
    auto* toneWidth = window.findChild<QDoubleSpinBox*>(QStringLiteral("tone1WidthMHz"));
    QVERIFY(unthrottled && sweepStart && toneWidth);
    QVERIFY(window.findChild<QPushButton*>(QStringLiteral("saveScenarioButton")));
    QVERIFY(unthrottled->isChecked());
    if (frameRate) {
        QVERIFY(!frameRate->isEnabled());
    }
    QCOMPARE(sweepStart->value(), 2370.0);
    QCOMPARE(toneWidth->value(), 4.5);
}

void MainWindowTests::invalidSweepOffsetsAreCorrected()
{
    auto source = std::make_unique<SimulatedSpectrumSource>();
    auto* sourceObserver = source.get();
    MainWindow window(std::move(source), nullptr, false);
    auto* sweepStart = window.findChild<QDoubleSpinBox*>(
        QStringLiteral("sweepStartFrequencyMHz"));
    auto* sweepStop = window.findChild<QDoubleSpinBox*>(
        QStringLiteral("sweepStopFrequencyMHz"));
    QVERIFY(sweepStart && sweepStop);

    sweepStart->setValue(1050.0);
    sweepStop->setValue(950.0);
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
    auto* markerEnabled = window.findChild<QCheckBox*>(QStringLiteral("markerEnabled"));
    auto* markerFreq = window.findChild<FrequencySpinBox*>(QStringLiteral("markerFrequencyMHz"));
    auto* markerTable = window.findChild<QTableWidget*>(QStringLiteral("markerTable"));
    auto* markerLabel = window.findChild<QLabel*>(QStringLiteral("markerLabel"));
    auto* deltaLabel = window.findChild<QLabel*>(QStringLiteral("activeMarkerDeltaLabel"));
    auto* mToCf = window.findChild<QPushButton*>(QStringLiteral("markerToCenterButton"));
    auto* center = window.findChild<FrequencySpinBox*>(QStringLiteral("centerFrequencyMHz"));

    QVERIFY(plot && active && threshold && markerEnabled && markerFreq && markerTable && markerLabel && deltaLabel && mToCf && center);
    QCOMPARE(active->count(), 4);
    QCOMPARE(markerTable->rowCount(), 4);

    window.singleAcquisition();
    QTRY_VERIFY_WITH_TIMEOUT(plot->frame() != nullptr, 1000);
    threshold->setValue(-180.0);

    // M1 Peak Search
    active->setCurrentIndex(0);
    plot->peakSearch();
    QVERIFY(plot->markerBin(0).has_value());

    // M2 Peak Search & Next Peak
    active->setCurrentIndex(1);
    plot->peakSearch();
    plot->nextPeak();
    QVERIFY(plot->markerBin(1).has_value());
    QVERIFY(plot->markerBin(0).has_value());

    // Delta measurement is automatically valid without any checkbox
    QVERIFY(plot->deltaMarkerMeasurement().valid);
    QVERIFY(deltaLabel->text().contains(QStringLiteral("Δ(M2 - M1)")));
    QVERIFY(markerTable->item(1, 3)->text().contains(QStringLiteral("ΔF:")));

    // Test Marker -> Center Frequency (M->CF)
    const double m2Freq = plot->markerMeasurement(1).frequencyHz;
    mToCf->click();
    QCOMPARE(center->frequencyHz(), m2Freq);
}

void MainWindowTests::canvasSingleClickDoesNotPlaceMarkers()
{
    MainWindow window(std::make_unique<SimulatedSpectrumSource>(), nullptr, false);
    auto* plot = window.findChild<SpectrumPlotWidget*>(QStringLiteral("spectrumPlot"));
    QVERIFY(plot);

    window.singleAcquisition();
    QTRY_VERIFY_WITH_TIMEOUT(plot->frame() != nullptr, 1000);
    plot->clearAllMarkers();
    for (std::size_t i = 0; i < kSpectrumMarkerCount; ++i) {
        QVERIFY(!plot->markerBin(i).has_value());
    }

    // Single click on canvas center
    const QPoint centerPoint = plot->rect().center();
    QTest::mouseClick(plot, Qt::LeftButton, Qt::NoModifier, centerPoint);

    // Verify canvas click did NOT create/trigger any marker
    for (std::size_t i = 0; i < kSpectrumMarkerCount; ++i) {
        QVERIFY(!plot->markerBin(i).has_value());
    }

    // Now enable M1 through API / Peak Search
    plot->peakSearch();
    QVERIFY(plot->markerBin(0).has_value());
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

void MainWindowTests::unfocusedInputsIgnoreMouseWheelToPreventAccidentalChanges()
{
    MainWindow window(std::make_unique<SimulatedSpectrumSource>(), nullptr, false);
    window.show();
    auto* centerSpin = window.findChild<QDoubleSpinBox*>(QStringLiteral("centerFrequencyMHz"));
    auto* fftCombo = window.findChild<QComboBox*>(QStringLiteral("fftSize"));
    QVERIFY(centerSpin && fftCombo);

    window.setFocus();
    QVERIFY(!centerSpin->hasFocus());
    QVERIFY(!fftCombo->hasFocus());

    const double initialCenter = centerSpin->value();
    const int initialFftIndex = fftCombo->currentIndex();

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QWheelEvent wheelEvent(QPointF(10, 10), QPointF(10, 10), QPoint(0, 0), QPoint(0, 120),
                           Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
#else
    QWheelEvent wheelEvent(QPoint(10, 10), 120, Qt::NoButton, Qt::NoModifier);
#endif

    // 1. Unfocused spinbox in main window ignores wheel
    QApplication::sendEvent(centerSpin, &wheelEvent);
    QCOMPARE(centerSpin->value(), initialCenter);

    // 2. Unfocused combobox in main window ignores wheel
    QApplication::sendEvent(fftCombo, &wheelEvent);
    QCOMPARE(fftCombo->currentIndex(), initialFftIndex);
}

void MainWindowTests::frequencySpinBoxSeparatesValueAndUnitWithAutoScaling()
{
    FrequencySpinBox spin;
    spin.setFrequencyRangeHz(0.0, 20.0e9);

    // Initial default in MHz
    spin.setFrequencyHz(1000.0e6);
    QCOMPARE(spin.value(), 1000.0);
    QCOMPARE(spin.unit(), FrequencySpinBox::Unit::MHz);
    QCOMPARE(spin.unitComboBox()->currentText(), QStringLiteral("MHz"));

    // Changing dropdown to GHz preserves physical frequency and changes value to 1.0
    spin.setUnit(FrequencySpinBox::Unit::GHz);
    QCOMPARE(spin.unit(), FrequencySpinBox::Unit::GHz);
    QCOMPARE(spin.value(), 1.0);
    QCOMPARE(spin.frequencyHz(), 1000.0e6);

    // Compound widget layout test
    QWidget* compound = spin.createCompoundWidget();
    QVERIFY(compound);
    QVERIFY(spin.parentWidget() == compound);
    QVERIFY(spin.unitComboBox()->parentWidget() == compound);
}

void MainWindowTests::frequencySpinBoxStepsDownFromKhzToHzBelowOneKilohertz()
{
    FrequencySpinBox spin;
    spin.setFrequencyRangeHz(0.0, 20.0e9);

    // Set to 5 kHz
    spin.setFrequencyHz(5.0e3);
    QCOMPARE(spin.unit(), FrequencySpinBox::Unit::kHz);
    QCOMPARE(spin.value(), 5.0);
    QCOMPARE(spin.unitComboBox()->currentText(), QStringLiteral("kHz"));

    // Step down 1: 5 kHz -> 4 kHz
    spin.stepBy(-1);
    QCOMPARE(spin.unit(), FrequencySpinBox::Unit::kHz);
    QCOMPARE(spin.value(), 4.0);

    // Step down 2: 4 kHz -> 3 kHz
    spin.stepBy(-1);
    QCOMPARE(spin.unit(), FrequencySpinBox::Unit::kHz);
    QCOMPARE(spin.value(), 3.0);

    // Step down 3: 3 kHz -> 2 kHz
    spin.stepBy(-1);
    QCOMPARE(spin.unit(), FrequencySpinBox::Unit::kHz);
    QCOMPARE(spin.value(), 2.0);

    // Step down 4: 2 kHz -> 1 kHz
    spin.stepBy(-1);
    QCOMPARE(spin.unit(), FrequencySpinBox::Unit::kHz);
    QCOMPARE(spin.value(), 1.0);

    // Step down 5: When decreasing below 1 kHz, unit automatically switches to Hz and value starts from 1000 downwards (999 Hz)
    spin.stepBy(-1);
    QCOMPARE(spin.unit(), FrequencySpinBox::Unit::Hz);
    QCOMPARE(spin.unitComboBox()->currentText(), QStringLiteral("Hz"));
    QCOMPARE(spin.value(), 999.0);
    QCOMPARE(spin.frequencyHz(), 999.0);

    // Continue decreasing in Hz
    spin.stepBy(-1);
    QCOMPARE(spin.unit(), FrequencySpinBox::Unit::Hz);
    QCOMPARE(spin.value(), 998.0);
    QCOMPARE(spin.frequencyHz(), 998.0);

    // Step up back to 1000 Hz: auto-switches back to kHz (1.000 kHz)
    spin.stepBy(1); // 999 Hz
    QCOMPARE(spin.value(), 999.0);
    spin.stepBy(1); // 1000 Hz -> switches to kHz
    QCOMPARE(spin.unit(), FrequencySpinBox::Unit::kHz);
    QCOMPARE(spin.unitComboBox()->currentText(), QStringLiteral("kHz"));
    QCOMPARE(spin.value(), 1.0);

    // Also test MHz to kHz boundary: at 1.0 MHz, step down becomes 999.0 kHz
    spin.setFrequencyHz(1.0e6);
    QCOMPARE(spin.unit(), FrequencySpinBox::Unit::MHz);
    QCOMPARE(spin.value(), 1.0);
    spin.stepBy(-1);
    QCOMPARE(spin.unit(), FrequencySpinBox::Unit::kHz);
    QCOMPARE(spin.unitComboBox()->currentText(), QStringLiteral("kHz"));
    QCOMPARE(spin.value(), 999.0);

    // Also test GHz to MHz boundary: at 1.0 GHz, step down becomes 999.0 MHz
    spin.setUnit(FrequencySpinBox::Unit::GHz);
    spin.setValue(1.0);
    QCOMPARE(spin.unit(), FrequencySpinBox::Unit::GHz);
    QCOMPARE(spin.value(), 1.0);
    spin.stepBy(-1);
    QCOMPARE(spin.unit(), FrequencySpinBox::Unit::MHz);
    QCOMPARE(spin.unitComboBox()->currentText(), QStringLiteral("MHz"));
    QCOMPARE(spin.value(), 999.0);
}

void MainWindowTests::displayMenuProvidesThemesTraceColorsLineWidthAndGrid()
{
    MainWindow window(std::make_unique<SimulatedSpectrumSource>(), nullptr, false);
    auto* bar = window.menuBar();
    QVERIFY(bar);

    QMenu* displayMenu = nullptr;
    for (auto* action : bar->actions()) {
        if (action->text().contains(QStringLiteral("显示"))) {
            displayMenu = action->menu();
            break;
        }
    }
    QVERIFY2(displayMenu != nullptr, "Display menu '显示 (&D)' must be present on the top menu bar");

    // Check submenus and actions within displayMenu
    bool foundViewMode = false;
    bool foundThemes = false;
    bool foundTraceColors = false;
    bool foundLineWidth = false;
    bool foundGrid = false;
    bool foundColormap = false;

    for (auto* action : displayMenu->actions()) {
        const QString text = action->text();
        if (text.contains(QStringLiteral("视图模式"))) {
            foundViewMode = true;
            QVERIFY(action->menu());
            QCOMPARE(action->menu()->actions().size(), 3);
        } else if (text.contains(QStringLiteral("绘图区主题"))) {
            foundThemes = true;
            QVERIFY(action->menu());
            QVERIFY(action->menu()->actions().size() >= 5);
        } else if (text.contains(QStringLiteral("曲线颜色"))) {
            foundTraceColors = true;
            QVERIFY(action->menu());
            QVERIFY(action->menu()->actions().size() >= 7);
        } else if (text.contains(QStringLiteral("曲线线宽"))) {
            foundLineWidth = true;
            QVERIFY(action->menu());
            QCOMPARE(action->menu()->actions().size(), 4);
        } else if (text.contains(QStringLiteral("网格"))) {
            foundGrid = true;
            QVERIFY(action->isCheckable());
            QVERIFY(action->isChecked());
        } else if (text.contains(QStringLiteral("瀑布图色图"))) {
            foundColormap = true;
            QVERIFY(action->menu());
            QCOMPARE(action->menu()->actions().size(), 5);
        }
    }

    QVERIFY(foundViewMode);
    QVERIFY(foundThemes);
    QVERIFY(foundTraceColors);
    QVERIFY(foundLineWidth);
    QVERIFY(foundGrid);
    QVERIFY(foundColormap);
}

void MainWindowTests::displayAppearanceCustomizationsApplyToSpectrumPlot()
{
    MainWindow window(std::make_unique<SimulatedSpectrumSource>(), nullptr, false);
    auto* plot = window.findChild<SpectrumPlotWidget*>(QStringLiteral("spectrumPlot"));
    QVERIFY(plot);

    // 1. Theme switching
    window.setPlotTheme(2); // Deep Navy
    window.setPlotTheme(1); // Light
    window.setPlotTheme(3); // Pitch Black
    window.setPlotTheme(0); // Classic Dark

    // 2. Trace color preset switching
    window.setTraceColorPreset(1); // Cyan Blue
    window.setTraceColorPreset(4); // Industrial Orange
    window.setTraceColorPreset(0); // Emerald Green

    // 3. Line width switching
    window.setTraceLineWidth(3);
    window.setTraceLineWidth(1);

    // 4. Grid visibility toggling
    window.setGridVisible(false);
    window.setGridVisible(true);

    // 5. View mode switching
    window.setDisplayViewMode(0); // Spectrum Only
    QVERIFY(!plot->isHidden());
    window.setDisplayViewMode(1); // Waterfall Only
    QVERIFY(plot->isHidden());
    window.setDisplayViewMode(2); // Dual View
    QVERIFY(!plot->isHidden());
}

} // namespace rtsa

QTEST_MAIN(rtsa::MainWindowTests)

#include "tst_MainWindow.moc"
