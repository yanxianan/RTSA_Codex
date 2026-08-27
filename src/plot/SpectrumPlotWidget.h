#pragma once

#include "core/SpectrumFrame.h"
#include "plot/RasterSpectrumRenderer.h"

#include <QElapsedTimer>
#include <QPoint>
#include <QString>
#include <QWidget>

#include <cstddef>
#include <array>
#include <optional>

class QRubberBand;

namespace rtsa {

struct MarkerMeasurement {
    bool valid = false;
    std::size_t bin = 0;
    double frequencyHz = 0.0;
    float amplitude = 0.0F;
};

struct DeltaMarkerMeasurement {
    bool valid = false;
    double frequencyDeltaHz = 0.0;
    float amplitudeDelta = 0.0F;
};

class SpectrumPlotWidget final : public QWidget {
    Q_OBJECT

public:
    explicit SpectrumPlotWidget(QWidget* parent = nullptr);

    void setFrame(ConstSpectrumFramePtr frame);
    ConstSpectrumFramePtr frame() const noexcept;
    void setAmplitudeScale(float referenceLevel, float bottomLevel);
    void setAppearance(const QColor& traceColor, int traceWidth,
                       bool gridVisible, int themeIndex,
                       const QColor& customBgColor = QColor());
    void setAppearance(const QColor& traceColor, int traceWidth,
                       bool gridVisible, bool lightTheme);
    void setActiveMarker(std::size_t markerIndex);
    std::size_t activeMarker() const noexcept;
    void clearMarker();
    void clearAllMarkers();
    void peakSearch();
    void nextPeak();
    void previousPeak();
    void setPeakThreshold(float threshold);
    float peakThreshold() const noexcept;
    void setDeltaMarkerEnabled(bool enabled);
    bool deltaMarkerEnabled() const noexcept;

    void setMarkerFrequency(std::size_t markerIndex, double frequencyHz);
    void setMarkerEnabled(std::size_t markerIndex, bool enabled);
    bool isMarkerEnabled(std::size_t markerIndex) const noexcept;
    std::optional<double> markerFrequency(std::size_t markerIndex) const noexcept;

    std::size_t envelopeColumnCount() const noexcept;
    std::optional<std::size_t> markerBin() const noexcept;
    std::optional<std::size_t> markerBin(std::size_t markerIndex) const noexcept;
    MarkerMeasurement markerMeasurement(std::size_t markerIndex) const noexcept;
    DeltaMarkerMeasurement deltaMarkerMeasurement() const noexcept;
    DeltaMarkerMeasurement deltaMarkerMeasurement(std::size_t markerIndex, std::size_t referenceIndex = 0) const noexcept;
    double lastPaintMilliseconds() const noexcept;

signals:
    void markerChanged(double frequencyHz, float amplitude,
                       const QString& unitText, bool calibrated);
    void markerCleared();
    void spanScaleRequested(double scaleFactor, double anchorFrequencyHz);
    void frequencyPanRequested(double centerShiftHz);
    void frequencyRangeSelected(double startFrequencyHz, double stopFrequencyHz);
    void frequencyRangeResetRequested();
    void framePainted(std::uint64_t publicationSequence, std::uint64_t timestampNs);

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QRect calculatePlotRect() const;
    std::size_t binAtPosition(const QPoint& position) const;
    QPoint mousePosition(const QMouseEvent* event) const;
    QPoint wheelPosition(const QWheelEvent* event) const;
    void selectMarkerAt(const QPoint& position);
    void synchronizeMarkers();
    void publishActiveMarker();
    void updateRendererMarkers();
    void searchAdjacentPeak(int direction);
    std::vector<std::size_t> findPeaks() const;
    bool isPeak(std::size_t bin) const noexcept;

    ConstSpectrumFramePtr frame_;
    RasterSpectrumRenderer renderer_;
    QElapsedTimer paintTimer_;
    double lastPaintMilliseconds_ = 0.0;
    std::array<std::optional<double>, kSpectrumMarkerCount> markerFrequenciesHz_;
    std::size_t activeMarkerIndex_ = 0;
    float peakThreshold_ = -100.0F;
    bool deltaMarkerEnabled_ = false;
    QPoint pressPosition_;
    QPoint lastPanPosition_;
    bool leftButtonPressed_ = false;
    bool panning_ = false;
    bool boxZooming_ = false;
    QRubberBand* zoomBand_ = nullptr;
};

} // namespace rtsa
