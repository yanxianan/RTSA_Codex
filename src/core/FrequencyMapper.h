#pragma once

#include "core/SpectrumFrame.h"

#include <cstddef>

namespace rtsa {

class FrequencyMapper final {
public:
    static double binWidthHz(const SpectrumMetadata& metadata) noexcept;
    static double frequencyForBin(const SpectrumMetadata& metadata, std::size_t bin) noexcept;
    static std::size_t nearestBinForFrequency(const SpectrumMetadata& metadata,
                                              double frequencyHz) noexcept;
};

} // namespace rtsa

