#include "plot/WaterfallPlotWidget.h"
#include "core/FrequencyMapper.h"

#include <QApplication>
#include <QFontMetrics>
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
#include <cstring>

namespace rtsa {
namespace {

constexpr int kLeftMargin = 76;
constexpr int kRightMargin = 72;
constexpr int kTopMargin = 14;
constexpr int kBottomMargin = 42;

} // namespace

WaterfallPlotWidget::WaterfallPlotWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(480, 200);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_AcceptTouchEvents, true);
    grabGesture(Qt::PinchGesture);
    grabGesture(Qt::PanGesture);

    colorTable_ = Colormap::createColorTable(colormapPreset_);
    updatePlotGeometry();
}

void WaterfallPlotWidget::setAmplitudeScale(const float referenceLevel, const float bottomLevel)
{
    const float safeBottom = std::min(bottomLevel, referenceLevel - 1.0F);
    if (referenceLevel_ == referenceLevel && bottomLevel_ == safeBottom) {
        return;
    }
    referenceLevel_ = referenceLevel;
    bottomLevel_ = safeBottom;
    update();
}

void WaterfallPlotWidget::setColormap(const ColormapPreset preset)
{
    if (colormapPreset_ == preset && !colorTable_.isEmpty()) {
        return;
    }
    colormapPreset_ = preset;
    colorTable_ = Colormap::createColorTable(preset);
    if (!bufferImage_.isNull()) {
        bufferImage_.setColorTable(colorTable_);
    }
    update();
}

void WaterfallPlotWidget::setHistoryDepth(int depth)
{
    depth = std::clamp(depth, 64, 2048);
    if (historyDepth_ == depth) {
        return;
    }
    historyDepth_ = depth;
    resizeImageBuffer(bufferWidth_, historyDepth_);
    update();
}

void WaterfallPlotWidget::clear()
{
    headRow_ = 0;
    rowCount_ = 0;
    totalFramesAdded_ = 0;
    if (!bufferImage_.isNull()) {
        bufferImage_.fill(0);
    }
    update();
}

QRect WaterfallPlotWidget::calculatePlotRect() const
{
    return rect().adjusted(kLeftMargin, kTopMargin, -kRightMargin, -kBottomMargin);
}

void WaterfallPlotWidget::updatePlotGeometry()
{
    plotRect_ = calculatePlotRect();
    const int targetWidth = std::max(64, plotRect_.width());
    if (bufferImage_.isNull() || bufferWidth_ != targetWidth || bufferImage_.height() != historyDepth_) {
        resizeImageBuffer(targetWidth, historyDepth_);
    }
}

void WaterfallPlotWidget::resizeImageBuffer(const int width, const int height)
{
    const int newWidth = std::max(64, width);
    const int newHeight = std::max(64, height);
    if (bufferWidth_ == newWidth && historyDepth_ == newHeight && !bufferImage_.isNull()) {
        return;
    }

    QImage newImage(newWidth, newHeight, QImage::Format_Indexed8);
    newImage.setColorTable(colorTable_);
    newImage.fill(0);

    if (!bufferImage_.isNull() && rowCount_ > 0) {
        // Copy scanlines directly for Format_Indexed8 (cannot use QPainter on Indexed8 image)
        const int copyRows = std::min(rowCount_, newHeight);
        for (int i = 0; i < copyRows; ++i) {
            const int oldRow = (headRow_ + i) % bufferImage_.height();
            const int newRow = (newHeight - copyRows + i) % newHeight;
            const uchar* srcScan = bufferImage_.constScanLine(oldRow);
            uchar* dstScan = newImage.scanLine(newRow);
            const int copyCols = std::min(bufferWidth_, newWidth);
            std::memcpy(dstScan, srcScan, static_cast<std::size_t>(copyCols));
        }
        headRow_ = (newHeight - copyRows) % newHeight;
        rowCount_ = copyRows;
    } else {
        headRow_ = 0;
        rowCount_ = 0;
    }

    bufferWidth_ = newWidth;
    historyDepth_ = newHeight;
    bufferImage_ = std::move(newImage);
}

