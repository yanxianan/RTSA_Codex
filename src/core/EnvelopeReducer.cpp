#include "core/EnvelopeReducer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rtsa {

void EnvelopeReducer::reduce(const float* bins,
                             const std::size_t binCount,
                             const std::size_t pixelWidth,
                             std::vector<EnvelopeColumn>& output)
{
    output.clear();
    if (bins == nullptr || binCount == 0 || pixelWidth == 0) {
        return;
    }

    const std::size_t columnCount = std::min(binCount, pixelWidth);
    output.resize(columnCount);

    for (std::size_t column = 0; column < columnCount; ++column) {
        const std::size_t begin = (column * binCount) / columnCount;
        std::size_t end = ((column + 1U) * binCount) / columnCount;
        end = std::max(end, begin + 1U);
        end = std::min(end, binCount);

        float minimum = std::numeric_limits<float>::infinity();
        float maximum = -std::numeric_limits<float>::infinity();
        bool valid = false;

        for (std::size_t index = begin; index < end; ++index) {
            const float value = bins[index];
            if (!std::isfinite(value)) {
                continue;
            }
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
            valid = true;
        }

        output[column] = EnvelopeColumn {
            valid ? minimum : 0.0F,
            valid ? maximum : 0.0F,
            begin,
            end - 1U,
            valid
        };
    }
}

} // namespace rtsa

