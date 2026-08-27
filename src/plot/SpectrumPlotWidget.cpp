#include "plot/SpectrumPlotWidget.h"

#include "core/AmplitudeUnits.h"
#include "core/FrequencyMapper.h"

#include <QApplication>
#include <QGestureEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPanGesture>
#include <QPainter>
#include <QPaintEvent>
#include <QPinchGesture>
#include <QResizeEvent>
#include <QRubberBand>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace rtsa {

SpectrumPlotWidget::SpectrumPlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(480, 320);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    grabGesture(Qt::PinchGesture);
    grabGesture(Qt::PanGesture);
    renderer_.setViewport(size(), calculatePlotRect());
}

bool SpectrumPlotWidget::event(QEvent* event)
{
    if (event->type() == QEvent::Gesture && frame_) {
        auto* gestureEvent = static_cast<QGestureEvent*>(event);
        if (auto* pinch = static_cast<QPinchGesture*>(
                gestureEvent->gesture(Qt::PinchGesture))) {
            if (pinch->changeFlags().testFlag(QPinchGesture::ScaleFactorChanged)
                && pinch->scaleFactor() > 0.0) {
                emit spanScaleRequested(1.0 / pinch->scaleFactor(),
                                        frame_->metadata.centerFrequencyHz);
            }
        }
        if (auto* pan = static_cast<QPanGesture*>(
                gestureEvent->gesture(Qt::PanGesture))) {
            if (renderer_.plotRect().width() > 1 && pan->delta().x() != 0.0) {
                emit frequencyPanRequested(-pan->delta().x()
                    * frame_->metadata.spanHz
                    / static_cast<double>(renderer_.plotRect().width() - 1));
            }
        }
        gestureEvent->accept();
        return true;
    }
    return QWidget::event(event);
}

void SpectrumPlotWidget::setFrame(ConstSpectrumFramePtr frame)
{
    frame_ = std::move(frame);
    renderer_.setFrame(frame_);
    synchronizeMarkers();
    update();
}

ConstSpectrumFramePtr SpectrumPlotWidget::frame() const noexcept
{
    return frame_;
}

void SpectrumPlotWidget::setAmplitudeScale(const float referenceLevel, const float bottomLevel)
{
    renderer_.setAmplitudeScale(referenceLevel, bottomLevel);
    update();
}

void SpectrumPlotWidget::setAppearance(const QColor& traceColor,
                                       const int traceWidth,
                                       const bool gridVisible,
                                       const int themeIndex,
                                       const QColor& customBgColor)
{
    renderer_.setAppearance(traceColor, traceWidth, gridVisible, themeIndex, customBgColor);
    update();
}

void SpectrumPlotWidget::setAppearance(const QColor& traceColor,
                                       const int traceWidth,
                                       const bool gridVisible,
                                       const bool lightTheme)
{
    renderer_.setAppearance(traceColor, traceWidth, gridVisible, lightTheme ? 1 : 0);
    update();
}

void SpectrumPlotWidget::setActiveMarker(const std::size_t markerIndex)
{
    activeMarkerIndex_ = std::min(markerIndex, kSpectrumMarkerCount - 1U);
    updateRendererMarkers();
    publishActiveMarker();
    update();
}

std::size_t SpectrumPlotWidget::activeMarker() const noexcept
{
    return activeMarkerIndex_;
}

void SpectrumPlotWidget::clearMarker()
{
    markerFrequenciesHz_[activeMarkerIndex_].reset();
    updateRendererMarkers();
    emit markerCleared();
    update();
}

void SpectrumPlotWidget::clearAllMarkers()
{
    for (auto& frequency : markerFrequenciesHz_) {
        frequency.reset();
    }
    updateRendererMarkers();
    emit markerCleared();
    update();
}

