#include "plot/RasterSpectrumRenderer.h"

#include "core/AmplitudeUnits.h"
#include "core/FrequencyMapper.h"

#include <QColor>
#include <QFontMetrics>
#include <QPainter>
#include <QPen>
#include <QString>

#include <algorithm>
#include <cmath>

namespace rtsa {

void RasterSpectrumRenderer::setViewport(const QSize& widgetSize, const QRect& plotRect)
{
    if (widgetSize_ == widgetSize && plotRect_ == plotRect) {
        return;
    }
    widgetSize_ = widgetSize;
    plotRect_ = plotRect;
    staticLayerDirty_ = true;
    rebuildGeometry();
}

void RasterSpectrumRenderer::setAmplitudeScale(const float referenceLevel,
                                               const float bottomLevel)
{
    const float safeBottom = std::min(bottomLevel, referenceLevel - 1.0F);
    if (referenceLevel_ == referenceLevel && bottomLevel_ == safeBottom) {
        return;
    }
    referenceLevel_ = referenceLevel;
    bottomLevel_ = safeBottom;
    staticLayerDirty_ = true;
    rebuildGeometry();
}

void RasterSpectrumRenderer::setAppearance(const QColor& traceColor,
                                           const int traceWidth,
                                           const bool gridVisible,
                                           const int themeIndex,
                                           const QColor& customBgColor)
{
    const QColor safeColor = traceColor.isValid() ? traceColor : QColor(0, 235, 180);
    const int safeWidth = std::clamp(traceWidth, 1, 4);
    if (traceColor_ == safeColor && traceWidth_ == safeWidth
        && gridVisible_ == gridVisible && themeIndex_ == themeIndex
        && customBgColor_ == customBgColor) {
        return;
    }
    traceColor_ = safeColor;
    traceWidth_ = safeWidth;
    gridVisible_ = gridVisible;
    themeIndex_ = themeIndex;
    customBgColor_ = customBgColor;
    staticLayerDirty_ = true;
}

void RasterSpectrumRenderer::setAppearance(const QColor& traceColor,
                                           const int traceWidth,
                                           const bool gridVisible,
                                           const bool lightTheme)
{
    setAppearance(traceColor, traceWidth, gridVisible, lightTheme ? 1 : 0, QColor());
}

void RasterSpectrumRenderer::setFrame(ConstSpectrumFramePtr frame)
{
    if (frame_ && frame
        && frame_->metadata.sequence == frame->metadata.sequence
        && frame_->metadata.configurationEpoch == frame->metadata.configurationEpoch) {
        return;
    }

    const bool axesChanged = !frame_ || !frame
        || frame_->metadata.centerFrequencyHz != frame->metadata.centerFrequencyHz
        || frame_->metadata.spanHz != frame->metadata.spanHz
        || frame_->metadata.unit != frame->metadata.unit
        || frame_->metadata.calibrated != frame->metadata.calibrated;
    frame_ = std::move(frame);
    if (axesChanged) {
        staticLayerDirty_ = true;
    }
    rebuildGeometry();
}

void RasterSpectrumRenderer::setMarkerBins(
    const std::array<std::optional<std::size_t>, kSpectrumMarkerCount>& markerBins,
    const std::size_t activeMarkerIndex)
{
    markerBins_ = markerBins;
    activeMarkerIndex_ = std::min(activeMarkerIndex, kSpectrumMarkerCount - 1U);
}

void RasterSpectrumRenderer::invalidateStaticLayer()
{
    staticLayerDirty_ = true;
}

void RasterSpectrumRenderer::paint(QPainter& painter)
{
    if (staticLayerDirty_) {
        rebuildStaticLayer();
    }

    if (!staticLayer_.isNull()) {
        painter.drawImage(QPoint(0, 0), staticLayer_);
    }

    painter.save();
    painter.setClipRect(plotRect_);
    painter.setRenderHint(QPainter::Antialiasing, false);

    if (!envelopeLines_.isEmpty()) {
        QColor envelopeColor = traceColor_.darker(125);
        envelopeColor.setAlpha(130);
        QPen envelopePen(envelopeColor);
        envelopePen.setWidth(1);
        painter.setPen(envelopePen);
        painter.drawLines(envelopeLines_);
    }

    if (peakPolyline_.size() > 1) {
        QPen tracePen(traceColor_);
        tracePen.setWidth(traceWidth_);
        painter.setPen(tracePen);
        painter.drawPolyline(peakPolyline_.constData(), peakPolyline_.size());
    }

    if (frame_ && !frame_->bins.empty() && plotRect_.width() > 10 && plotRect_.height() > 10) {
        const std::array<QColor, kSpectrumMarkerCount> colors {
            QColor(255, 213, 79),  // M1: 琥珀金黄 (Amber Yellow)
            QColor(0, 229, 255),   // M2: 科技青蓝 (Electric Cyan)
            QColor(255, 64, 129),  // M3: 荧光洋红 (Neon Magenta)
            QColor(0, 230, 118)    // M4: 明亮翠绿 (Bright Green)
        };
        const QFont originalFont = painter.font();

        const int minY = std::min(plotRect_.top(), plotRect_.bottom());
        const int maxY = std::max(plotRect_.top(), plotRect_.bottom());

        for (std::size_t markerIndex = 0; markerIndex < markerBins_.size(); ++markerIndex) {
            if (!markerBins_[markerIndex]) {
                continue;
            }
            const std::size_t bin = std::min(
                *markerBins_[markerIndex], frame_->bins.size() - 1U);
            const double ratio = frame_->bins.size() > 1
                ? static_cast<double>(bin) / static_cast<double>(frame_->bins.size() - 1U)
                : 0.0;
            const int x = plotRect_.left() + static_cast<int>(std::lround(
                ratio * static_cast<double>(plotRect_.width() - 1)));
            const float rawAmp = frame_->bins[bin];
            const float safeAmp = std::isfinite(rawAmp) ? rawAmp : bottomLevel_;
            const int y = std::clamp(yForAmplitude(safeAmp), minY, maxY);
            const bool active = markerIndex == activeMarkerIndex_;
            const QColor markerColor = colors[markerIndex];

            // 1. 工业级微弱垂直虚线标尺 (仅从采样点向下落到X轴，不横跨整个屏幕，不遮挡其他信号)
            if (active) {
                QPen guidePen(QColor(markerColor.red(), markerColor.green(), markerColor.blue(), 100), 1, Qt::DotLine);
                painter.setPen(guidePen);
                painter.drawLine(x, y, x, plotRect_.bottom());
            }

            // 2. 曲线峰值倒三角标 (Inverted Triangle Badge ▼) 精准锚定在 (x, y)
            constexpr int kTriHalfWidth = 5;
            constexpr int kTriHeight = 8;
            QPolygonF triangle;
            triangle << QPointF(x, y)
                     << QPointF(x - kTriHalfWidth, y - kTriHeight)
                     << QPointF(x + kTriHalfWidth, y - kTriHeight);

            painter.setBrush(active ? markerColor : QColor(markerColor.red(), markerColor.green(), markerColor.blue(), 140));
            painter.setPen(QPen(active ? Qt::white : markerColor.darker(130), 1.0));
            painter.drawPolygon(triangle);

            // 3. 紧凑型编号标签 (例如 M1 / M2)，位于倒三角上方
            const QString tag = QStringLiteral("M%1").arg(markerIndex + 1U);
            QFont tagFont = originalFont;
            tagFont.setPixelSize(9);
            tagFont.setBold(active);
            painter.setFont(tagFont);

            const int tagW = 20;
            const int tagH = 12;
            int tagY = y - kTriHeight - tagH - 1;
            if (tagY < plotRect_.top() + 2) {
                tagY = y + 4; // 若靠近绘图区顶部则放置在顶点下方
            }
            const int minTagX = plotRect_.left() + 2;
            const int maxTagX = std::max(minTagX, plotRect_.right() - tagW - 2);
            const int tagX = std::clamp(x - tagW / 2, minTagX, maxTagX);
            const QRect tagRect(tagX, tagY, tagW, tagH);

            painter.fillRect(tagRect, QColor(10, 15, 22, active ? 230 : 180));
            painter.setPen(QPen(active ? markerColor : markerColor.darker(110), 1));
            painter.drawRect(tagRect);
            painter.setPen(active ? Qt::white : markerColor);
            painter.drawText(tagRect, Qt::AlignCenter, tag);
        }
        painter.setFont(originalFont);
    }

    painter.restore();
}

QRect RasterSpectrumRenderer::plotRect() const noexcept
{
    return plotRect_;
}

std::size_t RasterSpectrumRenderer::envelopeColumnCount() const noexcept
{
    return envelope_.size();
}

std::optional<std::size_t> RasterSpectrumRenderer::markerBin(
    const std::size_t markerIndex) const noexcept
{
    return markerIndex < markerBins_.size()
        ? markerBins_[markerIndex] : std::optional<std::size_t> {};
}

void RasterSpectrumRenderer::rebuildStaticLayer()
{
    staticLayerDirty_ = false;
    if (widgetSize_.isEmpty()) {
        staticLayer_ = QImage();
        return;
    }

    staticLayer_ = QImage(widgetSize_, QImage::Format_ARGB32_Premultiplied);
    QColor windowColor;
    QColor plotColor;
    QColor gridColor;
    QColor borderColor;
    QColor textColor;

    switch (themeIndex_) {
    case 1: // 明亮浅色 (High Contrast Light)
        windowColor = QColor(232, 236, 240);
        plotColor = QColor(250, 252, 253);
        gridColor = QColor(195, 203, 210);
        borderColor = QColor(95, 105, 115);
        textColor = QColor(35, 42, 48);
        break;
    case 2: // 深海科技 (Deep Navy)
        windowColor = QColor(13, 31, 48);
        plotColor = QColor(7, 19, 30);
        gridColor = QColor(26, 59, 92);
        borderColor = QColor(0, 229, 255);
        textColor = QColor(128, 216, 255);
        break;
    case 3: // 复古纯黑 (Pitch Black / OLED)
        windowColor = QColor(10, 10, 10);
        plotColor = QColor(0, 0, 0);
        gridColor = QColor(38, 38, 38);
        borderColor = QColor(64, 64, 64);
        textColor = QColor(0, 230, 118);
        break;
    case 4: // 自定义背景颜色 (Custom Background)
        if (customBgColor_.isValid()) {
            plotColor = customBgColor_;
            const int l = customBgColor_.lightness();
            if (l >= 128) {
                windowColor = customBgColor_.darker(108);
                gridColor = customBgColor_.darker(125);
                borderColor = customBgColor_.darker(160);
                textColor = QColor(25, 30, 35);
            } else {
                windowColor = customBgColor_.lighter(130);
                gridColor = customBgColor_.lighter(180);
                borderColor = customBgColor_.lighter(240);
                textColor = QColor(220, 230, 240);
            }
        } else {
            windowColor = QColor(15, 20, 27);
            plotColor = QColor(4, 9, 14);
            gridColor = QColor(53, 66, 78);
            borderColor = QColor(125, 142, 158);
            textColor = QColor(195, 207, 218);
        }
        break;
    case 0: // 经典深黑 (Classic Dark)
    default:
        windowColor = QColor(15, 20, 27);
        plotColor = QColor(4, 9, 14);
        gridColor = QColor(53, 66, 78);
        borderColor = QColor(125, 142, 158);
        textColor = QColor(195, 207, 218);
        break;
    }

    staticLayer_.fill(windowColor);
    QPainter painter(&staticLayer_);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(plotRect_, plotColor);

    painter.setPen(QPen(gridColor, 1));
    constexpr int divisions = 10;
    if (gridVisible_) {
        for (int division = 0; division <= divisions; ++division) {
            const int x = plotRect_.left() + division * plotRect_.width() / divisions;
            const int y = plotRect_.top() + division * plotRect_.height() / divisions;
            painter.drawLine(x, plotRect_.top(), x, plotRect_.bottom());
            painter.drawLine(plotRect_.left(), y, plotRect_.right(), y);
        }
    }

    painter.setPen(QPen(borderColor, 1));
    painter.drawRect(plotRect_);
    painter.setPen(textColor);

    const QFontMetrics metrics(painter.font());
    for (int division = 0; division <= divisions; ++division) {
        const float amplitude = referenceLevel_
            - static_cast<float>(division) * (referenceLevel_ - bottomLevel_) / divisions;
        const int y = plotRect_.top() + division * plotRect_.height() / divisions;
        const QString label = QStringLiteral("%1").arg(amplitude, 0, 'f', 0);
        painter.drawText(QRect(2, y - metrics.height() / 2, plotRect_.left() - 8, metrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter,
                         label);
    }

    if (frame_) {
        const double startHz = frame_->startFrequencyHz();
        const double stopHz = frame_->stopFrequencyHz();
        for (int division = 0; division <= divisions; division += 2) {
            const double frequency = startHz
                + (stopHz - startHz) * static_cast<double>(division) / divisions;
            const int x = plotRect_.left() + division * plotRect_.width() / divisions;
            const QString label = formatFrequency(frequency);
            const int width = metrics.horizontalAdvance(label) + 6;
            painter.drawText(QRect(x - width / 2,
                                   plotRect_.bottom() + 7,
                                   width,
                                   metrics.height()),
                             Qt::AlignCenter,
                             label);
        }
        painter.drawText(QRect(plotRect_.left(), 4, plotRect_.width(), metrics.height()),
                         Qt::AlignCenter,
                         QStringLiteral("Center %1    Span %2")
                             .arg(formatFrequency(frame_->metadata.centerFrequencyHz))
                             .arg(formatFrequency(frame_->metadata.spanHz)));
    }

    painter.save();
    painter.translate(10, plotRect_.center().y());
    painter.rotate(-90.0);
    painter.drawText(QRect(-plotRect_.height() / 2, 0, plotRect_.height(), metrics.height()),
                     Qt::AlignCenter,
                     amplitudeUnitText());
    painter.restore();
}

void RasterSpectrumRenderer::rebuildGeometry()
{
    envelope_.clear();
    envelopeLines_.clear();
    peakPolyline_.clear();
    geometrySequence_ = 0;

    if (!frame_ || !frame_->isConsistent() || plotRect_.width() <= 0 || plotRect_.height() <= 0) {
        return;
    }

    EnvelopeReducer::reduce(frame_->bins.data(),
                            frame_->bins.size(),
                            static_cast<std::size_t>(plotRect_.width()),
                            envelope_);
    envelopeLines_.reserve(static_cast<qsizetype>(envelope_.size()));
    peakPolyline_.reserve(static_cast<qsizetype>(envelope_.size()));

    const double horizontalScale = envelope_.size() > 1U
        ? static_cast<double>(plotRect_.width() - 1)
            / static_cast<double>(envelope_.size() - 1U)
        : 0.0;
    for (std::size_t column = 0; column < envelope_.size(); ++column) {
        const auto& value = envelope_[column];
        if (!value.valid) {
            continue;
        }
        const int x = plotRect_.left()
            + static_cast<int>(std::lround(static_cast<double>(column) * horizontalScale));
        const int topY = yForAmplitude(value.maximum);
        const int bottomY = yForAmplitude(value.minimum);
        envelopeLines_.append(QLine(x, topY, x, bottomY));
        peakPolyline_.append(QPoint(x, topY));
    }
    geometrySequence_ = frame_->metadata.sequence;
}

int RasterSpectrumRenderer::yForAmplitude(const float amplitude) const noexcept
{
    const float safeAmp = std::isfinite(amplitude) ? amplitude : bottomLevel_;
    const float range = std::max(1.0F, referenceLevel_ - bottomLevel_);
    const float normalized = std::clamp((referenceLevel_ - safeAmp) / range, 0.0F, 1.0F);
    return plotRect_.top()
        + static_cast<int>(std::lround(normalized * static_cast<float>(plotRect_.height())));
}

QString RasterSpectrumRenderer::formatFrequency(const double frequencyHz) const
{
    const double absolute = std::abs(frequencyHz);
    if (absolute >= 1.0e9) {
        return QStringLiteral("%1 GHz").arg(frequencyHz / 1.0e9, 0, 'f', 3);
    }
    if (absolute >= 1.0e6) {
        return QStringLiteral("%1 MHz").arg(frequencyHz / 1.0e6, 0, 'f', 3);
    }
    if (absolute >= 1.0e3) {
        return QStringLiteral("%1 kHz").arg(frequencyHz / 1.0e3, 0, 'f', 3);
    }
    return QStringLiteral("%1 Hz").arg(frequencyHz, 0, 'f', 1);
}

QString RasterSpectrumRenderer::amplitudeUnitText() const
{
    if (!frame_) {
        return QStringLiteral("dBFS");
    }
    QString text = QString::fromLatin1(amplitudeUnitSymbol(frame_->metadata.unit));
    if (!frame_->metadata.calibrated) {
        text += QStringLiteral(" · 未校准");
    }
    return text;
}

} // namespace rtsa
