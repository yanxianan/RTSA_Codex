#include "core/SpectrumMeasurements.h"

#include "core/FrequencyMapper.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rtsa {
namespace {

struct BinRange {
    bool valid = false;
    std::size_t first = 0;
    std::size_t end = 0;
    double startFrequencyHz = 0.0;
    double stopFrequencyHz = 0.0;
};

BinRange mapRange(const SpectrumFrame& frame,
                  const double requestedStartHz,
                  const double requestedStopHz) noexcept
{
    if (!frame.isConsistent() || frame.metadata.binCount < 2U
        || !std::isfinite(frame.metadata.centerFrequencyHz)
        || !std::isfinite(frame.metadata.spanHz)
        || frame.metadata.spanHz <= 0.0
        || !std::isfinite(requestedStartHz)
        || !std::isfinite(requestedStopHz)
        || requestedStartHz >= requestedStopHz) {
        return {};
    }

    const double viewStartHz = frame.startFrequencyHz();
    const double viewStopHz = frame.stopFrequencyHz();
    const double startHz = std::max(requestedStartHz, viewStartHz);
    const double stopHz = std::min(requestedStopHz, viewStopHz);
    if (startHz >= stopHz) {
        return {};
    }

    const double binWidthHz = FrequencyMapper::binWidthHz(frame.metadata);
    if (!std::isfinite(binWidthHz) || binWidthHz <= 0.0) {
        return {};
    }
    const double firstPosition = (startHz - viewStartHz) / binWidthHz;
    const double endPosition = (stopHz - viewStartHz) / binWidthHz;
    const auto first = static_cast<std::size_t>(std::clamp(
        std::ceil(firstPosition), 0.0, static_cast<double>(frame.bins.size())));
    const auto end = static_cast<std::size_t>(std::clamp(
        std::ceil(endPosition), 0.0, static_cast<double>(frame.bins.size())));
    if (first >= end) {
        return {};
    }
    return BinRange { true, first, end, startHz, stopHz };
}

} // namespace

RangePeakMeasurement SpectrumMeasurements::peakInRange(
    const SpectrumFrame& frame,
    const double startFrequencyHz,
    const double stopFrequencyHz) noexcept
{
    const BinRange range = mapRange(frame, startFrequencyHz, stopFrequencyHz);
    if (!range.valid) {
        return {};
    }

    std::size_t peakBin = range.first;
    float peak = -std::numeric_limits<float>::infinity();
    for (std::size_t bin = range.first; bin < range.end; ++bin) {
        const float value = frame.bins[bin];
        if (!std::isfinite(value)) {
            return {};
        }
        if (value > peak) {
            peak = value;
            peakBin = bin;
        }
    }
    return RangePeakMeasurement {
        true,
        peakBin,
        FrequencyMapper::frequencyForBin(frame.metadata, peakBin),
        peak,
        frame.metadata.unit,
        frame.metadata.calibrated
    };
}

ChannelPowerMeasurement SpectrumMeasurements::channelPowerInRange(
    const SpectrumFrame& frame,
    const double startFrequencyHz,
    const double stopFrequencyHz) noexcept
{
    const BinRange range = mapRange(frame, startFrequencyHz, stopFrequencyHz);
    if (!range.valid) {
        return {};
    }

    double linearSum = 0.0;
    for (std::size_t bin = range.first; bin < range.end; ++bin) {
        const double value = static_cast<double>(frame.bins[bin]);
        if (!std::isfinite(value)) {
            return {};
        }
        if (frame.metadata.unit == AmplitudeUnit::LinearPower) {
            if (value < 0.0) {
                return {};
            }
            linearSum += value;
        } else {
            linearSum += std::pow(10.0, value / 10.0);
        }
    }
    if (!std::isfinite(linearSum) || linearSum <= 0.0) {
        return {};
    }
    const double result = frame.metadata.unit == AmplitudeUnit::LinearPower
        ? linearSum : 10.0 * std::log10(linearSum);
    return ChannelPowerMeasurement {
        true,
        result,
        range.end - range.first,
        range.startFrequencyHz,
        range.stopFrequencyHz,
        frame.metadata.unit,
        frame.metadata.calibrated
    };
}

} // namespace rtsa
