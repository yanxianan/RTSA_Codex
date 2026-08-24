#include "core/FrequencyMapper.h"

#include <algorithm>
#include <cmath>

namespace rtsa {

double FrequencyMapper::binWidthHz(const SpectrumMetadata& metadata) noexcept
{
    if (metadata.binCount == 0 || metadata.spanHz <= 0.0) {
        return 0.0;
    }
    return metadata.spanHz / static_cast<double>(metadata.binCount);
}

double FrequencyMapper::frequencyForBin(const SpectrumMetadata& metadata,
                                        const std::size_t bin) noexcept
{
    if (metadata.binCount == 0) {
        return metadata.centerFrequencyHz;
    }

    const auto clampedBin = std::min<std::size_t>(bin, metadata.binCount - 1U);
    const double startHz = metadata.centerFrequencyHz - metadata.spanHz * 0.5;
    return startHz + static_cast<double>(clampedBin) * binWidthHz(metadata);
}

std::size_t FrequencyMapper::nearestBinForFrequency(const SpectrumMetadata& metadata,
                                                    const double frequencyHz) noexcept
{
    if (metadata.binCount == 0) {
        return 0;
    }

    const double widthHz = binWidthHz(metadata);
    if (widthHz <= 0.0) {
        return 0;
    }

    const double startHz = metadata.centerFrequencyHz - metadata.spanHz * 0.5;
    const double fractionalBin = (frequencyHz - startHz) / widthHz;
    const auto rounded = static_cast<long long>(std::llround(fractionalBin));
    const auto clamped = std::clamp<long long>(rounded,
                                               0,
                                               static_cast<long long>(metadata.binCount - 1U));
    return static_cast<std::size_t>(clamped);
}

} // namespace rtsa