void WaterfallPlotWidget::addFrame(ConstSpectrumFramePtr frame)
{
    if (!frame || frame->bins.empty()) {
        return;
    }
    latestFrame_ = frame;

    if (bufferImage_.isNull() || plotRect_ != calculatePlotRect()) {
        updatePlotGeometry();
    }

    const std::size_t binCount = frame->bins.size();
    if (columnMaxAmplitudes_.size() != static_cast<std::size_t>(bufferWidth_)) {
        columnMaxAmplitudes_.resize(bufferWidth_);
    }

    // Peak-downsample bins into bufferWidth_ columns
    const double binsPerCol = static_cast<double>(binCount) / static_cast<double>(bufferWidth_);
    for (int col = 0; col < bufferWidth_; ++col) {
        const std::size_t startBin = static_cast<std::size_t>(std::floor(col * binsPerCol));
        const std::size_t endBin = std::min(binCount,
            static_cast<std::size_t>(std::ceil((col + 1) * binsPerCol)));
        
        float maxVal = -1000.0F;
        for (std::size_t b = startBin; b < endBin; ++b) {
            if (frame->bins[b] > maxVal) {
                maxVal = frame->bins[b];
            }
        }
        columnMaxAmplitudes_[col] = maxVal;
    }

    // Advance ring buffer head backwards so increasing index goes chronologically backwards (top to bottom)
    headRow_ = (headRow_ - 1 + historyDepth_) % historyDepth_;
    rowCount_ = std::min(rowCount_ + 1, historyDepth_);
    ++totalFramesAdded_;

    // Write to ring buffer scanline at headRow_
    std::uint8_t* scanLine = bufferImage_.scanLine(headRow_);
    for (int col = 0; col < bufferWidth_; ++col) {
        scanLine[col] = Colormap::mapToColorIndex(columnMaxAmplitudes_[col], referenceLevel_, bottomLevel_);
    }

    update(plotRect_);
}

void WaterfallPlotWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updatePlotGeometry();
}

