#include "core/AmplitudeUnits.h"
#include "core/EnvelopeReducer.h"
#include "core/FrequencyMapper.h"
#include "core/FramePool.h"
#include "core/SpectrumPipeline.h"
#include "core/SpectrumMeasurements.h"
#include "core/TraceProcessor.h"

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace rtsa {

class CoreTests final : public QObject {
    Q_OBJECT

private slots:
    void frequencyMappingClampsAtEdges();
    void envelopePreservesNarrowPeak();
    void envelopeIgnoresInvalidValues();
    void maxHoldPreservesLargestValue();
    void averageConvergesOverConfiguredFrames();
    void traceResetsWhenConfigurationChanges();
    void framePoolEnforcesCapacityAndReusesFrames();
    void pipelineRejectsInvalidFrames();
    void pipelineReportsSynchronousProcessingTelemetry();
    void pipelineAssignsIndependentPublicationSequence();
    void pipelineAndTraceRejectNonFiniteBins_data();
    void pipelineAndTraceRejectNonFiniteBins();
    void amplitudeUnitTokensAreStable();
    void rangePeakUsesOnlyRequestedFullResolutionBins();
    void channelPowerIntegratesInLinearDomain();
    void rangeMeasurementsRejectInvalidInputs();
    void traceModesReuseBoundedOutputFrames_data();
    void traceModesReuseBoundedOutputFrames();
};

void CoreTests::frequencyMappingClampsAtEdges()
{
    SpectrumMetadata metadata;
    metadata.centerFrequencyHz = 100.0e6;
    metadata.spanHz = 20.0e6;
    metadata.binCount = 100;

    QCOMPARE(FrequencyMapper::binWidthHz(metadata), 200000.0);
    QCOMPARE(FrequencyMapper::frequencyForBin(metadata, 0), 90.0e6);
    QCOMPARE(FrequencyMapper::nearestBinForFrequency(metadata, 10.0e6), std::size_t(0));
    QCOMPARE(FrequencyMapper::nearestBinForFrequency(metadata, 200.0e6), std::size_t(99));
}

void CoreTests::envelopePreservesNarrowPeak()
{
    std::vector<float> bins(65536, -120.0F);
    bins[32771] = -3.0F;
    std::vector<EnvelopeColumn> columns;

    EnvelopeReducer::reduce(bins.data(), bins.size(), 1920, columns);

    QCOMPARE(columns.size(), std::size_t(1920));
    const bool peakFound = std::any_of(columns.cbegin(), columns.cend(), [](const auto& column) {
        return column.valid && column.maximum == -3.0F;
    });
    QVERIFY(peakFound);
}

void CoreTests::envelopeIgnoresInvalidValues()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::vector<float> bins { nan, -10.0F, nan, -20.0F };
    std::vector<EnvelopeColumn> columns;

    EnvelopeReducer::reduce(bins.data(), bins.size(), 2, columns);

    QCOMPARE(columns.size(), std::size_t(2));
    QVERIFY(columns[0].valid);
    QCOMPARE(columns[0].minimum, -10.0F);
    QCOMPARE(columns[1].maximum, -20.0F);
}

void CoreTests::maxHoldPreservesLargestValue()
{
    TraceProcessor processor;
    processor.setMode(TraceMode::MaxHold);

    auto first = std::make_shared<SpectrumFrame>();
    first->metadata.binCount = 3;
    first->bins = { -50.0F, -20.0F, -80.0F };
    auto second = std::make_shared<SpectrumFrame>(*first);
    second->metadata.sequence = 2;
    second->bins = { -40.0F, -30.0F, -10.0F };

    static_cast<void>(processor.process(first));
    const auto result = processor.process(second);

    QCOMPARE(result->bins[0], -40.0F);
    QCOMPARE(result->bins[1], -20.0F);
    QCOMPARE(result->bins[2], -10.0F);
}

void CoreTests::averageConvergesOverConfiguredFrames()
{
    TraceProcessor processor;
    processor.setMode(TraceMode::Average);
    processor.setAverageCount(2);

    auto first = std::make_shared<SpectrumFrame>();
    first->metadata.binCount = 1;
    first->bins = { -20.0F };
    auto second = std::make_shared<SpectrumFrame>(*first);
    second->metadata.sequence = 2;
    second->bins = { -10.0F };

    static_cast<void>(processor.process(first));
    const auto result = processor.process(second);
    QCOMPARE(result->bins[0], -15.0F);
}

void CoreTests::traceResetsWhenConfigurationChanges()
{
    TraceProcessor processor;
    processor.setMode(TraceMode::MaxHold);

    auto first = std::make_shared<SpectrumFrame>();
    first->metadata.binCount = 1;
    first->metadata.configurationEpoch = 1;
    first->bins = { -5.0F };
    auto second = std::make_shared<SpectrumFrame>(*first);
    second->metadata.sequence = 2;
    second->metadata.configurationEpoch = 2;
    second->bins = { -80.0F };

    static_cast<void>(processor.process(first));
    const auto result = processor.process(second);
    QCOMPARE(result->bins[0], -80.0F);
}

