#include "core/FrequencyMapper.h"
#include "core/SpectrumPipeline.h"
#include "sources/SimulatedSpectrumSource.h"
#include "sources/SpectrumSourceFactory.h"

#include <QtTest>
#include <QElapsedTimer>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace rtsa {

class SimulatedSourceTests final : public QObject {
    Q_OBJECT

private slots:
    void beginsInitializedAndTransitionsThroughLifecycle();
    void lifecycleSignalsCarryStatesAndErrors();
    void producesConsistentFramesWithVisibleTone();
    void pauseAndResumeChangeState();
    void interfaceAndStopRemainResponsiveAtLowRate();
    void highRateInputSupportsIndependentThirtyFpsView();
    void workerFailureCanBeStoppedAndRestarted();
    void singleAcquisitionPublishesExactlyOneFrame();
    void toneRespectsHalfOpenSpanBoundaries_data();
    void toneRespectsHalfOpenSpanBoundaries();
    void sweepDirectionControlsInitialFrequency_data();
    void sweepDirectionControlsInitialFrequency();
    void transientDurationKeepsFrequencyStable();
    void sourceFactoryParsesAndRejectsUnavailableDma();
    void unthrottledModeDoesNotWaitForConfiguredRate();
    void fixedSeedProducesIdenticalBins();
    void faultInjectionCreatesSequenceGapsAndInvalidFrames();
    void injectedDataPauseIsObservableAndInterruptible();
    void injectedBurstProducesBackToBackFrames();
    void toneWidthChangesOccupiedBins();
    void equalTonesCombineInLinearPower();
    void pingPongSweepMovesInBothDirections();
    void intervalRatesResetWhenNotRunning();
};

void SimulatedSourceTests::beginsInitializedAndTransitionsThroughLifecycle()
{
    SimulatedSpectrumSource source;
    QCOMPARE(source.state(), SourceState::Initialized);
    source.setFrameSink([](const SpectrumFramePtr&) { return true; });

    QVERIFY(source.start());
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), SourceState::Running, 500);
    source.stop();
    QCOMPARE(source.state(), SourceState::Stopped);
}

void SimulatedSourceTests::lifecycleSignalsCarryStatesAndErrors()
{
    SimulatedSpectrumSource source;
    QSignalSpy stateSpy(&source, &ISpectrumSource::stateChanged);
    QSignalSpy errorSpy(&source, &ISpectrumSource::errorOccurred);
    source.setFrameSink([](const SpectrumFramePtr&) { return true; });

    QVERIFY(source.start());
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), SourceState::Running, 500);
    QTRY_VERIFY_WITH_TIMEOUT(stateSpy.count() >= 2, 500);
    source.pause();
    source.resume();
    source.stop();
    QTRY_VERIFY_WITH_TIMEOUT(stateSpy.count() >= 5, 500);
    QCOMPARE(stateSpy.at(0).at(0).toInt(), static_cast<int>(SourceState::Starting));
    QCOMPARE(stateSpy.at(1).at(0).toInt(), static_cast<int>(SourceState::Running));
    QCOMPARE(stateSpy.at(2).at(0).toInt(), static_cast<int>(SourceState::Paused));
    QCOMPARE(stateSpy.at(3).at(0).toInt(), static_cast<int>(SourceState::Running));
    QCOMPARE(stateSpy.at(4).at(0).toInt(), static_cast<int>(SourceState::Stopped));

    source.setFrameSink([](const SpectrumFramePtr&) -> bool {
        throw std::runtime_error("signal-payload-marker");
    });
    QVERIFY(source.start());
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), SourceState::Error, 500);
    QTRY_COMPARE_WITH_TIMEOUT(errorSpy.count(), 1, 500);
    QVERIFY(errorSpy.first().at(0).toString().contains(
        QStringLiteral("signal-payload-marker")));
    source.pause();
    source.resume();
    QCOMPARE(source.state(), SourceState::Error);
    source.stop();
}