void SpectrumPlotWidget::peakSearch()
{
    if (!frame_ || frame_->bins.empty()) {
        return;
    }

    const auto peaks = findPeaks();
    std::size_t bestBin = 0U;
    float maxAmp = -1e9F;
    bool found = false;

    if (!peaks.empty()) {
        for (const std::size_t p : peaks) {
            if (frame_->bins[p] > maxAmp) {
                maxAmp = frame_->bins[p];
                bestBin = p;
                found = true;
            }
        }
    } else {
        const auto peak = std::max_element(
            frame_->bins.cbegin(), frame_->bins.cend(), [](float left, float right) {
                if (!std::isfinite(left)) {
                    return true;
                }
                if (!std::isfinite(right)) {
                    return false;
                }
                return left < right;
            });
        if (peak != frame_->bins.cend() && std::isfinite(*peak) && *peak >= peakThreshold_) {
            bestBin = static_cast<std::size_t>(std::distance(frame_->bins.cbegin(), peak));
            found = true;
        }
    }

    if (!found) {
        return;
    }

    markerFrequenciesHz_[activeMarkerIndex_] =
        FrequencyMapper::frequencyForBin(frame_->metadata, bestBin);
    updateRendererMarkers();
    publishActiveMarker();
    update();
}

void SpectrumPlotWidget::nextPeak()
{
    searchAdjacentPeak(1);
}

void SpectrumPlotWidget::previousPeak()
{
    searchAdjacentPeak(-1);
}

void SpectrumPlotWidget::setPeakThreshold(const float threshold)
{
    if (std::isfinite(threshold)) {
        peakThreshold_ = threshold;
    }
}

float SpectrumPlotWidget::peakThreshold() const noexcept
{
    return peakThreshold_;
}

void SpectrumPlotWidget::setDeltaMarkerEnabled(const bool enabled)
{
    deltaMarkerEnabled_ = enabled;
}

bool SpectrumPlotWidget::deltaMarkerEnabled() const noexcept
{
    return deltaMarkerEnabled_;
}

void SpectrumPlotWidget::setMarkerFrequency(const std::size_t markerIndex, const double frequencyHz)
{
    if (markerIndex >= kSpectrumMarkerCount) {
        return;
    }
    if (std::isfinite(frequencyHz)) {
        markerFrequenciesHz_[markerIndex] = frequencyHz;
    } else {
        markerFrequenciesHz_[markerIndex].reset();
    }
    updateRendererMarkers();
    if (markerIndex == activeMarkerIndex_) {
        publishActiveMarker();
    }
    update();
}

void SpectrumPlotWidget::setMarkerEnabled(const std::size_t markerIndex, const bool enabled)
{
    if (markerIndex >= kSpectrumMarkerCount) {
        return;
    }
    if (enabled) {
        if (!markerFrequenciesHz_[markerIndex]) {
            if (frame_ && !frame_->bins.empty()) {
                markerFrequenciesHz_[markerIndex] = frame_->metadata.centerFrequencyHz;
            } else {
                markerFrequenciesHz_[markerIndex] = 1.0e9;
            }
        }
    } else {
        markerFrequenciesHz_[markerIndex].reset();
    }
    updateRendererMarkers();
    publishActiveMarker();
    update();
}

bool SpectrumPlotWidget::isMarkerEnabled(const std::size_t markerIndex) const noexcept
{
    return markerIndex < kSpectrumMarkerCount && markerFrequenciesHz_[markerIndex].has_value();
}

std::optional<double> SpectrumPlotWidget::markerFrequency(const std::size_t markerIndex) const noexcept
{
    if (markerIndex < kSpectrumMarkerCount) {
        return markerFrequenciesHz_[markerIndex];
    }
    return std::nullopt;
}

std::size_t SpectrumPlotWidget::envelopeColumnCount() const noexcept
{
    return renderer_.envelopeColumnCount();
}

std::optional<std::size_t> SpectrumPlotWidget::markerBin() const noexcept
{
    return markerBin(activeMarkerIndex_);
}

std::optional<std::size_t> SpectrumPlotWidget::markerBin(
    const std::size_t markerIndex) const noexcept
{
    return renderer_.markerBin(markerIndex);
}

