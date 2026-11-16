#pragma once

#include "core/SpectrumFrame.h"
#include "plot/RasterSpectrumRenderer.h"
#include "plot/RenderTiming.h"

#include <QElapsedTimer>
#include <QPoint>
#include <QString>
#include <QWidget>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

class QRubberBand;
class QThread;

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

struct SpectrumRenderDiagnostics {
    TimingSummary renderTotal;
    TimingSummary geometry;
    TimingSummary staticBuild;
    TimingSummary staticRestore;
    TimingSummary trace;
    TimingSummary guiPaint;
    std::uint64_t pendingOverwriteCount = 0U;
    std::uint64_t syncFallbackCount = 0U;
    std::uint64_t directPaintCount = 0U;
    std::uint64_t offscreenPaintCount = 0U;
    double lastPaintMilliseconds = 0.0;
    bool lastPaintOffscreen = false;
};

class SpectrumPlotWidget final : public QWidget {
    Q_OBJECT

public:
    enum class RenderMode {
        Direct,
        Offscreen
    };

    explicit SpectrumPlotWidget(QWidget* parent = nullptr);
    ~SpectrumPlotWidget() override;

    void setFrame(ConstSpectrumFramePtr frame);
    ConstSpectrumFramePtr frame() const noexcept;
    void setRenderMode(RenderMode mode);
    RenderMode renderMode() const noexcept;
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
    bool isLastPaintOffscreen() const noexcept;
    QRect plotRect() const;
    SpectrumRenderDiagnostics renderDiagnostics() const;
    void resetRenderDiagnostics();

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

    void invalidateFrontImageAndRerender();
    bool enqueueLatestRenderContext();
    void startRenderWorker();
    void stopRenderWorker();
    void recordOffscreenRender(double totalMilliseconds,
                               const RasterRenderTiming& timings);
    void recordDirectRender(double totalMilliseconds,
                            const RasterRenderTiming& timings);
    void recordGuiPaint(double milliseconds);
    void workerLoop();

    // 方案二：多核异步离屏渲染工作线程与双缓冲
    RasterSpectrumRenderer offscreenRenderer_;
    QThread* renderWorker_ = nullptr;
    std::mutex taskMutex_;
    std::condition_variable taskCv_;
    std::atomic<bool> workerStopping_{false};
    RenderContext pendingContext_;
    bool hasPendingContext_{false};

    std::mutex frontImageMutex_;
    QImage frontImage_;
    std::uint64_t frontStaticLayerGeneration_ = 0U;
    std::uint64_t frontPublicationSequence_ = 0U;
    std::uint64_t frontTimestampNs_ = 0U;
    bool hasFrontImage_{false};
    bool lastPaintOffscreen_ = false;

    RenderMode renderMode_ = RenderMode::Offscreen;
    std::atomic<std::uint64_t> pendingOverwriteCount_ { 0U };
    std::atomic<std::uint64_t> syncFallbackCount_ { 0U };
    std::atomic<std::uint64_t> directPaintCount_ { 0U };
    std::atomic<std::uint64_t> offscreenPaintCount_ { 0U };

    mutable std::mutex metricsMutex_;
    std::mutex paintRenderMutex_;
    RollingTiming renderTotalTiming_;
    RollingTiming geometryTiming_;
    RollingTiming staticBuildTiming_;
    RollingTiming staticRestoreTiming_;
    RollingTiming traceTiming_;
    RollingTiming guiPaintTiming_;
};

} // namespace rtsa
