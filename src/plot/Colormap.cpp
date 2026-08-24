#include "plot/Colormap.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rtsa {
namespace {

struct ColorStop {
    float pos;
    QRgb color;
};

QRgb interpolateStops(float x, const std::vector<ColorStop>& stops)
{
    if (stops.empty()) {
        return qRgb(0, 0, 0);
    }
    x = std::clamp(x, 0.0F, 1.0F);
    if (x <= stops.front().pos) {
        return stops.front().color;
    }
    if (x >= stops.back().pos) {
        return stops.back().color;
    }

    for (std::size_t i = 0; i + 1 < stops.size(); ++i) {
        if (x >= stops[i].pos && x <= stops[i + 1].pos) {
            const float t = (x - stops[i].pos) / (stops[i + 1].pos - stops[i].pos);
            const int r = static_cast<int>(std::round(qRed(stops[i].color) + t * (qRed(stops[i + 1].color) - qRed(stops[i].color))));
            const int g = static_cast<int>(std::round(qGreen(stops[i].color) + t * (qGreen(stops[i + 1].color) - qGreen(stops[i].color))));
            const int b = static_cast<int>(std::round(qBlue(stops[i].color) + t * (qBlue(stops[i + 1].color) - qBlue(stops[i].color))));
            return qRgb(std::clamp(r, 0, 255), std::clamp(g, 0, 255), std::clamp(b, 0, 255));
        }
    }
    return stops.back().color;
}

// 1. Keysight / Tektronix DPX Standard RF Rainbow
const std::vector<ColorStop>& classicRainbowStops()
{
    static const std::vector<ColorStop> stops = {
        { 0.00F, qRgb(4, 7, 20) },      // Deep midnight navy (Noise Floor)
        { 0.12F, qRgb(0, 26, 102) },    // Navy Blue
        { 0.28F, qRgb(0, 85, 255) },    // Royal Blue
        { 0.42F, qRgb(0, 212, 255) },   // Bright Cyan (Weak signal pop-out)
        { 0.58F, qRgb(0, 230, 0) },     // Bright Green
        { 0.72F, qRgb(255, 238, 0) },   // Vibrant Yellow
        { 0.86F, qRgb(238, 17, 0) },    // Vivid Crimson Red
        { 0.96F, qRgb(255, 0, 170) },   // Magenta
        { 1.00F, qRgb(255, 255, 255) }  // Hot White (Saturation)
    };
    return stops;
}

// 2. Rohde & Schwarz FSW High-Contrast Marine
const std::vector<ColorStop>& rohdeSchwarzStops()
{
    static const std::vector<ColorStop> stops = {
        { 0.00F, qRgb(0, 8, 28) },      // Deep dark marine
        { 0.20F, qRgb(0, 56, 168) },    // Ocean Blue
        { 0.40F, qRgb(0, 200, 150) },   // Mint Cyan-Green
        { 0.65F, qRgb(255, 208, 0) },   // Golden Yellow
        { 0.85F, qRgb(255, 56, 0) },    // Fiery Orange-Red
        { 1.00F, qRgb(255, 255, 255) }  // Pure White
    };
    return stops;
}

// 3. FLIR / Signal Hound Thermal Ironbow
const std::vector<ColorStop>& ironbowStops()
{
    static const std::vector<ColorStop> stops = {
        { 0.00F, qRgb(0, 0, 0) },       // Pure Black
        { 0.20F, qRgb(56, 0, 104) },    // Deep Purple-Violet
        { 0.40F, qRgb(156, 16, 56) },   // Rust Red
        { 0.65F, qRgb(255, 108, 0) },   // Fire Orange
        { 0.85F, qRgb(255, 230, 0) },   // Bright Yellow
        { 1.00F, qRgb(255, 255, 255) }  // White Hot
    };
    return stops;
}

// 4. SDR# / Deep Sea Ice Blue
const std::vector<ColorStop>& deepOceanStops()
{
    static const std::vector<ColorStop> stops = {
        { 0.00F, qRgb(2, 5, 14) },      // Midnight
        { 0.25F, qRgb(16, 37, 84) },    // Deep Indigo
        { 0.50F, qRgb(26, 100, 184) },  // Sea Blue
        { 0.75F, qRgb(0, 212, 255) },   // Ice Cyan
        { 1.00F, qRgb(255, 255, 255) }  // Pure White
    };
    return stops;
}

} // namespace

QVector<QRgb> Colormap::createColorTable(const ColormapPreset preset)
{
    QVector<QRgb> table(256);
    for (int i = 0; i < 256; ++i) {
        const float x = static_cast<float>(i) / 255.0F;
        switch (preset) {
        case ColormapPreset::RohdeSchwarz:
            table[i] = interpolateStops(x, rohdeSchwarzStops());
            break;
        case ColormapPreset::Ironbow:
            table[i] = interpolateStops(x, ironbowStops());
            break;
        case ColormapPreset::DeepOcean:
            table[i] = interpolateStops(x, deepOceanStops());
            break;
        case ColormapPreset::Grayscale: {
            const int v = std::clamp(static_cast<int>(std::round(x * 255.0F)), 0, 255);
            table[i] = qRgb(v, v, v);
            break;
        }
        case ColormapPreset::ClassicRainbow:
        default:
            table[i] = interpolateStops(x, classicRainbowStops());
            break;
        }
    }
    return table;
}

} // namespace rtsa