void CoreTests::framePoolEnforcesCapacityAndReusesFrames()
{
    FramePool pool(2);
    auto first = pool.acquire(1024);
    auto second = pool.acquire(2048);
    auto exhausted = pool.acquire(1024);

    QVERIFY(first);
    QVERIFY(second);
    QVERIFY(!exhausted);
    QCOMPARE(pool.allocatedCount(), std::size_t(2));

    first.reset();
    QCOMPARE(pool.availableCount(), std::size_t(1));
    auto reused = pool.acquire(4096);
    QVERIFY(reused);
    QCOMPARE(reused->bins.size(), std::size_t(4096));
    QCOMPARE(pool.allocatedCount(), std::size_t(2));
}

void CoreTests::pipelineRejectsInvalidFrames()
{
    SpectrumPipeline pipeline;
    auto invalid = std::make_shared<SpectrumFrame>();
    invalid->metadata.binCount = 3;
    invalid->bins = { -10.0F };

    QVERIFY(!pipeline.submit(invalid));
    QCOMPARE(pipeline.statistics().invalidFrames, std::uint64_t(1));
    QVERIFY(!pipeline.latest());
}

void CoreTests::pipelineReportsSynchronousProcessingTelemetry()
{
    SpectrumPipeline pipeline;
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.binCount = 4096;
    frame->bins.assign(4096, -100.0F);
    QVERIFY(pipeline.submit(frame));

    const PipelineStatistics statistics = pipeline.statistics();
    QCOMPARE(statistics.submittedFrames, std::uint64_t(1));
    QCOMPARE(statistics.publishedFrames, std::uint64_t(1));
    QVERIFY(statistics.lastProcessingMilliseconds >= 0.0);
    QCOMPARE(statistics.queueDepth, std::uint32_t(0));
}

void CoreTests::pipelineAssignsIndependentPublicationSequence()
{
    SpectrumPipeline pipeline;
    auto first = std::make_shared<SpectrumFrame>();
    first->metadata.sequence = 42;
    first->metadata.binCount = 2;
    first->bins = { -10.0F, -20.0F };
    QVERIFY(pipeline.submit(first));
    QCOMPARE(pipeline.latest()->metadata.publicationSequence, std::uint64_t(1));

    auto invalid = std::make_shared<SpectrumFrame>(*first);
    invalid->bins[0] = std::numeric_limits<float>::quiet_NaN();
    QVERIFY(!pipeline.submit(invalid));

    auto repeatedSourceSequence = std::make_shared<SpectrumFrame>(*first);
    repeatedSourceSequence->bins[0] = -5.0F;
    QVERIFY(pipeline.submit(repeatedSourceSequence));
    QCOMPARE(pipeline.latest()->metadata.sequence, std::uint64_t(42));
    QCOMPARE(pipeline.latest()->metadata.publicationSequence, std::uint64_t(2));
}

void CoreTests::pipelineAndTraceRejectNonFiniteBins_data()
{
    QTest::addColumn<float>("invalidValue");
    QTest::newRow("nan") << std::numeric_limits<float>::quiet_NaN();
    QTest::newRow("positive-infinity") << std::numeric_limits<float>::infinity();
    QTest::newRow("negative-infinity") << -std::numeric_limits<float>::infinity();
}

void CoreTests::pipelineAndTraceRejectNonFiniteBins()
{
    QFETCH(float, invalidValue);
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.binCount = 3;
    frame->bins = { -20.0F, invalidValue, -30.0F };

    SpectrumPipeline pipeline;
    QVERIFY(!pipeline.submit(frame));
    QCOMPARE(pipeline.statistics().submittedFrames, std::uint64_t(1));
    QCOMPARE(pipeline.statistics().invalidFrames, std::uint64_t(1));
    QVERIFY(!pipeline.latest());

    TraceProcessor trace;
    trace.setMode(TraceMode::Average);
    QVERIFY(!trace.process(frame));
}

void CoreTests::amplitudeUnitTokensAreStable()
{
    QCOMPARE(QString::fromLatin1(amplitudeUnitSymbol(AmplitudeUnit::Dbfs)),
             QStringLiteral("dBFS"));
    QCOMPARE(QString::fromLatin1(amplitudeUnitSymbol(AmplitudeUnit::Dbm)),
             QStringLiteral("dBm"));
    QCOMPARE(QString::fromLatin1(amplitudeUnitSymbol(AmplitudeUnit::Dbc)),
             QStringLiteral("dBc"));
    QCOMPARE(QString::fromLatin1(amplitudeUnitSymbol(AmplitudeUnit::LinearPower)),
             QStringLiteral("Power"));
    QCOMPARE(QString::fromLatin1(amplitudeCsvColumnName(AmplitudeUnit::LinearPower)),
             QStringLiteral("power_linear"));
}

