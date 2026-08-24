#pragma once

#include <QColor>
#include <QVector>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace rtsa {

enum class ColormapPreset : std::uint8_t {
    ClassicRainbow = 0, // Keysight / Tektronix DPX Standard RF Rainbow
    RohdeSchwarz = 1,   // Rohde & Schwarz FSW High-Contrast Marine
    Ironbow = 2,        // FLIR / Signal Hound Thermal Ironbow
    DeepOcean = 3,      // SDR# / Deep Sea Ice Blue
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