void WaterfallPlotWidget::paintEvent(QPaintEvent*)
{
    paintTimer_.restart();

    if (plotRect_ != calculatePlotRect()) {
        updatePlotGeometry();
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    // 1. Fill background
    const QColor bgColor = lightTheme_ ? QColor(245, 247, 250) : QColor(14, 18, 24);
    painter.fillRect(rect(), bgColor);

    // 2. Draw Waterfall Image
    drawWaterfallImage(painter);

    // 3. Draw Grid, Axes and Labels
    drawGridAndAxes(painter);

    // 4. Draw Color Bar Legend
    drawColorBar(painter);

    lastPaintMilliseconds_ = static_cast<double>(paintTimer_.nsecsElapsed()) / 1.0e6;
}

void WaterfallPlotWidget::drawColorBar(QPainter& painter)
{
    if (plotRect_.width() <= 0 || plotRect_.height() <= 0 || colorTable_.isEmpty()) {
        return;
    }

    const int barX = plotRect_.right() + 10;
    const int barY = plotRect_.top();
    const int barW = 12;
    const int barH = plotRect_.height();

    // Vertical Color Bar (top is 255 = ref, bottom is 0 = bottom)
    QImage barImage(1, 256, QImage::Format_Indexed8);
    barImage.setColorTable(colorTable_);
    for (int i = 0; i < 256; ++i) {
        barImage.setPixel(0, 255 - i, i);
    }
    painter.drawImage(QRect(barX, barY, barW, barH), barImage);

    // Border
    const QColor frameColor = lightTheme_ ? QColor(190, 198, 208) : QColor(48, 58, 72);
    const QColor textColor = lightTheme_ ? QColor(70, 80, 95) : QColor(160, 175, 195);
    painter.setPen(QPen(frameColor, 1));
    painter.drawRect(barX, barY, barW, barH);

    // Text Labels
    painter.setPen(textColor);
    QFont font = painter.font();
    font.setPointSize(7);
    painter.setFont(font);
    const QFontMetrics metrics(font);

    const QString topText = QStringLiteral("%1").arg(std::round(referenceLevel_));
    const QString midText = QStringLiteral("%1").arg(std::round((referenceLevel_ + bottomLevel_) * 0.5F));
    const QString botText = QStringLiteral("%1").arg(std::round(bottomLevel_));

    painter.drawText(barX + barW + 4, barY + metrics.ascent(), topText);
    painter.drawText(barX + barW + 4, barY + barH / 2 + metrics.ascent() / 2, midText);
    painter.drawText(barX + barW + 4, barY + barH, botText);

    const QString unitText = QStringLiteral("dBFS");
    painter.drawText(barX - 2, barY - 3, unitText);
}

void WaterfallPlotWidget::drawWaterfallImage(QPainter& painter)
{
    if (plotRect_.width() <= 0 || plotRect_.height() <= 0 || rowCount_ == 0 || bufferImage_.isNull()) {
        painter.fillRect(plotRect_, QColor(8, 10, 14));
        return;
    }

    painter.save();
    painter.setClipRect(plotRect_);

    if (rowCount_ < historyDepth_) {
        // Partially filled ring buffer: valid rowCount_ rows starting at headRow_ to historyDepth_ - 1
        const int sourceY = headRow_;
        const int sourceH = rowCount_;
        const int targetH = static_cast<int>(std::round(plotRect_.height() * (static_cast<double>(rowCount_) / historyDepth_)));

        const QRect sourceRect(0, sourceY, bufferWidth_, sourceH);
        const QRect targetRect(plotRect_.left(), plotRect_.top(), plotRect_.width(), targetH);
        painter.drawImage(targetRect, bufferImage_, sourceRect);
    } else {
        // Full ring buffer: split into two slices
        // Slice 1 (Top / Recent): [headRow_, historyDepth_)
        const int h1Rows = historyDepth_ - headRow_;
        const int h1Pixels = static_cast<int>(std::round(plotRect_.height() * (static_cast<double>(h1Rows) / historyDepth_)));
        const QRect source1(0, headRow_, bufferWidth_, h1Rows);
        const QRect target1(plotRect_.left(), plotRect_.top(), plotRect_.width(), h1Pixels);

        // Slice 2 (Bottom / Older): [0, headRow_)
        const int h2Rows = headRow_;
        const int h2Pixels = plotRect_.height() - h1Pixels;
        const QRect source2(0, 0, bufferWidth_, h2Rows);
        const QRect target2(plotRect_.left(), plotRect_.top() + h1Pixels, plotRect_.width(), h2Pixels);

        painter.drawImage(target1, bufferImage_, source1);
        if (h2Rows > 0 && h2Pixels > 0) {
            painter.drawImage(target2, bufferImage_, source2);
        }
    }

    painter.restore();
}

void WaterfallPlotWidget::drawGridAndAxes(QPainter& painter)
{
    const QColor frameColor = lightTheme_ ? QColor(190, 198, 208) : QColor(48, 58, 72);
    const QColor gridColor = lightTheme_ ? QColor(220, 226, 234, 160) : QColor(36, 44, 56, 180);
    const QColor textColor = lightTheme_ ? QColor(70, 80, 95) : QColor(160, 175, 195);

    // Plot Border
    painter.setPen(QPen(frameColor, 1));
    painter.drawRect(plotRect_);

    // Frequency Grid (Vertical Lines - 10 divisions)
    constexpr int kDivisions = 10;
    const double colStep = static_cast<double>(plotRect_.width()) / kDivisions;
    painter.setPen(QPen(gridColor, 1, Qt::DashLine));
    for (int i = 1; i < kDivisions; ++i) {
        const int x = plotRect_.left() + static_cast<int>(std::round(i * colStep));
        painter.drawLine(x, plotRect_.top(), x, plotRect_.bottom());
    }

    // Time Grid (Horizontal Lines - 4 divisions)
    constexpr int kTimeDivisions = 4;
    const double rowStep = static_cast<double>(plotRect_.height()) / kTimeDivisions;
    for (int j = 1; j < kTimeDivisions; ++j) {
        const int y = plotRect_.top() + static_cast<int>(std::round(j * rowStep));
        painter.drawLine(plotRect_.left(), y, plotRect_.right(), y);
    }

    // Frequency Axis Labels (Bottom)
    painter.setPen(textColor);
    QFont font = painter.font();
    font.setPointSize(8);
    painter.setFont(font);
    const QFontMetrics metrics(font);

    double centerHz = 1.0e9;
    double spanHz = 200.0e6;
    if (latestFrame_) {
        centerHz = latestFrame_->metadata.centerFrequencyHz;
        spanHz = latestFrame_->metadata.spanHz;
    }
    const double startHz = centerHz - spanHz * 0.5;

    for (int i = 0; i <= kDivisions; i += 2) {
        const double freq = startHz + spanHz * (static_cast<double>(i) / kDivisions);
        const QString text = formatFrequency(freq);
        const int textWidth = metrics.horizontalAdvance(text);
        const int targetX = plotRect_.left() + static_cast<int>(std::round(i * colStep)) - textWidth / 2;
        const int minX = 2;
        const int maxX = std::max(minX, width() - textWidth - 2);
        const int x = std::clamp(targetX, minX, maxX);
        painter.drawText(x, plotRect_.bottom() + 18, text);
    }

    // Time Axis Labels (Left Margin: "最新/0", "-100", "-200", etc.)
    for (int j = 0; j <= kTimeDivisions; ++j) {
        const int frameOffset = static_cast<int>(j * (static_cast<double>(historyDepth_) / kTimeDivisions));
        const QString timeText = j == 0 ? tr("最新") : QStringLiteral("-%1").arg(frameOffset);
        const int textWidth = metrics.horizontalAdvance(timeText);
        const int y = plotRect_.top() + static_cast<int>(std::round(j * rowStep)) + metrics.ascent() / 2;
        painter.drawText(plotRect_.left() - textWidth - 6, y, timeText);
    }

    // Bottom Axis Unit Title
    const QString axisTitle = tr("频率 (Hz)");
    const int titleWidth = metrics.horizontalAdvance(axisTitle);
    painter.drawText(plotRect_.center().x() - titleWidth / 2, plotRect_.bottom() + 34, axisTitle);
}

QString WaterfallPlotWidget::formatFrequency(const double frequencyHz) const
{
    const double absFreq = std::abs(frequencyHz);
    if (absFreq >= 1.0e9) {
        return QStringLiteral("%1 GHz").arg(frequencyHz / 1.0e9, 0, 'f', 3);
    }
    if (absFreq >= 1.0e6) {
        return QStringLiteral("%1 MHz").arg(frequencyHz / 1.0e6, 0, 'f', 2);
    }
    if (absFreq >= 1.0e3) {
        return QStringLiteral("%1 kHz").arg(frequencyHz / 1.0e3, 0, 'f', 1);
    }
    return QStringLiteral("%1 Hz").arg(frequencyHz, 0, 'f', 0);
}

QPoint WaterfallPlotWidget::mousePosition(const QMouseEvent* event) const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

QPoint WaterfallPlotWidget::wheelPosition(const QWheelEvent* event) const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position().toPoint();
#else
    return event->pos();
#endif
}