void SimulatedSourceTests::producesConsistentFramesWithVisibleTone()
{
    SpectrumPipeline pipeline;
    SimulatedSpectrumSource source;
    SimulationConfig config;
    config.binCount = 4096;
    config.frameRate = 200.0;
    config.centerFrequencyHz = 100.0e6;
    config.spanHz = 20.0e6;
    config.noiseFloorDbfs = -120.0F;
    config.noiseDeviationDb = 0.2F;
    config.sweepEnabled = false;
    config.transientProbability = 0.0F;
    config.tones = { ToneConfig { true, 103.0e6, -10.0F, 1.0F } };

    source.configure(config);
    source.setFrameSink([&pipeline](const SpectrumFramePtr& frame) {
        return pipeline.submit(frame);
    });
    QVERIFY(source.start());
    QTRY_VERIFY_WITH_TIMEOUT(pipeline.statistics().publishedFrames >= 10, 1000);
    source.stop();

    const auto frame = pipeline.latest();
    QVERIFY(frame);
    QVERIFY(frame->isConsistent());
    QCOMPARE(frame->bins.size(), std::size_t(4096));
    const auto peak = std::max_element(frame->bins.cbegin(), frame->bins.cend());
    QVERIFY(peak != frame->bins.cend());
    const auto peakBin = static_cast<std::size_t>(std::distance(frame->bins.cbegin(), peak));
    const double peakHz = FrequencyMapper::frequencyForBin(frame->metadata, peakBin);
    QVERIFY(std::abs(peakHz - 103.0e6) <= FrequencyMapper::binWidthHz(frame->metadata));
    QVERIFY(*peak > -11.0F);
}

void SimulatedSourceTests::pauseAndResumeChangeState()
{
    SimulatedSpectrumSource source;
    source.setFrameSink([](const SpectrumFramePtr&) { return true; });
    QVERIFY(source.start());
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), SourceState::Running, 500);
    for (int iteration = 0; iteration < 50; ++iteration) {
        source.pause();
        QCOMPARE(source.state(), SourceState::Paused);
        QTest::qWait(1);
        source.resume();
        QCOMPARE(source.state(), SourceState::Running);
    }
    source.stop();
    QCOMPARE(source.state(), SourceState::Stopped);
}

void SimulatedSourceTests::interfaceAndStopRemainResponsiveAtLowRate()
{
    SimulatedSpectrumSource concrete;
    ISpectrumSource* source = &concrete;
    SimulationConfig config;
    config.frameRate = 1.0;
    concrete.configure(config);
    source->setFrameSink([](const SpectrumFramePtr&) { return true; });
    QVERIFY(source->start());
    QTRY_VERIFY_WITH_TIMEOUT(source->statistics().producedFrames >= 1, 500);

    QElapsedTimer timer;
    timer.start();
    source->stop();
    QVERIFY2(timer.elapsed() < 250, "stop() waited for the one-second frame period");
    QCOMPARE(source->state(), SourceState::Stopped);
}

void SimulatedSourceTests::highRateInputSupportsIndependentThirtyFpsView()
{
    SpectrumPipeline pipeline;
    SimulatedSpectrumSource source;
    SimulationConfig config;
    config.binCount = 16384;
    config.frameRate = 1000.0;
    config.sweepEnabled = true;
    config.transientProbability = 0.002F;
    source.configure(config);
    source.setFrameSink([&pipeline](const SpectrumFramePtr& frame) {
        return pipeline.submit(frame);
    });
    QVERIFY(source.start());

    QElapsedTimer timer;
    timer.start();
    std::uint64_t lastSequence = 0;
    int distinctViewFrames = 0;
    while (timer.elapsed() < 1500) {
        QTest::qWait(16);
        const auto latest = pipeline.latest();
        if (latest && latest->metadata.sequence != lastSequence) {
            lastSequence = latest->metadata.sequence;
            ++distinctViewFrames;
        }
    }
    source.stop();

    const auto statistics = source.statistics();
    const double viewFps = static_cast<double>(distinctViewFrames) * 1000.0
        / static_cast<double>(timer.elapsed());
    qInfo().nospace() << "16,384 points @ requested 1,000 FPS: produced="
                      << statistics.actualFrameRate << " FPS, sampled view="
                      << viewFps << " FPS, data=" << statistics.bytesPerSecond / 1.0e6
                      << " MB/s";
#ifdef NDEBUG
    QVERIFY2(statistics.actualFrameRate >= 900.0,
             "Release simulated source fell more than 10% below requested 1,000 FPS");
#endif
    QVERIFY2(viewFps >= 30.0, "Latest-frame display sampling fell below 30 FPS");
    QVERIFY(statistics.producedFrames > 0);
}