MarkerMeasurement SpectrumPlotWidget::markerMeasurement(
    const std::size_t markerIndex) const noexcept
{
    if (markerIndex >= markerFrequenciesHz_.size()
        || !markerFrequenciesHz_[markerIndex]
        || !frame_ || frame_->bins.empty()) {
        return {};
    }
    const double frequency = *markerFrequenciesHz_[markerIndex];
    if (!std::isfinite(frequency)
        || frequency < frame_->startFrequencyHz()
        || frequency >= frame_->stopFrequencyHz()) {
        return {};
    }
    const std::size_t bin = FrequencyMapper::nearestBinForFrequency(
        frame_->metadata, frequency);
    if (bin >= frame_->bins.size() || !std::isfinite(frame_->bins[bin])) {
        return {};
    }
    return MarkerMeasurement {
        true,
        bin,
        FrequencyMapper::frequencyForBin(frame_->metadata, bin),
        frame_->bins[bin]
    };
}

DeltaMarkerMeasurement SpectrumPlotWidget::deltaMarkerMeasurement() const noexcept
{
    if (activeMarkerIndex_ == 0U) {
        return deltaMarkerMeasurement(1U, 0U);
    }
    return deltaMarkerMeasurement(activeMarkerIndex_, 0U);
}

DeltaMarkerMeasurement SpectrumPlotWidget::deltaMarkerMeasurement(
    const std::size_t markerIndex, const std::size_t referenceIndex) const noexcept
{
    if (markerIndex == referenceIndex || markerIndex >= kSpectrumMarkerCount || referenceIndex >= kSpectrumMarkerCount) {
        return {};
    }
    const MarkerMeasurement reference = markerMeasurement(referenceIndex);
    const MarkerMeasurement target = markerMeasurement(markerIndex);
    if (!reference.valid || !target.valid) {
        return {};
    }
    return DeltaMarkerMeasurement {
        true,
        target.frequencyHz - reference.frequencyHz,
        target.amplitude - reference.amplitude
    };
}

double SpectrumPlotWidget::lastPaintMilliseconds() const noexcept
{
    return lastPaintMilliseconds_;
}

void SpectrumPlotWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    paintTimer_.restart();
    QPainter painter(this);
    renderer_.paint(painter);
    painter.end();
    lastPaintMilliseconds_ = static_cast<double>(paintTimer_.nsecsElapsed()) / 1.0e6;
    if (frame_) {
        emit framePainted(frame_->metadata.publicationSequence,
                          frame_->metadata.timestampNs);
    }
}

void SpectrumPlotWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    renderer_.setViewport(event->size(), calculatePlotRect());
}

void SpectrumPlotWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !frame_ || frame_->bins.empty()
        || !renderer_.plotRect().contains(mousePosition(event))) {
        QWidget::mousePressEvent(event);
        return;
    }

    leftButtonPressed_ = true;
    panning_ = false;
    boxZooming_ = event->modifiers().testFlag(Qt::ShiftModifier);
    pressPosition_ = mousePosition(event);
    lastPanPosition_ = pressPosition_;
    if (boxZooming_) {
        if (!zoomBand_) {
            zoomBand_ = new QRubberBand(QRubberBand::Rectangle, this);
        }
        zoomBand_->setGeometry(QRect(pressPosition_, QSize()).normalized());
        zoomBand_->show();
    }
    setFocus(Qt::MouseFocusReason);
    event->accept();
}

void SpectrumPlotWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!leftButtonPressed_ || !frame_ || renderer_.plotRect().width() <= 1) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPoint position = mousePosition(event);
    if (boxZooming_) {
        const QRect selection = QRect(pressPosition_, position).normalized()
            .intersected(renderer_.plotRect());
        zoomBand_->setGeometry(selection);
        event->accept();
        return;
    }
    if (!panning_
        && (position - pressPosition_).manhattanLength() >= QApplication::startDragDistance()) {
        panning_ = true;
        setCursor(Qt::ClosedHandCursor);
    }

    if (panning_) {
        const int deltaPixels = position.x() - lastPanPosition_.x();
        if (deltaPixels != 0) {
            const double centerShiftHz = -static_cast<double>(deltaPixels)
                * frame_->metadata.spanHz
                / static_cast<double>(renderer_.plotRect().width() - 1);
            emit frequencyPanRequested(centerShiftHz);
            lastPanPosition_ = position;
        }
    }
    event->accept();
}

void SpectrumPlotWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !leftButtonPressed_) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const bool wasBoxZooming = boxZooming_;
    leftButtonPressed_ = false;
    panning_ = false;
    boxZooming_ = false;
    unsetCursor();
    if (zoomBand_) {
        zoomBand_->hide();
    }
    if (wasBoxZooming) {
        const QRect selection = QRect(pressPosition_, mousePosition(event)).normalized()
            .intersected(renderer_.plotRect());
        if (selection.width() >= QApplication::startDragDistance()) {
            const std::size_t firstBin = binAtPosition(
                QPoint(selection.left(), selection.center().y()));
            const std::size_t lastBin = binAtPosition(
                QPoint(selection.right(), selection.center().y()));
            double startHz = FrequencyMapper::frequencyForBin(frame_->metadata, firstBin);
            double stopHz = FrequencyMapper::frequencyForBin(frame_->metadata, lastBin)
                + FrequencyMapper::binWidthHz(frame_->metadata);
            if (startHz > stopHz) {
                std::swap(startHz, stopHz);
            }
            emit frequencyRangeSelected(startHz, stopHz);
        }
    }
    event->accept();
}

void SpectrumPlotWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        leftButtonPressed_ = false;
        panning_ = false;
        boxZooming_ = false;
        if (zoomBand_) {
            zoomBand_->hide();
        }
        unsetCursor();
        emit frequencyRangeResetRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void SpectrumPlotWidget::wheelEvent(QWheelEvent* event)
{
    if (!frame_ || !renderer_.plotRect().contains(wheelPosition(event))) {
        QWidget::wheelEvent(event);
        return;
    }
    const double scale = event->angleDelta().y() > 0 ? 0.8 : 1.25;
    const auto bin = binAtPosition(wheelPosition(event));
    emit spanScaleRequested(scale, FrequencyMapper::frequencyForBin(frame_->metadata, bin));
    event->accept();
}

