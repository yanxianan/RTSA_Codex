#include "core/SpectrumFrameCodec.h"

#include <algorithm>
#include <cstring>

namespace rtsa {
namespace {

std::uint16_t readLe16(const std::uint8_t* data) noexcept
{
    return static_cast<std::uint16_t>(data[0])
        | static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8U);
}

std::uint32_t readLe32(const std::uint8_t* data) noexcept
{
    return static_cast<std::uint32_t>(data[0])
        | (static_cast<std::uint32_t>(data[1]) << 8U)
        | (static_cast<std::uint32_t>(data[2]) << 16U)
        | (static_cast<std::uint32_t>(data[3]) << 24U);
}

std::uint64_t readLe64(const std::uint8_t* data) noexcept
{
    std::uint64_t value = 0;
    for (unsigned index = 0; index < 8U; ++index) {
        value |= static_cast<std::uint64_t>(data[index]) << (index * 8U);
    }
    return value;
}

} // namespace

std::size_t bytesPerSpectrumBin(const std::uint8_t dataFormat) noexcept
{
    switch (static_cast<SpectrumDataFormat>(dataFormat)) {
    case SpectrumDataFormat::Int16LogDb001:
        return 2U;
    case SpectrumDataFormat::Int32LogDbQ16_16:
    case SpectrumDataFormat::Int32LinearPower:
        return 4U;
    default:
        return 0U;
    }
}

std::size_t spectrumFrameBytes(const std::uint32_t binCount,
                               const std::uint8_t dataFormat) noexcept
{
    const std::size_t bytesPerBin = bytesPerSpectrumBin(dataFormat);
    if (bytesPerBin == 0U) {
        return 0U;
    }
    return static_cast<std::size_t>(kSpectrumFrameHeaderBytes)
        + static_cast<std::size_t>(binCount) * bytesPerBin;
}

bool isSupportedSpectrumBinCount(const std::uint32_t binCount) noexcept
{
    return binCount >= 1024U && binCount <= 65536U
        && (binCount & (binCount - 1U)) == 0U;
}

double windowEnbwFactor(const std::uint8_t windowType) noexcept
{
    switch (windowType) {
    case 0:
        return 1.00;
    case 1:
        return 1.50;
    case 2:
        return 1.36;
    case 3:
        return 1.73;
    case 4:
        return 1.90;
    case 5:
        return 3.77;
    default:
        return 1.00;
    }
}

bool parseSpectrumFrameHeader(const std::uint8_t* data,
                              const std::size_t availableBytes,
                              SpectrumFrameHeader& header,
                              SpectrumFrameParseError& error) noexcept
{
    error = SpectrumFrameParseError::None;
    if (data == nullptr || availableBytes < kSpectrumFrameHeaderBytes) {
        error = SpectrumFrameParseError::BufferTooSmall;
        return false;
    }

    header.magic = readLe32(data + 0x00U);
    header.version = readLe16(data + 0x04U);
    header.headerBytes = readLe16(data + 0x06U);
    header.frameBytes = readLe32(data + 0x08U);
    header.flags = readLe32(data + 0x0CU);
    header.sequence = readLe64(data + 0x10U);
    header.timestampTicks = readLe64(data + 0x18U);
    header.centerFrequencyHz = static_cast<std::int64_t>(readLe64(data + 0x20U));
    header.spanHz = readLe64(data + 0x28U);
    header.binCount = readLe32(data + 0x30U);
    header.traceMode = data[0x34U];
    header.accumulationFrames = readLe16(data + 0x35U);
    header.windowType = data[0x37U];
    header.calibrationOffsetDbQ16_16 =
        static_cast<std::int32_t>(readLe32(data + 0x38U));
    header.dataFormat = data[0x3CU];
    header.unit = data[0x3DU];
    header.configEpoch = readLe16(data + 0x3EU);

    if (header.magic != kSpectrumFrameMagic) {
        error = SpectrumFrameParseError::BadMagic;
        return false;
    }
    if (header.version != kSpectrumFrameVersion) {
        error = SpectrumFrameParseError::BadVersion;
        return false;
    }
    if (header.headerBytes != kSpectrumFrameHeaderBytes) {
        error = SpectrumFrameParseError::BadHeaderBytes;
        return false;
    }
    if (!isSupportedSpectrumBinCount(header.binCount)) {
        error = SpectrumFrameParseError::BadBinCount;
        return false;
    }
    if (header.spanHz == 0U) {
        error = SpectrumFrameParseError::BadSpan;
        return false;
    }
    const std::size_t expectedFrameBytes =
        spectrumFrameBytes(header.binCount, header.dataFormat);
    if (expectedFrameBytes == 0U) {
        error = SpectrumFrameParseError::UnsupportedDataFormat;
        return false;
    }
    if (header.frameBytes != expectedFrameBytes) {
        error = SpectrumFrameParseError::BadFrameBytes;
        return false;
    }
    return true;
}

bool copyInt16LogBins(const std::uint8_t* payload,
                      const std::size_t payloadBytes,
                      const std::uint32_t binCount,
                      const bool alreadyFftShifted,
                      std::vector<std::int16_t>& output,
                      SpectrumFrameParseError& error) noexcept
{
    error = SpectrumFrameParseError::None;
    const std::size_t requiredBytes =
        static_cast<std::size_t>(binCount) * sizeof(std::int16_t);
    if (payload == nullptr || payloadBytes < requiredBytes) {
        error = SpectrumFrameParseError::PayloadSizeMismatch;
        return false;
    }

    output.resize(binCount);
    // The host and PL are both little-endian. The contract fixes little-endian
    // for the wire format, and the Zynq PS is little-endian as well.
    std::memcpy(output.data(), payload, requiredBytes);

    if (!alreadyFftShifted && binCount > 1U) {
        const auto middle = output.begin() + static_cast<std::ptrdiff_t>(binCount / 2U);
        std::rotate(output.begin(), middle, output.end());
    }
    return true;
}

} // namespace rtsa