void SimulatedSourceTests::workerFailureCanBeStoppedAndRestarted()
{
    SimulatedSpectrumSource source;
    source.setFrameSink([](const SpectrumFramePtr&) -> bool {
        throw std::runtime_error("injected sink failure");
    });
    QVERIFY(source.start());
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), SourceState::Error, 500);

    source.stop();
    QCOMPARE(source.state(), SourceState::Stopped);
    source.setFrameSink([](const SpectrumFramePtr&) { return true; });
    QVERIFY(source.start());
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), SourceState::Running, 500);
    source.stop();
}

void SimulatedSourceTests::singleAcquisitionPublishesExactlyOneFrame()
{
    SpectrumPipeline pipeline;
    SimulatedSpectrumSource source;
    source.setFrameSink([&pipeline](const SpectrumFramePtr& frame) {
        return pipeline.submit(frame);
    });

    QVERIFY(source.start(AcquisitionMode::SingleFrame));
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), SourceState::Stopped, 500);
    QCOMPARE(source.statistics().producedFrames, std::uint64_t(1));
    QCOMPARE(pipeline.statistics().submittedFrames, std::uint64_t(1));
    QCOMPARE(pipeline.statistics().publishedFrames, std::uint64_t(1));
    source.stop();
}

void SimulatedSourceTests::toneRespectsHalfOpenSpanBoundaries_data()
{
    QTest::addColumn<double>("frequencyHz");
    QTest::addColumn<bool>("visible");
    QTest::newRow("below-start") << 89.0e6 << false;
    QTest::newRow("exact-start") << 90.0e6 << true;
    QTest::newRow("exact-stop") << 110.0e6 << false;
    QTest::newRow("above-stop") << 111.0e6 << false;
}

void SimulatedSourceTests::toneRespectsHalfOpenSpanBoundaries()
{
    QFETCH(double, frequencyHz);
    QFETCH(bool, visible);

    SpectrumPipeline pipeline;
    SimulatedSpectrumSource source;
    SimulationConfig config;
    config.binCount = 1024;
    config.centerFrequencyHz = 100.0e6;
    config.spanHz = 20.0e6;
    config.noiseFloorDbfs = -150.0F;
    config.noiseDeviationDb = 0.0F;
    config.sweepEnabled = false;
    config.transientProbability = 0.0F;
    config.tones = { ToneConfig { true, frequencyHz, -10.0F, 0.5F } };
    source.configure(config);
    source.setFrameSink([&pipeline](const SpectrumFramePtr& frame) {
        return pipeline.submit(frame);
    });

    QVERIFY(source.start(AcquisitionMode::SingleFrame));
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), SourceState::Stopped, 500);
    const auto frame = pipeline.latest();
    QVERIFY(frame);
    const float peak = *std::max_element(frame->bins.cbegin(), frame->bins.cend());
    if (visible) {
        QVERIFY(peak > -11.0F);
    } else {
        QCOMPARE(peak, -150.0F);
    }
    source.stop();
}

void SimulatedSourceTests::sweepDirectionControlsInitialFrequency_data()
{
    QTest::addColumn<int>("direction");
    QTest::addColumn<double>("expectedFrequencyHz");
    QTest::newRow("up") << static_cast<int>(SweepDirection::Up) << 95.0e6;
    QTest::newRow("down") << static_cast<int>(SweepDirection::Down) << 105.0e6;
    QTest::newRow("ping-pong") << static_cast<int>(SweepDirection::PingPong) << 95.0e6;
}

