#pragma once

#include <cstddef>
#include <vector>

namespace rtsa {

struct EnvelopeColumn {
    float minimum = 0.0F;
    float maximum = 0.0F;
    std::size_t firstBin = 0;
    std::size_t lastBin = 0;
    bool valid = false;
};

class EnvelopeReducer final {
public:
    static void reduce(const float* bins,
                       std::size_t binCount,
                       std::size_t pixelWidth,
                       std::vector<EnvelopeColumn>& output);
};

} // namespace rtsa

