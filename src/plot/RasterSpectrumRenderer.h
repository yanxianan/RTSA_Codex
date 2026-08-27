#pragma once

#include "core/EnvelopeReducer.h"
#include "core/SpectrumFrame.h"

#include <QImage>
#include <QColor>
#include <QLine>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>

#include <cstddef>
#include <array>
#include <optional>
#include <vector>

class QPainter;

namespace rtsa {

constexpr std::size_t kSpectrumMarkerCount = 4U;

class RasterSpectrumRenderer final {
public:
    void setViewport(const QSize& widgetSize, const QRect& plotRect);
    void setAmplitudeScale(float referenceLevel, float bottomLevel);
    void setAppearance(const QColor& traceColor, int traceWidth,
                       bool gridVisible, int themeIndex,
                       const QColor& customBgColor = QColor());
    void setAppearance(const QColor& traceColor, int traceWidth,
                       bool gridVisible, bool lightTheme);
    void setFrame(ConstSpectrumFramePtr frame);
    void setMarkerBins(
        const std::array<std::optional<std::size_t>, kSpectrumMarkerCount>& markerBins,
        std::size_t activeMarkerIndex);
    void invalidateStaticLayer();

    void paint(QPainter& painter);

    QRect plotRect() const noexcept;
    std::size_t envelopeColumnCount() const noexcept;
    std::optional<std::size_t> markerBin(std::size_t markerIndex) const noexcept;

private:
    void rebuildStaticLayer();
    void rebuildGeometry();
    int yForAmplitude(float amplitude) const noexcept;
    QString formatFrequency(double frequencyHz) const;
    QString amplitudeUnitText() const;

    QSize widgetSize_;
    QRect plotRect_;
    QImage staticLayer_;
    bool staticLayerDirty_ = true;
    float referenceLevel_ = 0.0F;
    float bottomLevel_ = -140.0F;
    QColor traceColor_ { 0, 235, 180 };
    int traceWidth_ = 1;
    bool gridVisible_ = true;
    int themeIndex_ = 0;
    QColor customBgColor_;
    ConstSpectrumFramePtr frame_;
    std::uint64_t geometrySequence_ = 0;
    std::vector<EnvelopeColumn> envelope_;
    QVector<QLine> envelopeLines_;
    QVector<QPoint> peakPolyline_;
    std::array<std::optional<std::size_t>, kSpectrumMarkerCount> markerBins_;
    std::size_t activeMarkerIndex_ = 0;
};

} // namespace rtsa