void SimulatedSourceTests::sweepDirectionControlsInitialFrequency()
{
    QFETCH(int, direction);
    QFETCH(double, expectedFrequencyHz);

    SpectrumPipeline pipeline;
    SimulatedSpectrumSource source;
    SimulationConfig config;
    config.binCount = 4096;
    config.centerFrequencyHz = 100.0e6;
    config.spanHz = 20.0e6;
    config.noiseFloorDbfs = -150.0F;
    config.noiseDeviationDb = 0.0F;
    config.tones.clear();
    config.sweepEnabled = true;
    config.sweepStartHz = 95.0e6;
    config.sweepStopHz = 105.0e6;
    config.sweepPeriodSeconds = 10.0;
    config.sweepAmplitudeDbfs = -10.0F;
    config.sweepDirection = static_cast<SweepDirection>(direction);
    config.transientProbability = 0.0F;
    source.configure(config);
    source.setFrameSink([&pipeline](const SpectrumFramePtr& frame) {
        return pipeline.submit(frame);
    });

    QVERIFY(source.start(AcquisitionMode::SingleFrame));
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), SourceState::Stopped, 500);
    const auto frame = pipeline.latest();
    QVERIFY(frame);
    const auto peak = std::max_element(frame->bins.cbegin(), frame->bins.cend());
    const auto peakBin = static_cast<std::size_t>(
        std::distance(frame->bins.cbegin(), peak));
    const double peakFrequencyHz = FrequencyMapper::frequencyForBin(
        frame->metadata, peakBin);
    QVERIFY(std::abs(peakFrequencyHz - expectedFrequencyHz)
            <= FrequencyMapper::binWidthHz(frame->metadata) * 2.0);
    source.stop();
}

void SimulatedSourceTests::transientDurationKeepsFrequencyStable()
{
    SimulatedSpectrumSource source;
    SimulationConfig config;
    config.binCount = 1024;
    config.frameRate = 200.0;
    config.noiseFloorDbfs = -150.0F;
    config.noiseDeviationDb = 0.0F;
    config.tones.clear();
    config.sweepEnabled = false;
    config.transientProbability = 1.0F;
    config.transientAmplitudeDbfs = -10.0F;
    config.transientDurationSeconds = 1.0;
    source.configure(config);

    std::vector<std::size_t> peakBins;
    source.setFrameSink([&peakBins](const SpectrumFramePtr& frame) {
        if ((frame->metadata.flags & 0x1U) == 0U) {
            return false;
        }
        const auto peak = std::max_element(frame->bins.cbegin(), frame->bins.cend());
        peakBins.push_back(static_cast<std::size_t>(
            std::distance(frame->bins.cbegin(), peak)));
        return true;
    });

    QVERIFY(source.start());
    QTRY_VERIFY_WITH_TIMEOUT(source.statistics().producedFrames >= 5, 500);
    source.stop();
    QVERIFY(peakBins.size() >= 5U);
    const std::size_t firstPeak = peakBins.front();
    QVERIFY(std::all_of(peakBins.cbegin(), peakBins.cend(), [firstPeak](const std::size_t bin) {
        return bin == firstPeak;
    }));
}

void SimulatedSourceTests::sourceFactoryParsesAndRejectsUnavailableDma()
{
    SpectrumSourceKind kind = SpectrumSourceKind::Dma;
    QString error;
    QVERIFY(parseSpectrumSourceKind(QStringLiteral(" simulated "), kind, error));
    QCOMPARE(static_cast<int>(kind), static_cast<int>(SpectrumSourceKind::Simulated));
    QVERIFY(error.isEmpty());
    QVERIFY(createSpectrumSource(kind).source);

    QVERIFY(parseSpectrumSourceKind(QStringLiteral("DMA"), kind, error));
    QCOMPARE(static_cast<int>(kind), static_cast<int>(SpectrumSourceKind::Dma));
    SourceCreationResult dma = createSpectrumSource(kind);
    QVERIFY(!dma.source);
    QVERIFY(dma.errorMessage.contains(QStringLiteral("DMA")));

    QVERIFY(!parseSpectrumSourceKind(QStringLiteral("unknown"), kind, error));
    QVERIFY(!error.isEmpty());
}

void SimulatedSourceTests::unthrottledModeDoesNotWaitForConfiguredRate()
{
    SimulatedSpectrumSource source;
    SimulationConfig config;
    config.binCount = 1024;
    config.frameRate = 1.0;
    config.unthrottled = true;
    config.sweepEnabled = false;
    config.transientProbability = 0.0F;
    source.configure(config);
    source.setFrameSink([](const SpectrumFramePtr&) { return true; });

    QVERIFY(source.start());
    QTRY_VERIFY_WITH_TIMEOUT(source.statistics().producedFrames >= 20, 500);
    QElapsedTimer stopTimer;
    stopTimer.start();
    source.stop();
    QVERIFY(stopTimer.elapsed() < 250);
    QVERIFY(source.statistics().actualFrameRate > config.frameRate * 10.0);
}

