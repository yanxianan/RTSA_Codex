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
    double centerFrequencyHz = 1.0e9;
    double spanHz = 200.0e6;
    std::uint32_t binCount = 0;
    AmplitudeUnit unit = AmplitudeUnit::Dbfs;
    bool calibrated = false;
    std::uint32_t flags = 0;
    std::uint32_t configurationEpoch = 0;
};

struct SpectrumFrame {
    SpectrumMetadata metadata;
    std::vector<float> bins;

    bool isConsistent() const noexcept
    {
        return metadata.binCount == bins.size() && !bins.empty();
    }

    bool hasFiniteBins() const noexcept
    {
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