void CoreTests::rangePeakUsesOnlyRequestedFullResolutionBins()
{
    SpectrumFrame frame;
    frame.metadata.binCount = 100;
    frame.metadata.centerFrequencyHz = 100.0e6;
    frame.metadata.spanHz = 20.0e6;
    frame.metadata.unit = AmplitudeUnit::Dbfs;
    frame.bins.assign(100, -120.0F);
    frame.bins[10] = -1.0F;
    frame.bins[25] = -12.0F;

    const RangePeakMeasurement result = SpectrumMeasurements::peakInRange(
        frame, 94.0e6, 96.0e6);
    QVERIFY(result.valid);
    QCOMPARE(result.bin, std::size_t(25));
    QCOMPARE(result.frequencyHz,
             FrequencyMapper::frequencyForBin(frame.metadata, 25));
    QCOMPARE(result.amplitude, -12.0F);
    QCOMPARE(static_cast<int>(result.unit), static_cast<int>(AmplitudeUnit::Dbfs));
    QVERIFY(!result.calibrated);
}

void CoreTests::channelPowerIntegratesInLinearDomain()
{
    SpectrumFrame frame;
    frame.metadata.binCount = 4;
    frame.metadata.centerFrequencyHz = 2.0;
    frame.metadata.spanHz = 4.0;
    frame.metadata.unit = AmplitudeUnit::Dbfs;
    frame.bins = { -10.0F, -10.0F, -40.0F, -40.0F };

    const ChannelPowerMeasurement logarithmic =
        SpectrumMeasurements::channelPowerInRange(frame, 0.0, 2.0);
    QVERIFY(logarithmic.valid);
    QCOMPARE(logarithmic.integratedBins, std::size_t(2));
    QVERIFY(std::abs(logarithmic.value - (-6.989700043)) < 1.0e-6);

    frame.metadata.unit = AmplitudeUnit::LinearPower;
    frame.metadata.calibrated = true;
    frame.bins = { 1.0F, 2.0F, 4.0F, 8.0F };
    const ChannelPowerMeasurement linear =
        SpectrumMeasurements::channelPowerInRange(frame, 1.0, 4.0);
    QVERIFY(linear.valid);
    QCOMPARE(linear.integratedBins, std::size_t(3));
    QCOMPARE(linear.value, 14.0);
    QVERIFY(linear.calibrated);
}

void CoreTests::rangeMeasurementsRejectInvalidInputs()
{
    SpectrumFrame frame;
    frame.metadata.binCount = 2;
    frame.metadata.centerFrequencyHz = 1.0;
    frame.metadata.spanHz = 2.0;
    frame.bins = { -10.0F, -20.0F };

    QVERIFY(!SpectrumMeasurements::peakInRange(frame, 1.0, 1.0).valid);
    QVERIFY(!SpectrumMeasurements::channelPowerInRange(frame, 3.0, 4.0).valid);
    frame.bins[1] = std::numeric_limits<float>::quiet_NaN();
    QVERIFY(!SpectrumMeasurements::peakInRange(frame, 0.0, 2.0).valid);
    QVERIFY(!SpectrumMeasurements::channelPowerInRange(frame, 0.0, 2.0).valid);
}

void CoreTests::traceModesReuseBoundedOutputFrames_data()
{
    QTest::addColumn<int>("modeValue");
    QTest::newRow("average") << static_cast<int>(TraceMode::Average);
    QTest::newRow("max-hold") << static_cast<int>(TraceMode::MaxHold);
    QTest::newRow("min-hold") << static_cast<int>(TraceMode::MinHold);
}

void CoreTests::traceModesReuseBoundedOutputFrames()
{
    QFETCH(int, modeValue);
    TraceProcessor processor;
    processor.setMode(static_cast<TraceMode>(modeValue));
    auto input = std::make_shared<SpectrumFrame>();
    input->metadata.binCount = 16384;
    input->bins.assign(16384, -100.0F);

    ConstSpectrumFramePtr result;
    for (std::uint64_t sequence = 1; sequence <= 200; ++sequence) {
        input->metadata.sequence = sequence;
        input->bins[static_cast<std::size_t>(sequence) % input->bins.size()] = -10.0F;
        result = processor.process(input);
        QVERIFY(result);
    }
    QVERIFY(processor.reusableOutputFrameCount() <= std::size_t(2));
}

} // namespace rtsa

QTEST_APPLESS_MAIN(rtsa::CoreTests)

#include "tst_Core.moc"