void SimulatedSourceTests::fixedSeedProducesIdenticalBins()
{
    SimulationConfig config;
    config.binCount = 2048;
    config.randomSeed = 987654U;
    config.sweepEnabled = false;
    config.transientProbability = 0.0F;

    auto generate = [&config]() -> ConstSpectrumFramePtr {
        SpectrumPipeline pipeline;
        SimulatedSpectrumSource source;
        source.configure(config);
        source.setFrameSink([&pipeline](const SpectrumFramePtr& frame) {
            return pipeline.submit(frame);
        });
        const bool started = source.start(AcquisitionMode::SingleFrame);
        if (!started) {
            return ConstSpectrumFramePtr {};
        }
        for (int attempt = 0; attempt < 100 && source.state() != SourceState::Stopped;
             ++attempt) {
            QTest::qWait(5);
        }
        source.stop();
        return pipeline.latest();
    };

    const ConstSpectrumFramePtr first = generate();
    const ConstSpectrumFramePtr second = generate();
    QVERIFY(first && second);
    QVERIFY(first->bins == second->bins);
}

void SimulatedSourceTests::faultInjectionCreatesSequenceGapsAndInvalidFrames()
{
    SpectrumPipeline pipeline;
    SimulatedSpectrumSource source;
    SimulationConfig config;
    config.binCount = 1024;
    config.frameRate = 500.0;
    config.sweepEnabled = false;
    config.transientProbability = 0.0F;
    config.faults.sequenceJumpEveryFrames = 3;
    config.faults.sequenceSkipCount = 2;
    config.faults.invalidFrameEveryFrames = 4;
    source.configure(config);

    std::mutex framesMutex;
    std::vector<SpectrumMetadata> metadata;
    std::atomic<std::size_t> received { 0 };
    source.setFrameSink([&](const SpectrumFramePtr& frame) {
        {
            std::lock_guard<std::mutex> lock(framesMutex);
            metadata.push_back(frame->metadata);
        }
        received.fetch_add(1, std::memory_order_relaxed);
        return pipeline.submit(frame);
    });

    QVERIFY(source.start());
    QTRY_VERIFY_WITH_TIMEOUT(received.load(std::memory_order_relaxed) >= 10U, 1000);
    source.stop();

    QVERIFY(pipeline.statistics().invalidFrames >= 2U);
    QCOMPARE(source.statistics().droppedFrames, std::uint64_t(0));
    bool sawJump = false;
    bool sawInvalid = false;
    for (std::size_t index = 0; index < metadata.size(); ++index) {
        sawInvalid = sawInvalid
            || (metadata[index].flags & SpectrumFrameInjectedInvalid) != 0U;
        if (index > 0U
            && metadata[index].sequence > metadata[index - 1U].sequence + 1U) {
            sawJump = true;
            QCOMPARE(metadata[index].sequence - metadata[index - 1U].sequence,
                     std::uint64_t(3));
            QVERIFY((metadata[index].flags & SpectrumFrameSequenceJump) != 0U);
        }
    }
    QVERIFY(sawJump);
    QVERIFY(sawInvalid);
}

void SimulatedSourceTests::injectedDataPauseIsObservableAndInterruptible()
{
    SimulatedSpectrumSource source;
    SimulationConfig config;
    config.binCount = 1024;
    config.frameRate = 500.0;
    config.sweepEnabled = false;
    config.transientProbability = 0.0F;
    config.faults.pauseEveryFrames = 2;
    config.faults.pauseDurationSeconds = 0.08;
    source.configure(config);

    std::mutex timestampsMutex;
    std::vector<std::uint64_t> timestamps;
    std::atomic<std::size_t> received { 0 };
    source.setFrameSink([&](const SpectrumFramePtr& frame) {
        {
            std::lock_guard<std::mutex> lock(timestampsMutex);
            timestamps.push_back(frame->metadata.timestampNs);
        }
        received.fetch_add(1, std::memory_order_relaxed);
        return true;
    });
    QVERIFY(source.start());
    QTRY_VERIFY_WITH_TIMEOUT(received.load(std::memory_order_relaxed) >= 5U, 1000);
    source.stop();

    std::uint64_t largestGapNs = 0;
    for (std::size_t index = 1; index < timestamps.size(); ++index) {
        largestGapNs = std::max(largestGapNs, timestamps[index] - timestamps[index - 1U]);
    }
    QVERIFY2(largestGapNs >= 60'000'000U,
             "injected data pause did not create an observable frame gap");

    config.faults.pauseEveryFrames = 1;
    config.faults.pauseDurationSeconds = 10.0;
    source.configure(config);
    received.store(0, std::memory_order_relaxed);
    QVERIFY(source.start());
    QTRY_VERIFY_WITH_TIMEOUT(received.load(std::memory_order_relaxed) >= 1U, 500);
    QTest::qWait(20);
    QElapsedTimer stopTimer;
    stopTimer.start();
    source.stop();
    QVERIFY2(stopTimer.elapsed() < 250,
             "stop() did not interrupt an injected data pause");
}