void SpectrumPlotWidget::keyPressEvent(QKeyEvent* event)
{
    if (!frame_) {
        QWidget::keyPressEvent(event);
        return;
    }

    const double centerHz = frame_->metadata.centerFrequencyHz;
    switch (event->key()) {
    case Qt::Key_Home:
    case Qt::Key_R:
        emit frequencyRangeResetRequested();
        break;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        emit spanScaleRequested(0.8, centerHz);
        break;
    case Qt::Key_Minus:
        emit spanScaleRequested(1.25, centerHz);
        break;
    case Qt::Key_Left:
        emit frequencyPanRequested(-frame_->metadata.spanHz * 0.1);
        break;
    case Qt::Key_Right:
        emit frequencyPanRequested(frame_->metadata.spanHz * 0.1);
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    event->accept();
}

QRect SpectrumPlotWidget::calculatePlotRect() const
{
    constexpr int leftMargin = 76;
    constexpr int topMargin = 30;
    constexpr int rightMargin = 20;
    constexpr int bottomMargin = 54;
    return rect().adjusted(leftMargin, topMargin, -rightMargin, -bottomMargin);
}

std::size_t SpectrumPlotWidget::binAtPosition(const QPoint& position) const
{
    if (!frame_ || frame_->bins.empty() || renderer_.plotRect().width() <= 1) {
        return 0;
    }
    const double normalized = std::clamp(
        static_cast<double>(position.x() - renderer_.plotRect().left())
            / static_cast<double>(renderer_.plotRect().width() - 1),
        0.0,
        1.0);
    return static_cast<std::size_t>(std::llround(
        normalized * static_cast<double>(frame_->bins.size() - 1U)));
}

QPoint SpectrumPlotWidget::mousePosition(const QMouseEvent* event) const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

QPoint SpectrumPlotWidget::wheelPosition(const QWheelEvent* event) const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

void SpectrumPlotWidget::selectMarkerAt(const QPoint& position)
{
    if (!frame_ || frame_->bins.empty()) {
        return;
    }
    const auto bin = binAtPosition(position);
    markerFrequenciesHz_[activeMarkerIndex_] =
        FrequencyMapper::frequencyForBin(frame_->metadata, bin);
    updateRendererMarkers();
    publishActiveMarker();
    update();
}

void SpectrumPlotWidget::synchronizeMarkers()
{
    const bool activeWasSet = markerFrequenciesHz_[activeMarkerIndex_].has_value();
    if (!frame_ || frame_->bins.empty()) {
        updateRendererMarkers();
        if (activeWasSet) {
            emit markerCleared();
        }
        return;
    }

    const double startHz = frame_->startFrequencyHz();
    const double stopHz = frame_->stopFrequencyHz();
    for (auto& frequency : markerFrequenciesHz_) {
        if (frequency
            && (!std::isfinite(*frequency)
                || *frequency < startHz || *frequency >= stopHz)) {
            frequency.reset();
        }
    }
    updateRendererMarkers();
    if (!markerFrequenciesHz_[activeMarkerIndex_]) {
        if (activeWasSet) {
            emit markerCleared();
        }
        return;
    }
    publishActiveMarker();
}

void SpectrumPlotWidget::publishActiveMarker()
{
    const MarkerMeasurement measurement = markerMeasurement(activeMarkerIndex_);
    if (!measurement.valid || !frame_) {
        emit markerCleared();
        return;
    }

    emit markerChanged(measurement.frequencyHz, measurement.amplitude,
                       QString::fromLatin1(amplitudeUnitSymbol(frame_->metadata.unit)),
                       frame_->metadata.calibrated);
}

void SpectrumPlotWidget::updateRendererMarkers()
{
    std::array<std::optional<std::size_t>, kSpectrumMarkerCount> bins;
    if (frame_ && !frame_->bins.empty()) {
        for (std::size_t index = 0; index < markerFrequenciesHz_.size(); ++index) {
            if (!markerFrequenciesHz_[index]) {
                continue;
            }
            const double frequency = *markerFrequenciesHz_[index];
            if (std::isfinite(frequency)
                && frequency >= frame_->startFrequencyHz()
                && frequency < frame_->stopFrequencyHz()) {
                bins[index] = FrequencyMapper::nearestBinForFrequency(
                    frame_->metadata, frequency);
            }
        }
    }
    renderer_.setMarkerBins(bins, activeMarkerIndex_);
}

std::vector<std::size_t> SpectrumPlotWidget::findPeaks() const
{
    std::vector<std::size_t> peaks;
    if (!frame_ || frame_->bins.size() < 3) {
        return peaks;
    }

    const auto& data = frame_->bins;
    const std::size_t n = data.size();
    constexpr float kExcursion = 3.0F; // 工业标准 3 dB 峰值凸起高度 (Peak Excursion)

    for (std::size_t i = 0; i < n; ++i) {
        const float val = data[i];
        if (!std::isfinite(val) || val < peakThreshold_) {
            continue;
        }

        // 1. 局部极大值条件（不低于相邻有效采样点）
        const bool notBelowLeft = (i == 0 || !std::isfinite(data[i - 1]) || val >= data[i - 1]);
        const bool notBelowRight = (i + 1 == n || !std::isfinite(data[i + 1]) || val >= data[i + 1]);
        if (!notBelowLeft || !notBelowRight) {
            continue;
        }

        // 如果是平顶峰，仅保留平顶区间的首个点避免重复
        if (i > 0 && data[i - 1] == val) {
            continue;
        }

        // 2. 向左检测谷底落差：在遇到更高点之前，必须向下掉落至少 kExcursion
        float leftMin = val;
        bool leftValid = false;
        for (std::size_t l = i; l > 0; --l) {
            const float lv = data[l - 1];
            if (!std::isfinite(lv)) {
                break;
            }
            if (lv > val) {
                // 遇到更高点，说明当前点仅处于某个更高主峰的侧翼上，不是独立山头
                break;
            }
            if (lv < leftMin) {
                leftMin = lv;
            }
            if (val - leftMin >= kExcursion) {
                leftValid = true;
                break;
            }
        }
        // 若向左已达边界且未遇更高点，且落差 >= 1.0 dB（或靠近边界），则认可左侧
        if (!leftValid && (i <= 3 || val - leftMin >= 1.0F)) {
            leftValid = true;
        }

        if (!leftValid) {
            continue;
        }

        // 3. 向右检测谷底落差：在遇到更高点之前，必须向下掉落至少 kExcursion
        float rightMin = val;
        bool rightValid = false;
        for (std::size_t r = i + 1; r < n; ++r) {
            const float rv = data[r];
            if (!std::isfinite(rv)) {
                break;
            }
            if (rv > val) {
                // 遇到更高点，说明当前点仅处于某个更高主峰的侧翼上
                break;
            }
            if (rv < rightMin) {
                rightMin = rv;
            }
            if (val - rightMin >= kExcursion) {
                rightValid = true;
                break;
            }
        }
        // 若向右已达边界且未遇更高点，且落差 >= 1.0 dB（或靠近边界），则认可右侧
        if (!rightValid && (i + 4 >= n || val - rightMin >= 1.0F)) {
            rightValid = true;
        }

        if (leftValid && rightValid) {
            peaks.push_back(i);
        }
    }

    // 4. 峰值非极大值抑制（Non-Maximum Suppression）：
    // 若两个峰值之间未下潜落差至 kExcursion（属于同一主瓣内未彻底分开的起伏），仅保留更高者
    if (peaks.size() > 1) {
        std::vector<std::size_t> filtered;
        for (std::size_t p : peaks) {
            if (filtered.empty()) {
                filtered.push_back(p);
                continue;
            }
            const std::size_t prev = filtered.back();
            float valley = std::min(data[prev], data[p]);
            for (std::size_t k = prev + 1; k < p; ++k) {
                if (std::isfinite(data[k]) && data[k] < valley) {
                    valley = data[k];
                }
            }
            if (data[prev] - valley < kExcursion && data[p] - valley < kExcursion) {
                if (data[p] > data[prev]) {
                    filtered.back() = p;
                }
            } else {
                filtered.push_back(p);
            }
        }
        peaks = std::move(filtered);
    }

    return peaks;
}

bool SpectrumPlotWidget::isPeak(const std::size_t bin) const noexcept
{
    if (!frame_ || bin >= frame_->bins.size()) {
        return false;
    }
    const auto peaks = findPeaks();
    return std::find(peaks.begin(), peaks.end(), bin) != peaks.end();
}

void SpectrumPlotWidget::searchAdjacentPeak(const int direction)
{
    if (!frame_ || frame_->bins.empty() || direction == 0) {
        return;
    }
    const MarkerMeasurement current = markerMeasurement(activeMarkerIndex_);
    if (!current.valid) {
        peakSearch();
        return;
    }

    const auto peaks = findPeaks();
    if (peaks.empty()) {
        return;
    }

    if (direction > 0) {
        // 向右搜索（频率更高）：找第一个 bin > current.bin 的独立真峰
        for (const std::size_t p : peaks) {
            if (p > current.bin) {
                markerFrequenciesHz_[activeMarkerIndex_] =
                    FrequencyMapper::frequencyForBin(frame_->metadata, p);
                updateRendererMarkers();
                publishActiveMarker();
                update();
                return;
            }
        }
        // 循环跳转到最左侧峰 (Wrap around)
        const std::size_t first = peaks.front();
        if (first != current.bin) {
            markerFrequenciesHz_[activeMarkerIndex_] =
                FrequencyMapper::frequencyForBin(frame_->metadata, first);
            updateRendererMarkers();
            publishActiveMarker();
            update();
        }
    } else {
        // 向左搜索（频率更低）：逆序找第一个 bin < current.bin 的独立真峰
        for (auto it = peaks.rbegin(); it != peaks.rend(); ++it) {
            if (*it < current.bin) {
                markerFrequenciesHz_[activeMarkerIndex_] =
                    FrequencyMapper::frequencyForBin(frame_->metadata, *it);
                updateRendererMarkers();
                publishActiveMarker();
                update();
                return;
            }
        }
        // 循环跳转到最右侧峰 (Wrap around)
        const std::size_t last = peaks.back();
        if (last != current.bin) {
            markerFrequenciesHz_[activeMarkerIndex_] =
                FrequencyMapper::frequencyForBin(frame_->metadata, last);
            updateRendererMarkers();
            publishActiveMarker();
            update();
        }
    }
}

} // namespace rtsa
