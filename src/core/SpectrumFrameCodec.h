#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rtsa {

inline constexpr std::uint32_t kSpectrumFrameMagic = 0x52545341U;
inline constexpr std::uint16_t kSpectrumFrameVersion = 1U;
inline constexpr std::uint16_t kSpectrumFrameHeaderBytes = 64U;

enum class SpectrumDataFormat : std::uint8_t {
    Int16LogDb001 = 0,
    Int32LogDbQ16_16 = 1,
    Int32LinearPower = 2,
    Int16ComplexIq = 3,
    Int32ComplexIq = 4
};

enum class SpectrumFrameParseError {
    None,
    BufferTooSmall,
    BadMagic,
    BadVersion,
    BadHeaderBytes,
    BadFrameBytes,
    BadBinCount,
    BadSpan,
    UnsupportedDataFormat,
    PayloadSizeMismatch
};

struct SpectrumFrameHeader {
    std::uint32_t magic = 0;
    std::uint16_t version = 0;
    std::uint16_t headerBytes = 0;
    std::uint32_t frameBytes = 0;
    std::uint32_t flags = 0;
    std::uint64_t sequence = 0;
    std::uint64_t timestampTicks = 0;
    std::int64_t centerFrequencyHz = 0;
    std::uint64_t spanHz = 0;
    std::uint32_t binCount = 0;
    std::uint8_t traceMode = 0;
    std::uint16_t accumulationFrames = 1;
    std::uint8_t windowType = 0;
    std::int32_t calibrationOffsetDbQ16_16 = 0;
    std::uint8_t dataFormat = 0;
    std::uint8_t unit = 0;
    std::uint16_t configEpoch = 0;

    bool fftShift() const noexcept { return (flags & (1U << 3U)) != 0U; }
    bool calibrated() const noexcept { return (flags & (1U << 2U)) != 0U; }
    bool iqCapture() const noexcept { return (flags & (1U << 5U)) != 0U; }
    bool overrange() const noexcept { return (flags & (1U << 6U)) != 0U; }
    bool fifoOverflow() const noexcept { return (flags & (1U << 7U)) != 0U; }
    bool frameValid() const noexcept { return (flags & (1U << 8U)) != 0U; }
};

std::size_t bytesPerSpectrumBin(std::uint8_t dataFormat) noexcept;
std::size_t spectrumFrameBytes(std::uint32_t binCount,
                               std::uint8_t dataFormat) noexcept;
bool isSupportedSpectrumBinCount(std::uint32_t binCount) noexcept;
double windowEnbwFactor(std::uint8_t windowType) noexcept;

bool parseSpectrumFrameHeader(const std::uint8_t* data,
                              std::size_t availableBytes,
                              SpectrumFrameHeader& header,
                              SpectrumFrameParseError& error) noexcept;

bool copyInt16LogBins(const std::uint8_t* payload,
                      std::size_t payloadBytes,
                      std::uint32_t binCount,
                      bool alreadyFftShifted,
                      std::vector<std::int16_t>& output,
                      SpectrumFrameParseError& error) noexcept;

} // namespace rtsa