void SimulatedSourceTests::injectedBurstProducesBackToBackFrames()
{
    SimulatedSpectrumSource source;
    SimulationConfig config;
    config.binCount = 1024;
    config.frameRate = 10.0;
    config.sweepEnabled = false;
    config.transientProbability = 0.0F;
    config.faults.burstEveryFrames = 2;
    config.faults.burstFrameCount = 3;
    source.configure(config);

    std::mutex framesMutex;
    std::vector<SpectrumMetadata> metadata;
    std::atomic<std::size_t> received { 0 };
    source.setFrameSink([&](const SpectrumFramePtr& frame) {
        {
            std::lock_guard<std::mutex> lock(framesMutex);
            metadata.push_back(frame->metadata);
        }
        received.fetch_add(1, std::memory_order_relaxed);
        return true;
    });
    QVERIFY(source.start());
    QTRY_VERIFY_WITH_TIMEOUT(received.load(std::memory_order_relaxed) >= 6U, 1000);
    source.stop();

    std::size_t shortIntervals = 0;
    std::size_t burstFlags = 0;
    for (std::size_t index = 0; index < metadata.size(); ++index) {
        if ((metadata[index].flags & SpectrumFrameBurst) != 0U) {
            ++burstFlags;
        }
        if (index > 0U
            && metadata[index].timestampNs - metadata[index - 1U].timestampNs
                < 20'000'000U) {
            ++shortIntervals;
        }
    }
    QVERIFY(burstFlags >= 3U);
    QVERIFY2(shortIntervals >= 3U,
             "configured burst did not bypass normal frame pacing");
}

void SimulatedSourceTests::toneWidthChangesOccupiedBins()
{
    auto occupiedBins = [](const double spanHz, const double widthHz) {
        SpectrumPipeline pipeline;
        SimulatedSpectrumSource source;
        SimulationConfig config;
        config.binCount = 4096;
        config.centerFrequencyHz = 100.0e6;
        config.spanHz = spanHz;
        config.noiseFloorDbfs = -150.0F;
        config.noiseDeviationDb = 0.0F;
        config.sweepEnabled = false;
        config.transientProbability = 0.0F;
        config.tones = { ToneConfig { true, 100.0e6, -10.0F, widthHz } };
        source.configure(config);
        source.setFrameSink([&pipeline](const SpectrumFramePtr& frame) {
            return pipeline.submit(frame);
        });
        if (!source.start(AcquisitionMode::SingleFrame)) {
            return std::size_t { 0 };
        }
        for (int attempt = 0; attempt < 100 && source.state() != SourceState::Stopped;
             ++attempt) {
            QTest::qWait(5);
        }
        source.stop();
        const ConstSpectrumFramePtr frame = pipeline.latest();
        return frame ? static_cast<std::size_t>(std::count_if(
                           frame->bins.cbegin(), frame->bins.cend(),
                           [](const float value) { return value > -80.0F; }))
                     : std::size_t { 0 };
    };

    const std::size_t narrow = occupiedBins(20.0e6, 20.0e3);
    const std::size_t wide = occupiedBins(20.0e6, 200.0e3);
    QVERIFY(narrow > 0U);
    QVERIFY(wide > narrow * 2U);

    // When the signal physical bandwidth is unchanged (100 kHz), narrowing the Span from 20MHz to 2MHz
    // causes the tone to occupy ~10x more frequency bins in the display.
    const std::size_t wideSpanOccupied = occupiedBins(20.0e6, 100.0e3);
    const std::size_t narrowSpanOccupied = occupiedBins(2.0e6, 100.0e3);
    QVERIFY(wideSpanOccupied > 0U);
    QVERIFY(narrowSpanOccupied > wideSpanOccupied * 3U);
}

