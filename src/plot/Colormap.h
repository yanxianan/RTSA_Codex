#pragma once

#include <QColor>
#include <QVector>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace rtsa {

enum class ColormapPreset : std::uint8_t {
    Turbo = 0,
    Viridis = 1,
    Jet = 2,
    Hot = 3,
    Grayscale = 4
};

class Colormap final {
public:
    Colormap() = delete;

    static QVector<QRgb> createColorTable(ColormapPreset preset);

    static inline std::uint8_t mapToColorIndex(float amplitude,
                                               float referenceLevel,
                                               float bottomLevel) noexcept
    {
        if (!std::isfinite(amplitude)) {
            return 0U;
        }
        if (referenceLevel <= bottomLevel) {
            return amplitude >= referenceLevel ? 255U : 0U;
        }
        const float normalized = (amplitude - bottomLevel) / (referenceLevel - bottomLevel);
        const float scaled = normalized * 255.0F;
        const int clamped = std::clamp(static_cast<int>(std::floor(scaled)), 0, 255);
        return static_cast<std::uint8_t>(clamped);
    }
};

} // namespace rtsa
