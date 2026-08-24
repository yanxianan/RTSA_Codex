#pragma once

#include "core/SpectrumFrame.h"

#include <cstddef>

namespace rtsa {

struct RangePeakMeasurement {
    bool valid = false;
    std::size_t bin = 0;
    double frequencyHz = 0.0;
    float amplitude = 0.0F;
    AmplitudeUnit unit = AmplitudeUnit::Dbfs;
    bool calibrated = false;
};

struct ChannelPowerMeasurement {
    bool valid = false;
    double value = 0.0;
    std::size_t integratedBins = 0;
    double startFrequencyHz = 0.0;
    double stopFrequencyHz = 0.0;
    AmplitudeUnit unit = AmplitudeUnit::Dbfs;
    bool calibrated = false;
};

class SpectrumMeasurements final {
public:
    SpectrumMeasurements() = delete;

    static RangePeakMeasurement peakInRange(const SpectrumFrame& frame,
                                              double startFrequencyHz,
                                              double stopFrequencyHz) noexcept;
    static ChannelPowerMeasurement channelPowerInRange(
        const SpectrumFrame& frame,
        double startFrequencyHz,
        double stopFrequencyHz) noexcept;
};

} // namespace rtsa
