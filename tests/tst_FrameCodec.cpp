#include "core/EnvelopeReducer.h"
#include "core/PlRegisterMap.h"
#include "core/SpectrumFrameCodec.h"
#include "core/SpectrumMeasurements.h"
#include "core/SpectrumPipeline.h"

#include <QtTest>

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

namespace rtsa {
namespace {

void writeLe16(std::vector<std::uint8_t>& data, const std::size_t offset,
               const std::uint16_t value)
{
    data[offset + 0U] = static_cast<std::uint8_t>(value & 0xFFU);
    data[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void writeLe32(std::vector<std::uint8_t>& data, const std::size_t offset,
               const std::uint32_t value)
{
    for (unsigned index = 0; index < 4U; ++index) {
        data[offset + index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

void writeLe64(std::vector<std::uint8_t>& data, const std::size_t offset,
               const std::uint64_t value)
{
    for (unsigned index = 0; index < 8U; ++index) {
        data[offset + index] =
            static_cast<std::uint8_t>((value >> (index * 8U)) & 0xFFU);
    }
}

std::vector<std::uint8_t> makeFrame(const std::uint32_t binCount,
                                    const bool fftShift,
                                    const std::uint64_t sequence = 7U)
{
    const std::size_t frameBytes =
        spectrumFrameBytes(binCount, static_cast<std::uint8_t>(
            SpectrumDataFormat::Int16LogDb001));
    std::vector<std::uint8_t> frame(frameBytes, 0U);
    writeLe32(frame, 0x00U, kSpectrumFrameMagic);
    writeLe16(frame, 0x04U, kSpectrumFrameVersion);
    writeLe16(frame, 0x06U, kSpectrumFrameHeaderBytes);
    writeLe32(frame, 0x08U, static_cast<std::uint32_t>(frameBytes));
    writeLe32(frame, 0x0CU, 1U | 0x100U | (fftShift ? (1U << 3U) : 0U));
    writeLe64(frame, 0x10U, sequence);
    writeLe64(frame, 0x18U, 123456U);
    writeLe64(frame, 0x20U, 0U);
    writeLe64(frame, 0x28U, 9765625U);
    writeLe32(frame, 0x30U, binCount);
    frame[0x34U] = 1U; // Max Hold
    writeLe16(frame, 0x35U, 80U);
    frame[0x37U] = 4U; // Blackman-Harris
    writeLe32(frame, 0x38U, 0U);
    frame[0x3CU] = static_cast<std::uint8_t>(SpectrumDataFormat::Int16LogDb001);
    frame[0x3DU] = 0U; // dBFS
    writeLe16(frame, 0x3EU, 3U);

    for (std::uint32_t index = 0; index < binCount; ++index) {
        const std::int16_t value =
            static_cast<std::int16_t>((index % 200U) - 100);
        const std::size_t offset = kSpectrumFrameHeaderBytes
            + static_cast<std::size_t>(index) * 2U;
        writeLe16(frame, offset, static_cast<std::uint16_t>(value));
    }
    return frame;
}

} // namespace

class FrameCodecTests final : public QObject {
    Q_OBJECT

private slots:
    void parsesValidHeader();
    void rejectsBadMagic();
    void rejectsBadVersion();
    void rejectsBadFrameBytes();
    void rejectsBadBinCount();
    void rejectsBadSpan();
    void rejectsShortPayload();
    void rotatesNaturalOrderIntoCenteredOrder();
    void reducesInt16LogEnvelope();
    void pipelineAcceptsLogFramesWithoutDoubleTrace();
    void measurementsUseLogBins();
    void plRegisterMapWritesConfiguration();
    void ddsPhaseAndSampleRateConversions();
    void logBinsApplyCalibrationOffset();
};

void FrameCodecTests::parsesValidHeader()
{
    const auto frame = makeFrame(1024U, false);
    SpectrumFrameHeader header;
    SpectrumFrameParseError error = SpectrumFrameParseError::None;
    QVERIFY(parseSpectrumFrameHeader(frame.data(), frame.size(), header, error));
    QVERIFY(error == SpectrumFrameParseError::None);
    QCOMPARE(header.magic, kSpectrumFrameMagic);
    QCOMPARE(header.version, kSpectrumFrameVersion);
    QCOMPARE(header.headerBytes, kSpectrumFrameHeaderBytes);
    QCOMPARE(header.frameBytes, std::uint32_t(2112U));
    QCOMPARE(header.binCount, std::uint32_t(1024U));
    QCOMPARE(header.spanHz, std::uint64_t(9765625U));
    QCOMPARE(header.sequence, std::uint64_t(7U));
    QCOMPARE(header.traceMode, std::uint8_t(1U));
    QCOMPARE(header.accumulationFrames, std::uint16_t(80U));
    QCOMPARE(header.windowType, std::uint8_t(4U));
    QCOMPARE(header.configEpoch, std::uint16_t(3U));
    QVERIFY(!header.fftShift());
}

void FrameCodecTests::rejectsBadMagic()
{
    auto data = makeFrame(1024U, false);
    data[0] = 0U;
    SpectrumFrameHeader header;
    SpectrumFrameParseError error = SpectrumFrameParseError::None;
    QVERIFY(!parseSpectrumFrameHeader(data.data(), data.size(), header, error));
    QVERIFY(error == SpectrumFrameParseError::BadMagic);
}

void FrameCodecTests::rejectsBadVersion()
{
    auto data = makeFrame(1024U, false);
    writeLe16(data, 0x04U, 2U);
    SpectrumFrameHeader header;
    SpectrumFrameParseError error = SpectrumFrameParseError::None;
    QVERIFY(!parseSpectrumFrameHeader(data.data(), data.size(), header, error));
    QVERIFY(error == SpectrumFrameParseError::BadVersion);
}

void FrameCodecTests::rejectsBadFrameBytes()
{
    auto data = makeFrame(1024U, false);
    writeLe32(data, 0x08U, 1234U);
    SpectrumFrameHeader header;
    SpectrumFrameParseError error = SpectrumFrameParseError::None;
    QVERIFY(!parseSpectrumFrameHeader(data.data(), data.size(), header, error));
    QVERIFY(error == SpectrumFrameParseError::BadFrameBytes);
}

void FrameCodecTests::rejectsBadBinCount()
{
    auto data = makeFrame(1024U, false);
    writeLe32(data, 0x30U, 1000U);
    SpectrumFrameHeader header;
    SpectrumFrameParseError error = SpectrumFrameParseError::None;
    QVERIFY(!parseSpectrumFrameHeader(data.data(), data.size(), header, error));
    QVERIFY(error == SpectrumFrameParseError::BadBinCount);
}

void FrameCodecTests::rejectsBadSpan()
{
    auto data = makeFrame(1024U, false);
    writeLe64(data, 0x28U, 0U);
    SpectrumFrameHeader header;
    SpectrumFrameParseError error = SpectrumFrameParseError::None;
    QVERIFY(!parseSpectrumFrameHeader(data.data(), data.size(), header, error));
    QVERIFY(error == SpectrumFrameParseError::BadSpan);
}

void FrameCodecTests::rejectsShortPayload()
{
    const auto data = makeFrame(1024U, false);
    SpectrumFrameHeader header;
    SpectrumFrameParseError error = SpectrumFrameParseError::None;
    QVERIFY(parseSpectrumFrameHeader(data.data(), data.size(), header, error));

    std::vector<std::int16_t> bins;
    QVERIFY(!copyInt16LogBins(data.data() + kSpectrumFrameHeaderBytes,
                              1024U,
                              header.binCount,
                              false,
                              bins,
                              error));
    QVERIFY(error == SpectrumFrameParseError::PayloadSizeMismatch);
}

void FrameCodecTests::rotatesNaturalOrderIntoCenteredOrder()
{
    const auto data = makeFrame(1024U, false);
    SpectrumFrameHeader header;
    SpectrumFrameParseError error = SpectrumFrameParseError::None;
    QVERIFY(parseSpectrumFrameHeader(data.data(), data.size(), header, error));

    std::vector<std::int16_t> bins;
    QVERIFY(copyInt16LogBins(data.data() + kSpectrumFrameHeaderBytes,
                             data.size() - kSpectrumFrameHeaderBytes,
                             header.binCount,
                             false,
                             bins,
                             error));
    QCOMPARE(bins.size(), std::size_t(1024U));
    QCOMPARE(bins[0], static_cast<std::int16_t>((512U % 200U) - 100U));
    QCOMPARE(bins[512], static_cast<std::int16_t>(0 - 100));
}

void FrameCodecTests::reducesInt16LogEnvelope()
{
    const std::vector<std::int16_t> bins { -1000, -2000, -500, -1500 };
    std::vector<EnvelopeColumn> columns;
    EnvelopeReducer::reduce(bins.data(), bins.size(), 2U, columns);
    QCOMPARE(columns.size(), std::size_t(2U));
    QCOMPARE(columns[0].minimumRaw, std::int16_t(-2000));
    QCOMPARE(columns[0].maximumRaw, std::int16_t(-1000));
    QCOMPARE(columns[1].minimumRaw, std::int16_t(-1500));
    QCOMPARE(columns[1].maximumRaw, std::int16_t(-500));
    QCOMPARE(columns[0].minimum, -20.0F);
    QCOMPARE(columns[1].maximum, -5.0F);
}

void FrameCodecTests::pipelineAcceptsLogFramesWithoutDoubleTrace()
{
    SpectrumPipeline pipeline;
    pipeline.setTraceMode(TraceMode::MaxHold);

    auto first = std::make_shared<SpectrumFrame>();
    first->metadata.binCount = 1024U;
    first->metadata.centerFrequencyHz = 0.0;
    first->metadata.spanHz = 9.765625e6;
    first->logBins.assign(1024U, -1000);
    QVERIFY(pipeline.submit(first));
    QVERIFY(pipeline.latest() != nullptr);
    QCOMPARE(pipeline.latest()->metadata.publicationSequence, std::uint64_t(1U));
    QCOMPARE(pipeline.latest()->binDb(0), -10.0F);

    auto second = std::make_shared<SpectrumFrame>();
    second->metadata.binCount = 1024U;
    second->metadata.centerFrequencyHz = 0.0;
    second->metadata.spanHz = 9.765625e6;
    second->logBins.assign(1024U, -2000);
    QVERIFY(pipeline.submit(second));
    QCOMPARE(pipeline.latest()->metadata.publicationSequence, std::uint64_t(2U));
    QCOMPARE(pipeline.latest()->binDb(0), -20.0F);
}

void FrameCodecTests::measurementsUseLogBins()
{
    SpectrumFrame frame;
    frame.metadata.binCount = 4U;
    frame.metadata.centerFrequencyHz = 2.0;
    frame.metadata.spanHz = 4.0;
    frame.logBins = { -1000, -1000, -4000, -4000 };

    const auto peak = SpectrumMeasurements::peakInRange(frame, 0.0, 4.0);
    QVERIFY(peak.valid);
    QCOMPARE(peak.bin, std::size_t(0U));
    QCOMPARE(peak.amplitude, -10.0F);

    const auto power = SpectrumMeasurements::channelPowerInRange(frame, 0.0, 4.0);
    QVERIFY(power.valid);
    QVERIFY(power.integratedBins == 4U);
    QVERIFY(power.value > -7.1 && power.value < -6.9);
}

void FrameCodecTests::plRegisterMapWritesConfiguration()
{
    std::array<std::uint32_t, 2048U> memory {};
    PlRegisterMap registers;
    QVERIFY(registers.attachMemoryForTest(memory.data(), memory.size()));

    PlConfig config;
    config.centerFrequencyHz = 1'000'000'000ULL;
    config.spanHz = 200'000'000ULL;
    config.traceMode = 2U;
    config.accumulationFrames = 16U;
    config.windowType = 1U;
    config.calibrationOffsetQ16_16 = -32768;
    config.configEpoch = 7U;
    config.dmaMode = 2U;
    config.cicRate = 16U;
    config.cicTrunc = 33U;
    config.hbLevel = 3U;
    config.maxHoldFrames = 120U;
    config.ddsFrequencyHz = 1.5e6;
    QVERIFY(registers.writeConfig(config));

    QCOMPARE(memory[kPlRegCenterLow / 4U], std::uint32_t(1'000'000'000U));
    QCOMPARE(memory[kPlRegCenterHigh / 4U], std::uint32_t(0U));
    QCOMPARE(memory[kPlRegSpanLow / 4U], std::uint32_t(200'000'000U));
    QCOMPARE(memory[kPlRegTraceAccum / 4U],
             (std::uint32_t(16U) << 8U) | 2U);
    QCOMPARE(memory[kPlRegWindow / 4U], std::uint32_t(1U));
    QCOMPARE(memory[kPlRegCommit / 4U], std::uint32_t(1U));
    QCOMPARE(memory[kPlRegDdsPhaseHigh / 4U], std::uint32_t(0x004EU));
    QCOMPARE(memory[kPlRegDdsPhaseLow / 4U], std::uint32_t(0xA4A9U));
    QCOMPARE(memory[kPlRegDmaMode / 4U], std::uint32_t(2U));
    QCOMPARE(memory[kPlRegCicRate / 4U], std::uint32_t(16U));
    QCOMPARE(memory[kPlRegCicTrunc / 4U], std::uint32_t(33U));
    QCOMPARE(memory[kPlRegHbLevel / 4U], std::uint32_t(3U));
    QCOMPARE(memory[kPlRegMaxHoldFrames / 4U], std::uint32_t(120U));
    QCOMPARE(memory[kPlRegDdsResync / 4U], std::uint32_t(0U));
    QCOMPARE(memory[kPlRegCicConfigValid / 4U], std::uint32_t(0U));

    const PlConfig readBack = registers.readConfig();
    QCOMPARE(readBack.centerFrequencyHz, config.centerFrequencyHz);
    QCOMPARE(readBack.spanHz, config.spanHz);
    QCOMPARE(readBack.traceMode, config.traceMode);
    QCOMPARE(readBack.accumulationFrames, config.accumulationFrames);
    QCOMPARE(readBack.windowType, config.windowType);
    QCOMPARE(readBack.calibrationOffsetQ16_16,
             config.calibrationOffsetQ16_16);
    QCOMPARE(readBack.configEpoch, config.configEpoch);
    QCOMPARE(readBack.dmaMode, config.dmaMode);
    QCOMPARE(readBack.cicRate, config.cicRate);
    QCOMPARE(readBack.cicTrunc, config.cicTrunc);
    QCOMPARE(readBack.hbLevel, config.hbLevel);
    QCOMPARE(readBack.maxHoldFrames, config.maxHoldFrames);
    QVERIFY(std::abs(readBack.ddsFrequencyHz - 1.5e6) < 0.1);

    // A second commit must remain effective even though the PS-visible commit
    // level was already 1 at the end of the first transaction.
    config.configEpoch = 8U;
    config.maxHoldFrames = 64U;
    QVERIFY(registers.writeConfig(config));
    QCOMPARE(memory[kPlRegCommit / 4U], std::uint32_t(1U));
    QCOMPARE(registers.readConfig().configEpoch, std::uint16_t(8U));
    QCOMPARE(registers.readConfig().maxHoldFrames, std::uint16_t(64U));

    // Runtime soft update: frame metadata and Max Hold may change without
    // touching the DDS/CIC/HB datapath registers.
    PlConfig soft = config;
    soft.centerFrequencyHz = 2'400'000'000ULL;
    soft.accumulationFrames = 32U;
    soft.maxHoldFrames = 32U;
    soft.configEpoch = 9U;
    QVERIFY(registers.writeFrameConfig(soft));
    QCOMPARE(registers.readConfig().centerFrequencyHz, soft.centerFrequencyHz);
    QCOMPARE(registers.readConfig().accumulationFrames,
             soft.accumulationFrames);
    QCOMPARE(registers.readConfig().maxHoldFrames, soft.maxHoldFrames);
    QCOMPARE(registers.readConfig().configEpoch, soft.configEpoch);
    QCOMPARE(registers.readConfig().dmaMode, std::uint8_t(2U));
    QCOMPARE(registers.readConfig().cicRate, std::uint16_t(16U));
    QCOMPARE(registers.readConfig().hbLevel, std::uint8_t(3U));

    // CIC=1 is the wideband bypass and must not be clamped to 4.
    PlConfig wide = config;
    wide.cicRate = 1U;
    wide.hbLevel = 0U;
    wide.configEpoch = 10U;
    QVERIFY(registers.writeConfig(wide));
    QCOMPARE(memory[kPlRegCicRate / 4U], std::uint32_t(1U));
    QCOMPARE(registers.readConfig().cicRate, std::uint16_t(1U));
    QCOMPARE(registers.readConfig().hbLevel, std::uint8_t(0U));

    // CIC 2/3 are not legal in the PL. They must be normalized to 4.
    PlConfig invalidCic = config;
    invalidCic.cicRate = 2U;
    invalidCic.configEpoch = 11U;
    QVERIFY(registers.writeConfig(invalidCic));
    QCOMPARE(memory[kPlRegCicRate / 4U], std::uint32_t(4U));

    // Config sequencer status is exposed read-only to the PS.
    memory[kPlRegConfigStatus / 4U] = 1U;
    bool busy = false;
    QVERIFY(registers.readConfigBusy(busy));
    QVERIFY(busy);
    memory[kPlRegConfigStatus / 4U] = 0U;
    QVERIFY(registers.readConfigBusy(busy));
    QVERIFY(!busy);

    memory[kPlRegActiveEpoch / 4U] = 9U;
    std::uint16_t activeEpoch = 0U;
    QVERIFY(registers.readActiveEpoch(activeEpoch));
    QCOMPARE(activeEpoch, std::uint16_t(9U));
    QVERIFY(registers.waitForActiveEpoch(9U, 10));
    memory[kPlRegActiveEpoch / 4U] = 8U;
    QVERIFY(!registers.waitForActiveEpoch(9U, 0));
}

void FrameCodecTests::ddsPhaseAndSampleRateConversions()
{
    QCOMPARE(ddsPhaseStepWord(1.5e6), std::uint32_t(0x004EA4A9U));
    QVERIFY(std::abs(ddsFrequencyFromPhaseStep(0x004EA4A9U) - 1.5e6) < 0.1);
    QCOMPARE(ddsPhaseStepWord(625.0e6), std::uint32_t(0x80000000U));
    QCOMPARE(ddsPhaseStepWord(-1.0), std::uint32_t(0U));

    QCOMPARE(normalizeCicRate(0U), std::uint16_t(1U));
    QCOMPARE(normalizeCicRate(1U), std::uint16_t(1U));
    QCOMPARE(normalizeCicRate(2U), std::uint16_t(4U));
    QCOMPARE(normalizeCicRate(3U), std::uint16_t(4U));
    QCOMPARE(normalizeCicRate(4U), std::uint16_t(4U));
    QCOMPARE(normalizeCicRate(8192U), std::uint16_t(8192U));

    QCOMPARE(plDecimatedSampleRateHz(8U, 2U), 9765625.0);
    QCOMPARE(plDecimatedSampleRateHz(2U, 0U), 78125000.0);
    QCOMPARE(plDecimatedSampleRateHz(1U, 0U), 312500000.0);
    QCOMPARE(plDecimatedSampleRateHz(1U, 1U), 156250000.0);
    QCOMPARE(plDecimatedSampleRateHz(4U, 0U), 78125000.0);
    QCOMPARE(plDecimatedSampleRateHz(8192U, 6U), 596.04644775390625);
}

void FrameCodecTests::logBinsApplyCalibrationOffset()
{
    SpectrumFrame frame;
    frame.metadata.binCount = 2U;
    frame.metadata.calibrationOffsetDb = -190.0;
    frame.logBins = { -1000, -2000 };
    QCOMPARE(frame.binDb(0), -200.0F);
    QCOMPARE(frame.binDb(1), -210.0F);
}

} // namespace rtsa

QTEST_MAIN(rtsa::FrameCodecTests)

#include "tst_FrameCodec.moc"