void SimulatedSourceTests::equalTonesCombineInLinearPower()
{
    SpectrumPipeline pipeline;
    SimulatedSpectrumSource source;
    SimulationConfig config;
    config.binCount = 4096;
    config.centerFrequencyHz = 100.0e6;
    config.spanHz = 20.0e6;
    config.noiseFloorDbfs = -180.0F;
    config.noiseDeviationDb = 0.0F;
    config.sweepEnabled = false;
    config.transientProbability = 0.0F;
    config.tones = {
        ToneConfig { true, 100.0e6, -20.0F, 0.5F },
        ToneConfig { true, 100.0e6, -20.0F, 0.5F }
    };
    source.configure(config);
    source.setFrameSink([&pipeline](const SpectrumFramePtr& frame) {
        return pipeline.submit(frame);
    });
    QVERIFY(source.start(AcquisitionMode::SingleFrame));
    QTRY_COMPARE_WITH_TIMEOUT(source.state(), SourceState::Stopped, 500);
    source.stop();
    const ConstSpectrumFramePtr frame = pipeline.latest();
    QVERIFY(frame);
    const float peak = *std::max_element(frame->bins.cbegin(), frame->bins.cend());
    QVERIFY(peak > -17.1F && peak < -16.9F);
}

void SimulatedSourceTests::pingPongSweepMovesInBothDirections()
{
    SimulatedSpectrumSource source;
    SimulationConfig config;
    config.binCount = 4096;
    config.frameRate = 500.0;
    config.centerFrequencyHz = 100.0e6;
    config.spanHz = 20.0e6;
    config.noiseFloorDbfs = -150.0F;
    config.noiseDeviationDb = 0.0F;
    config.tones.clear();
    config.sweepEnabled = true;
    config.sweepStartHz = 95.0e6;
    config.sweepStopHz = 105.0e6;
    config.sweepPeriodSeconds = 0.1;
    config.sweepAmplitudeDbfs = -10.0F;
    config.sweepDirection = SweepDirection::PingPong;
    config.transientProbability = 0.0F;

    std::vector<std::size_t> peaks;
    source.configure(config);
    source.setFrameSink([&peaks](const SpectrumFramePtr& frame) {
        const auto peak = std::max_element(frame->bins.cbegin(), frame->bins.cend());
        peaks.push_back(static_cast<std::size_t>(
            std::distance(frame->bins.cbegin(), peak)));
        return true;
    });
    QVERIFY(source.start());
    QTRY_VERIFY_WITH_TIMEOUT(source.statistics().producedFrames >= 80, 1000);
    source.stop();

    bool movedUp = false;
    bool movedDown = false;
    for (std::size_t index = 1; index < peaks.size(); ++index) {
        movedUp = movedUp || peaks[index] > peaks[index - 1] + 2U;
        movedDown = movedDown || peaks[index] + 2U < peaks[index - 1];
    }
    QVERIFY(movedUp);
    QVERIFY(movedDown);
}

void SimulatedSourceTests::intervalRatesResetWhenNotRunning()
{
    SimulatedSpectrumSource source;
    SimulationConfig config;
    config.frameRate = 200.0;
    source.configure(config);
    source.setFrameSink([](const SpectrumFramePtr&) { return true; });

    QVERIFY(source.start());
    QTRY_VERIFY_WITH_TIMEOUT(source.statistics().producedFrames >= 10, 1000);
    QTest::qWait(50);
    const SourceStatistics running = source.statistics();
    QVERIFY(running.intervalFrameRate > 0.0);
    QVERIFY(running.intervalBytesPerSecond > 0.0);

    source.pause();
    const SourceStatistics paused = source.statistics();
    QCOMPARE(paused.intervalFrameRate, 0.0);
    QCOMPARE(paused.intervalBytesPerSecond, 0.0);
    source.stop();
    const SourceStatistics stopped = source.statistics();
    QCOMPARE(stopped.intervalFrameRate, 0.0);
    QCOMPARE(stopped.intervalBytesPerSecond, 0.0);
    const double stoppedUptime = stopped.uptimeSeconds;
    QTest::qWait(20);
    QCOMPARE(source.statistics().uptimeSeconds, stoppedUptime);
}

} // namespace rtsa

QTEST_APPLESS_MAIN(rtsa::SimulatedSourceTests)

#include "tst_SimulatedSource.moc"
