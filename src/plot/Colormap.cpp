#include "plot/Colormap.h"

#include <algorithm>
#include <cmath>

namespace rtsa {
namespace {

QRgb turboColor(float x)
{
    x = std::clamp(x, 0.0F, 1.0F);
    // Google Turbo colormap 4th-order polynomial approximation
    const float r = 0.13572138F + x * (4.61539260F + x * (-42.66032258F + x * (132.13108234F + x * (-152.94239396F + x * 59.28637943F))));
    const float g = 0.09140261F + x * (2.19418839F + x * (4.84296658F + x * (-14.18503333F + x * (4.27729857F + x * 2.82956604F))));
    const float b = 0.10667330F + x * (12.64194608F + x * (-60.58204836F + x * (110.36276771F + x * (-89.90310912F + x * 27.34824973F))));

    const int ir = std::clamp(static_cast<int>(std::round(r * 255.0F)), 0, 255);
    const int ig = std::clamp(static_cast<int>(std::round(g * 255.0F)), 0, 255);
    const int ib = std::clamp(static_cast<int>(std::round(b * 255.0F)), 0, 255);
    return qRgb(ir, ig, ib);
}

QRgb viridisColor(float x)
{
    x = std::clamp(x, 0.0F, 1.0F);
    // Viridis piecewise polynomial approximation
    const float r = std::clamp(0.267F + x * (-0.015F + x * (0.835F + x * (-0.087F))), 0.0F, 1.0F);
    const float g = std::clamp(0.004F + x * (1.100F + x * (-0.350F + x * 0.246F)), 0.0F, 1.0F);
    const float b = std::clamp(0.329F + x * (0.750F + x * (-1.550F + x * 0.471F)), 0.0F, 1.0F);

    const int ir = std::clamp(static_cast<int>(std::round(r * 255.0F)), 0, 255);
    const int ig = std::clamp(static_cast<int>(std::round(g * 255.0F)), 0, 255);
    const int ib = std::clamp(static_cast<int>(std::round(b * 255.0F)), 0, 255);
    return qRgb(ir, ig, ib);
}

QRgb jetColor(float x)
{
    x = std::clamp(x, 0.0F, 1.0F);
    const float r = std::clamp(1.5F - std::abs(4.0F * x - 3.0F), 0.0F, 1.0F);
    const float g = std::clamp(1.5F - std::abs(4.0F * x - 2.0F), 0.0F, 1.0F);
    const float b = std::clamp(1.5F - std::abs(4.0F * x - 1.0F), 0.0F, 1.0F);

    return qRgb(static_cast<int>(r * 255.0F),
                static_cast<int>(g * 255.0F),
                static_cast<int>(b * 255.0F));
}

QRgb hotColor(float x)
{
    x = std::clamp(x, 0.0F, 1.0F);
    const float r = std::clamp(x * (8.0F / 3.0F), 0.0F, 1.0F);
    const float g = std::clamp(x * (8.0F / 3.0F) - 1.0F, 0.0F, 1.0F);
    const float b = std::clamp(x * 4.0F - 3.0F, 0.0F, 1.0F);

    return qRgb(static_cast<int>(r * 255.0F),
                static_cast<int>(g * 255.0F),
                static_cast<int>(b * 255.0F));
}

QRgb grayscaleColor(float x)
{
    const int v = std::clamp(static_cast<int>(std::round(x * 255.0F)), 0, 255);
    return qRgb(v, v, v);
}

} // namespace

QVector<QRgb> Colormap::createColorTable(const ColormapPreset preset)
{
    QVector<QRgb> table(256);
    for (int i = 0; i < 256; ++i) {
        const float x = static_cast<float>(i) / 255.0F;
        switch (preset) {
        case ColormapPreset::Viridis:
            table[i] = viridisColor(x);
            break;
        case ColormapPreset::Jet:
            table[i] = jetColor(x);
            break;
        case ColormapPreset::Hot:
            table[i] = hotColor(x);
            break;
        case ColormapPreset::Grayscale:
            table[i] = grayscaleColor(x);
            break;
        case ColormapPreset::Turbo:
        default:
            table[i] = turboColor(x);
            break;
        }
    }
    return table;
}

} // namespace rtsa
