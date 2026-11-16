#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <memory>
#include <vector>

namespace rtsa {

enum SpectrumFrameFlag : std::uint32_t {
    SpectrumFrameTransient = 1U << 0U,
    SpectrumFrameInjectedInvalid = 1U << 1U,
    SpectrumFrameSequenceJump = 1U << 2U,
    SpectrumFrameBurst = 1U << 3U
};

enum class AmplitudeUnit : std::uint8_t {
    Dbfs,
    Dbm,
    Dbc,
    LinearPower
};

struct SpectrumMetadata {
    std::uint64_t sequence = 0;
    std::uint64_t publicationSequence = 0;
    std::uint64_t timestampNs = 0;
    std::uint64_t completionTimestampNs = 0;
    std::uint64_t plTimestampTicks = 0;
    double centerFrequencyHz = 1.0e9;
    double spanHz = 200.0e6;
    double rbwHz = 0.0;
    double enbwHz = 0.0;
    double calibrationOffsetDb = 0.0;
    std::uint32_t binCount = 0;
    std::uint16_t accumulationFrames = 1;
    std::uint16_t configEpoch = 0;
    std::uint8_t traceMode = 0;
    std::uint8_t windowType = 0;
    std::uint8_t dataFormat = 0;
    AmplitudeUnit unit = AmplitudeUnit::Dbfs;
    bool calibrated = false;
    bool fftShift = false;
    std::uint32_t flags = 0;
    std::uint32_t configurationEpoch = 0;
};

struct SpectrumFrame {
    SpectrumMetadata metadata;

    // Simulation and legacy paths use float dB values.
    std::vector<float> bins;

    // DMA path uses int16 log values with 0.01 dB/LSB.
    // When non-empty, this vector is the authoritative spectrum payload.
    std::vector<std::int16_t> logBins;

    std::size_t binCount() const noexcept
    {
        return logBins.empty() ? bins.size() : logBins.size();
    }

    bool usesLogBins() const noexcept
    {
        return !logBins.empty();
    }

    float binDb(const std::size_t index) const noexcept
    {
        if (index >= binCount()) {
            return -200.0F;
        }
        return usesLogBins()
            ? static_cast<float>(logBins[index]) * 0.01F
                + static_cast<float>(metadata.calibrationOffsetDb)
            : bins[index];
    }

    const std::int16_t* logBinData() const noexcept
    {
        return logBins.empty() ? nullptr : logBins.data();
    }

    bool isConsistent() const noexcept
    {
        const std::size_t count = binCount();
        if (metadata.binCount != count || count == 0U) {
            return false;
        }
        if (usesLogBins()) {
            return bins.empty();
        }
        return bins.size() == count;
    }

    bool hasFiniteBins() const noexcept
    {
        if (usesLogBins()) {
            return logBins.size() == binCount();
        }
        for (const float value : bins) {
            if (!std::isfinite(value)) {
                return false;
            }
        }
        return true;
    }

    double startFrequencyHz() const noexcept
    {
        return metadata.centerFrequencyHz - metadata.spanHz * 0.5;
    }

    double stopFrequencyHz() const noexcept
    {
        return metadata.centerFrequencyHz + metadata.spanHz * 0.5;
    }
};

using SpectrumFramePtr = std::shared_ptr<SpectrumFrame>;
using ConstSpectrumFramePtr = std::shared_ptr<const SpectrumFrame>;

} // namespace rtsa
