#pragma once

#include "core/SpectrumFrame.h"
#include "plot/Colormap.h"
#include "plot/RenderTiming.h"

#include <QElapsedTimer>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QWidget>

#include <cstddef>
#include <vector>

class QRubberBand;

namespace rtsa {

struct WaterfallRenderDiagnostics {
    TimingSummary ingest;
    TimingSummary paint;
    double lastIngestMilliseconds = 0.0;
    double lastPaintMilliseconds = 0.0;
};

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
    double lastIngestMilliseconds() const noexcept { return lastIngestMilliseconds_; }
    WaterfallRenderDiagnostics renderDiagnostics() const;
    void resetRenderDiagnostics();

signals:
    void spanScaleRequested(double scaleFactor, double anchorFrequencyHz);
    void frequencyPanRequested(double centerShiftHz);
    void frequencyRangeSelected(double startFrequencyHz, double stopFrequencyHz);
    void frequencyRangeResetRequested();

protected:
    bool event(QEvent* event) override;
    void changeEvent(QEvent* event) override;
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
    void invalidateStaticLayer();
    void rebuildStaticLayer();
    void rebuildColorBarImage();
    void drawAxisText(QPainter& painter);
    void drawGridAndBorder(QPainter& painter);
    void drawWaterfallImage(QPainter& painter, const QRect& dirtyRect);
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
    double staticCenterFrequencyHz_ = 1.0e9;
    double staticSpanHz_ = 200.0e6;
    ColormapPreset colormapPreset_ = ColormapPreset::ClassicRainbow;
    QVector<QRgb> colorTable_;
    std::vector<float> columnMaxAmplitudes_;
    QImage colorBarImage_;
    QImage staticLayer_;
    bool staticLayerDirty_ = true;

    QElapsedTimer paintTimer_;
    QElapsedTimer ingestTimer_;
    double lastPaintMilliseconds_ = 0.0;
    double lastIngestMilliseconds_ = 0.0;
    bool lightTheme_ = false;
    RollingTiming ingestTiming_;
    RollingTiming paintTiming_;

    // Mouse drag interaction
    bool dragging_ = false;
    QPoint dragStartPos_;
    bool shiftSelecting_ = false;
    QRubberBand* rubberBand_ = nullptr;
    QPoint rubberBandOrigin_;
};

} // namespace rtsa