bool WaterfallPlotWidget::event(QEvent* event)
{
    if (event->type() == QEvent::Gesture && latestFrame_) {
        auto* gestureEvent = static_cast<QGestureEvent*>(event);
        if (auto* pinch = static_cast<QPinchGesture*>(gestureEvent->gesture(Qt::PinchGesture))) {
            if (pinch->changeFlags().testFlag(QPinchGesture::ScaleFactorChanged) && pinch->scaleFactor() > 0.0) {
                emit spanScaleRequested(1.0 / pinch->scaleFactor(), latestFrame_->metadata.centerFrequencyHz);
            }
        }
        if (auto* pan = static_cast<QPanGesture*>(gestureEvent->gesture(Qt::PanGesture))) {
            if (plotRect_.width() > 1 && pan->delta().x() != 0.0) {
                emit frequencyPanRequested(-pan->delta().x() * latestFrame_->metadata.spanHz / static_cast<double>(plotRect_.width() - 1));
            }
        }
        gestureEvent->accept();
        return true;
    }
    return QWidget::event(event);
}

void WaterfallPlotWidget::mousePressEvent(QMouseEvent* event)
{
    if (!latestFrame_ || !plotRect_.contains(mousePosition(event))) {
        QWidget::mousePressEvent(event);
        return;
    }

    if (event->button() == Qt::LeftButton) {
        if (event->modifiers().testFlag(Qt::ShiftModifier)) {
            shiftSelecting_ = true;
            rubberBandOrigin_ = mousePosition(event);
            if (!rubberBand_) {
                rubberBand_ = new QRubberBand(QRubberBand::Rectangle, this);
            }
            rubberBand_->setGeometry(QRect(rubberBandOrigin_, QSize()));
            rubberBand_->show();
        } else {
            dragging_ = true;
            dragStartPos_ = mousePosition(event);
            setCursor(Qt::ClosedHandCursor);
        }
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void WaterfallPlotWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (shiftSelecting_ && rubberBand_) {
        const QPoint pos = mousePosition(event);
        const int left = std::max(plotRect_.left(), std::min(rubberBandOrigin_.x(), pos.x()));
        const int right = std::min(plotRect_.right(), std::max(rubberBandOrigin_.x(), pos.x()));
        rubberBand_->setGeometry(QRect(QPoint(left, plotRect_.top()), QPoint(right, plotRect_.bottom())));
        event->accept();
        return;
    }

    if (dragging_ && latestFrame_ && plotRect_.width() > 1) {
        const QPoint pos = mousePosition(event);
        const int deltaX = pos.x() - dragStartPos_.x();
        dragStartPos_ = pos;
        const double shiftHz = -static_cast<double>(deltaX) * latestFrame_->metadata.spanHz / static_cast<double>(plotRect_.width() - 1);
        emit frequencyPanRequested(shiftHz);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void WaterfallPlotWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (shiftSelecting_ && rubberBand_ && latestFrame_) {
        shiftSelecting_ = false;
        rubberBand_->hide();
        const QRect selected = rubberBand_->geometry();
        if (selected.width() >= 10 && plotRect_.width() > 1) {
            const double startFraction = static_cast<double>(selected.left() - plotRect_.left()) / static_cast<double>(plotRect_.width() - 1);
            const double stopFraction = static_cast<double>(selected.right() - plotRect_.left()) / static_cast<double>(plotRect_.width() - 1);
            const double startHz = latestFrame_->startFrequencyHz() + startFraction * latestFrame_->metadata.spanHz;
            const double stopHz = latestFrame_->startFrequencyHz() + stopFraction * latestFrame_->metadata.spanHz;
            emit frequencyRangeSelected(startHz, stopHz);
        }
        event->accept();
        return;
    }

    if (dragging_) {
        dragging_ = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void WaterfallPlotWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        emit frequencyRangeResetRequested();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void WaterfallPlotWidget::wheelEvent(QWheelEvent* event)
{
    if (!latestFrame_ || !plotRect_.contains(wheelPosition(event))) {
        QWidget::wheelEvent(event);
        return;
    }

    const double numDegrees = static_cast<double>(event->angleDelta().y()) / 8.0;
    const double numSteps = numDegrees / 15.0;
    const double factor = std::pow(0.85, numSteps);

    const double fraction = static_cast<double>(wheelPosition(event).x() - plotRect_.left()) / static_cast<double>(plotRect_.width() - 1);
    const double anchorHz = latestFrame_->startFrequencyHz() + fraction * latestFrame_->metadata.spanHz;

    emit spanScaleRequested(factor, anchorHz);
    event->accept();
}

} // namespace rtsa
