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
                                           const bool lightTheme)
{
    const QColor safeColor = traceColor.isValid() ? traceColor : QColor(0, 235, 180);
    const int safeWidth = std::clamp(traceWidth, 1, 4);
    if (traceColor_ == safeColor && traceWidth_ == safeWidth
        && gridVisible_ == gridVisible && lightTheme_ == lightTheme) {
        return;
    }
    traceColor_ = safeColor;
    traceWidth_ = safeWidth;
    gridVisible_ = gridVisible;
    lightTheme_ = lightTheme;
    staticLayerDirty_ = true;
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

    if (frame_ && !frame_->bins.empty()) {
        const std::array<QColor, kSpectrumMarkerCount> colors {
            QColor(255, 210, 55), QColor(64, 210, 255),
            QColor(255, 115, 145), QColor(175, 235, 95)
        };
        const QFontMetrics metrics(painter.font());
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
            const int y = yForAmplitude(frame_->bins[bin]);
            const bool active = markerIndex == activeMarkerIndex_;
            painter.setPen(QPen(colors[markerIndex], active ? 2 : 1, Qt::DashLine));
            painter.drawLine(x, plotRect_.top(), x, plotRect_.bottom());
            if (active) {
                painter.drawLine(plotRect_.left(), y, plotRect_.right(), y);
            }

            const QString label = QStringLiteral("M%1  %2  %3 %4")
                .arg(markerIndex + 1U)
                .arg(formatFrequency(
                    FrequencyMapper::frequencyForBin(frame_->metadata, bin)))
                .arg(frame_->bins[bin], 0, 'f', 1)
                .arg(amplitudeUnitText());
            const QSize labelSize = metrics.size(Qt::TextSingleLine, label) + QSize(12, 6);
            int labelX = x + 6;
            if (labelX + labelSize.width() > plotRect_.right()) {
                labelX = x - labelSize.width() - 6;
            }
            int labelY = y - labelSize.height() - 6
                + static_cast<int>(markerIndex) * (labelSize.height() + 2);
            labelY = std::clamp(labelY,
                                plotRect_.top(),
                                plotRect_.bottom() - labelSize.height());
            const QRect labelRect(QPoint(labelX, labelY), labelSize);
            painter.fillRect(labelRect, QColor(20, 25, 30, active ? 240 : 210));
            painter.setPen(colors[markerIndex].lighter(active ? 115 : 100));
            painter.drawText(labelRect.adjusted(6, 3, -6, -3),
                             Qt::AlignLeft | Qt::AlignVCenter, label);
        }
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
    const QColor windowColor = lightTheme_ ? QColor(232, 236, 240) : QColor(15, 20, 27);
    const QColor plotColor = lightTheme_ ? QColor(250, 252, 253) : QColor(4, 9, 14);
    const QColor gridColor = lightTheme_ ? QColor(195, 203, 210) : QColor(53, 66, 78);
    const QColor borderColor = lightTheme_ ? QColor(95, 105, 115) : QColor(125, 142, 158);
    const QColor textColor = lightTheme_ ? QColor(35, 42, 48) : QColor(195, 207, 218);
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
    const float range = std::max(1.0F, referenceLevel_ - bottomLevel_);
    const float normalized = std::clamp((referenceLevel_ - amplitude) / range, 0.0F, 1.0F);
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
