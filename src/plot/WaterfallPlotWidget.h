#pragma once

#include "core/SpectrumFrame.h"
#include "plot/Colormap.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QWidget>

#include <cstddef>
#include <vector>

class QRubberBand;

namespace rtsa {

class WaterfallPlotWidget final : public QWidget {
    Q_OBJECT

public:
    explicit WaterfallPlotWidget(QWidget* parent = nullptr);
    ~WaterfallPlotWidget() override = default;

    void addFrame(ConstSpectrumFramePtr frame);
    void setAmplitudeScale(float referenceLevel, float bottomLevel);
    void setColormap(ColormapPreset preset);
    ColormapPreset colormap() const noexcept { return colormapPreset_; }
    void setHistoryDepth(int depth);
    int historyDepth() const noexcept { return historyDepth_; }
    void clear();

    QRect plotRect() const noexcept { return plotRect_; }
    double lastPaintMilliseconds() const noexcept { return lastPaintMilliseconds_; }

signals:
    void spanScaleRequested(double scaleFactor, double anchorFrequencyHz);
    void frequencyPanRequested(double centerShiftHz);
    void frequencyRangeSelected(double startFrequencyHz, double stopFrequencyHz);
    void frequencyRangeResetRequested();

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QRect calculatePlotRect() const;
    void updatePlotGeometry();
    void resizeImageBuffer(int width, int height);
    void drawGridAndAxes(QPainter& painter);
    void drawWaterfallImage(QPainter& painter);
    void drawColorBar(QPainter& painter);
    QString formatFrequency(double frequencyHz) const;
    QPoint mousePosition(const QMouseEvent* event) const;
    QPoint wheelPosition(const QWheelEvent* event) const;

    QRect plotRect_;
    ConstSpectrumFramePtr latestFrame_;
    QImage bufferImage_;
    int historyDepth_ = 512;
    int bufferWidth_ = 800;
    int headRow_ = 0;
    int rowCount_ = 0;
    std::uint64_t totalFramesAdded_ = 0;
    float referenceLevel_ = 0.0F;
    float bottomLevel_ = -140.0F;
    ColormapPreset colormapPreset_ = ColormapPreset::ClassicRainbow;
    QVector<QRgb> colorTable_;
    std::vector<float> columnMaxAmplitudes_;

    QElapsedTimer paintTimer_;
    double lastPaintMilliseconds_ = 0.0;
    bool lightTheme_ = false;

    // Mouse drag interaction
    bool dragging_ = false;
    QPoint dragStartPos_;
    bool shiftSelecting_ = false;
    QRubberBand* rubberBand_ = nullptr;
    QPoint rubberBandOrigin_;
};

} // namespace rtsa
