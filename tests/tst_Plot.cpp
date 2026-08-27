#include "plot/SpectrumPlotWidget.h"
#include "core/EnvelopeReducer.h"
#include "core/FrequencyMapper.h"

#include <QElapsedTimer>
#include <QGestureEvent>
#include <QImage>
#include <QPanGesture>
#include <QPainter>
#include <QPinchGesture>
#include <QtTest>

#include <algorithm>
#include <memory>

namespace rtsa {

class PlotTests final : public QObject {
    Q_OBJECT

private slots:
    void rasterWidgetRendersWithoutOpenGL();
    void lowBinTraceSpansFullPlotWidth();
    void peakSearchUsesFullResolutionFrame();
    void markerTracksFrequencyAcrossFrames();
    void markerClearsOutsideNewSpan();
    void fourMarkersDeltaAndAdjacentPeakSearch();
    void wideGaussianPeakWithNoisySkirtFindsApexNotSkirt();
    void peakThresholdRejectsLowSignals();
    void doubleClickRequestsFrequencyReset();
    void dragRequestsFrequencyPan();
    void shiftDragRequestsBoxZoom();
    void widgetAcceptsTouchInput();
    void touchGesturesRequestZoomAndPan();
    void paintedSignalIdentifiesRenderedFrame();
    void appearanceSettingsChangeRasterOutput();
    void sixtyFiveKPointCpuPerformance();
};

void PlotTests::rasterWidgetRendersWithoutOpenGL()
{
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.sequence = 1;
    frame->metadata.binCount = 65536;
    frame->metadata.centerFrequencyHz = 1.0e9;
    frame->metadata.spanHz = 200.0e6;
    frame->bins.assign(65536, -115.0F);
    frame->bins[32123] = -5.0F;

    SpectrumPlotWidget widget;
    widget.resize(1000, 600);
    widget.setFrame(frame);

    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget.render(&painter);
    painter.end();

    QCOMPARE(widget.envelopeColumnCount(), std::size_t(904));
    QVERIFY(!image.isNull());
    QVERIFY(image.pixelColor(500, 300) != QColor(Qt::transparent));
    QVERIFY(widget.lastPaintMilliseconds() >= 0.0);
}

void PlotTests::lowBinTraceSpansFullPlotWidth()
{
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.sequence = 1;
    frame->metadata.binCount = 1024;
    frame->metadata.centerFrequencyHz = 1.0e9;
    frame->metadata.spanHz = 200.0e6;
    frame->bins.assign(1024, -100.0F);

    SpectrumPlotWidget widget;
    widget.resize(1600, 600);
    widget.setFrame(frame);

    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget.render(&painter);
    painter.end();

    const QColor traceColor(0, 235, 180);
    bool traceAtRightEdge = false;
    for (int x = widget.width() - 24; x <= widget.width() - 18; ++x) {
        for (int y = 30; y < widget.height() - 54; ++y) {
            if (image.pixelColor(x, y) == traceColor) {
                traceAtRightEdge = true;
                break;
            }
        }
    }
    QVERIFY2(traceAtRightEdge, "A low-bin trace must reach the right edge of the plot");
}

void PlotTests::peakSearchUsesFullResolutionFrame()
{
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.sequence = 1;
    frame->metadata.binCount = 4096;
    frame->bins.assign(4096, -100.0F);
    frame->bins[2047] = -2.0F;

    SpectrumPlotWidget widget;
    widget.resize(800, 500);
    widget.setFrame(frame);
    widget.peakSearch();

    QVERIFY(widget.markerBin().has_value());
    QCOMPARE(*widget.markerBin(), std::size_t(2047));
}

void PlotTests::markerTracksFrequencyAcrossFrames()
{
    auto first = std::make_shared<SpectrumFrame>();
    first->metadata.sequence = 1;
    first->metadata.binCount = 100;
    first->metadata.centerFrequencyHz = 1.0e9;
    first->metadata.spanHz = 200.0e6;
    first->bins.assign(100, -100.0F);
    first->bins[25] = -5.0F;

    SpectrumPlotWidget widget;
    QSignalSpy markerSpy(&widget, &SpectrumPlotWidget::markerChanged);
    widget.setFrame(first);
    widget.peakSearch();
    QCOMPARE(*widget.markerBin(), std::size_t(25));

    auto zoomed = std::make_shared<SpectrumFrame>();
    zoomed->metadata.sequence = 2;
    zoomed->metadata.binCount = 100;
    zoomed->metadata.centerFrequencyHz = 950.0e6;
    zoomed->metadata.spanHz = 100.0e6;
    zoomed->bins.assign(100, -90.0F);
    zoomed->bins[50] = -12.5F;
    widget.setFrame(zoomed);

    QCOMPARE(*widget.markerBin(), std::size_t(50));
    QCOMPARE(markerSpy.count(), 2);
    QCOMPARE(markerSpy.last().at(1).toFloat(), -12.5F);
    QCOMPARE(markerSpy.last().at(2).toString(), QStringLiteral("dBFS"));
    QVERIFY(!markerSpy.last().at(3).toBool());
}

void PlotTests::markerClearsOutsideNewSpan()
{
    auto first = std::make_shared<SpectrumFrame>();
    first->metadata.sequence = 1;
    first->metadata.binCount = 100;
    first->metadata.centerFrequencyHz = 1.0e9;
    first->metadata.spanHz = 200.0e6;
    first->bins.assign(100, -100.0F);
    first->bins[25] = -5.0F;

    SpectrumPlotWidget widget;
    QSignalSpy clearedSpy(&widget, &SpectrumPlotWidget::markerCleared);
    widget.setFrame(first);
    widget.peakSearch();

    auto shifted = std::make_shared<SpectrumFrame>(*first);
    shifted->metadata.sequence = 2;
    shifted->metadata.centerFrequencyHz = 1.2e9;
    widget.setFrame(shifted);

    QVERIFY(!widget.markerBin().has_value());
    QCOMPARE(clearedSpy.count(), 1);
}

void PlotTests::fourMarkersDeltaAndAdjacentPeakSearch()
{
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.sequence = 1;
    frame->metadata.binCount = 1024;
    frame->metadata.centerFrequencyHz = 100.0e6;
    frame->metadata.spanHz = 20.0e6;
    frame->bins.assign(1024, -140.0F);
    frame->bins[100] = -10.0F;
    frame->bins[300] = -20.0F;
    frame->bins[700] = -30.0F;

    SpectrumPlotWidget widget;
    widget.setFrame(frame);
    widget.setPeakThreshold(-80.0F);
    widget.peakSearch();
    QCOMPARE(widget.markerBin(0), std::optional<std::size_t>(100U));

    widget.setActiveMarker(1U);
    widget.peakSearch();
    widget.nextPeak();
    QCOMPARE(widget.markerBin(1), std::optional<std::size_t>(300U));
    widget.nextPeak();
    QCOMPARE(widget.markerBin(1), std::optional<std::size_t>(700U));
    widget.previousPeak();
    QCOMPARE(widget.markerBin(1), std::optional<std::size_t>(300U));
    QCOMPARE(widget.markerBin(0), std::optional<std::size_t>(100U));

    widget.setDeltaMarkerEnabled(true);
    const DeltaMarkerMeasurement delta = widget.deltaMarkerMeasurement();
    QVERIFY(delta.valid);
    QCOMPARE(delta.frequencyDeltaHz,
             FrequencyMapper::frequencyForBin(frame->metadata, 300U)
                 - FrequencyMapper::frequencyForBin(frame->metadata, 100U));
    QCOMPARE(delta.amplitudeDelta, -10.0F);

    widget.setActiveMarker(3U);
    widget.peakSearch();
    QCOMPARE(widget.markerBin(3), std::optional<std::size_t>(100U));
    widget.clearMarker();
    QVERIFY(!widget.markerBin(3).has_value());
    QVERIFY(widget.markerBin(0).has_value());
    QVERIFY(widget.markerBin(1).has_value());
    widget.clearAllMarkers();
    for (std::size_t index = 0; index < kSpectrumMarkerCount; ++index) {
        QVERIFY(!widget.markerBin(index).has_value());
    }
}

void PlotTests::wideGaussianPeakWithNoisySkirtFindsApexNotSkirt()
{
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.sequence = 1;
    frame->metadata.binCount = 1024;
    frame->metadata.centerFrequencyHz = 500.0e6;
    frame->metadata.spanHz = 1000.0e6;
    frame->bins.assign(1024, -108.0F);

    // Left peak: wide Gaussian centered at bin 200, apex -54 dBFS, sigma = 20 bins
    for (int offset = -100; offset <= 100; ++offset) {
        const int b = 200 + offset;
        if (b >= 0 && b < 1024) {
            const float toneDb = -54.0F - 2.1714F * (static_cast<float>(offset * offset)) / 400.0F;
            frame->bins[b] = std::max(frame->bins[b], toneDb);
        }
    }
    // Add local noise ripples on the slope at bin 240 and 260 (+0.8 dB wiggles)
    frame->bins[240] = frame->bins[240] + 0.8F;
    frame->bins[260] = frame->bins[260] + 0.8F;

    // Middle peak: highest peak at bin 500, apex -11 dBFS
    for (int offset = -40; offset <= 40; ++offset) {
        const int b = 500 + offset;
        if (b >= 0 && b < 1024) {
            const float db = -11.0F - (offset * offset) / 25.0F;
            frame->bins[b] = std::max(frame->bins[b], db);
        }
    }

    // Right peak: narrow peak at bin 800, apex -36 dBFS
    frame->bins[799] = -50.0F;
    frame->bins[800] = -36.0F;
    frame->bins[801] = -50.0F;

    SpectrumPlotWidget widget;
    widget.setFrame(frame);
    widget.setPeakThreshold(-100.0F);

    // 1. Peak Search finds the global highest peak (bin 500)
    widget.peakSearch();
    QCOMPARE(widget.markerBin(0), std::optional<std::size_t>(500U));

    // 2. Previous Peak (Search Left) MUST find the apex of the left peak (bin 200), NOT the skirt wiggles at 245/250!
    widget.previousPeak();
    QCOMPARE(widget.markerBin(0), std::optional<std::size_t>(200U));

    // 3. Next Peak (Search Right) returns to middle peak (bin 500)
    widget.nextPeak();
    QCOMPARE(widget.markerBin(0), std::optional<std::size_t>(500U));

    // 4. Next Peak (Search Right) finds the right peak (bin 800)
    widget.nextPeak();
    QCOMPARE(widget.markerBin(0), std::optional<std::size_t>(800U));
}

void PlotTests::peakThresholdRejectsLowSignals()
{
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.sequence = 1;
    frame->metadata.binCount = 128;
    frame->bins.assign(128, -120.0F);
    frame->bins[64] = -20.0F;

    SpectrumPlotWidget widget;
    widget.setFrame(frame);
    widget.setPeakThreshold(-10.0F);
    widget.peakSearch();
    QVERIFY(!widget.markerBin().has_value());

    widget.setPeakThreshold(-30.0F);
    widget.peakSearch();
    QCOMPARE(widget.markerBin(), std::optional<std::size_t>(64U));
}

void PlotTests::doubleClickRequestsFrequencyReset()
{
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.sequence = 1;
    frame->metadata.binCount = 1024;
    frame->metadata.centerFrequencyHz = 1.0e9;
    frame->metadata.spanHz = 200.0e6;
    frame->bins.assign(1024, -100.0F);

    SpectrumPlotWidget widget;
    widget.resize(1000, 600);
    widget.setFrame(frame);
    QSignalSpy resetSpy(&widget, &SpectrumPlotWidget::frequencyRangeResetRequested);
    QTest::mouseDClick(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(500, 300));
    QCOMPARE(resetSpy.count(), 1);
}

void PlotTests::dragRequestsFrequencyPan()
{
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.sequence = 1;
    frame->metadata.binCount = 1024;
    frame->metadata.centerFrequencyHz = 1.0e9;
    frame->metadata.spanHz = 200.0e6;
    frame->bins.assign(1024, -100.0F);

    SpectrumPlotWidget widget;
    widget.resize(1000, 600);
    widget.setFrame(frame);
    QSignalSpy panSpy(&widget, &SpectrumPlotWidget::frequencyPanRequested);
    QTest::mousePress(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(500, 300));
    QTest::mouseMove(&widget, QPoint(600, 300));
    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::NoModifier, QPoint(600, 300));

    QVERIFY(!panSpy.isEmpty());
    QVERIFY(panSpy.last().at(0).toDouble() < 0.0);
    QVERIFY(!widget.markerBin().has_value());
}

void PlotTests::shiftDragRequestsBoxZoom()
{
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.sequence = 1;
    frame->metadata.binCount = 1024;
    frame->metadata.centerFrequencyHz = 1.0e9;
    frame->metadata.spanHz = 200.0e6;
    frame->bins.assign(1024, -100.0F);

    SpectrumPlotWidget widget;
    widget.resize(1000, 600);
    widget.show();
    widget.setFrame(frame);
    QSignalSpy selectionSpy(&widget, &SpectrumPlotWidget::frequencyRangeSelected);
    QTest::mousePress(&widget, Qt::LeftButton, Qt::ShiftModifier, QPoint(300, 300));
    QTest::mouseMove(&widget, QPoint(700, 300));
    QTest::mouseRelease(&widget, Qt::LeftButton, Qt::ShiftModifier, QPoint(700, 300));

    QCOMPARE(selectionSpy.count(), 1);
    const double startHz = selectionSpy.first().at(0).toDouble();
    const double stopHz = selectionSpy.first().at(1).toDouble();
    QVERIFY(startHz < stopHz);
    QVERIFY(stopHz - startHz < frame->metadata.spanHz);
    QVERIFY(!widget.markerBin().has_value());
}

void PlotTests::widgetAcceptsTouchInput()
{
    SpectrumPlotWidget widget;
    QVERIFY(widget.testAttribute(Qt::WA_AcceptTouchEvents));
}

void PlotTests::touchGesturesRequestZoomAndPan()
{
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.binCount = 1024;
    frame->metadata.centerFrequencyHz = 1.0e9;
    frame->metadata.spanHz = 200.0e6;
    frame->bins.assign(1024, -100.0F);

    SpectrumPlotWidget widget;
    widget.resize(1000, 600);
    widget.setFrame(frame);
    QSignalSpy zoomSpy(&widget, &SpectrumPlotWidget::spanScaleRequested);
    QSignalSpy panSpy(&widget, &SpectrumPlotWidget::frequencyPanRequested);

    QPinchGesture pinch;
    pinch.setChangeFlags(QPinchGesture::ScaleFactorChanged);
    pinch.setScaleFactor(2.0);
    QList<QGesture*> pinchGestures { &pinch };
    QGestureEvent pinchEvent(pinchGestures);
    QVERIFY(QCoreApplication::sendEvent(&widget, &pinchEvent));
    QCOMPARE(zoomSpy.count(), 1);
    QCOMPARE(zoomSpy.first().at(0).toDouble(), 0.5);
    QCOMPARE(zoomSpy.first().at(1).toDouble(), 1.0e9);

    QPanGesture pan;
    pan.setLastOffset(QPointF(0.0, 0.0));
    pan.setOffset(QPointF(100.0, 0.0));
    QList<QGesture*> panGestures { &pan };
    QGestureEvent panEvent(panGestures);
    QVERIFY(QCoreApplication::sendEvent(&widget, &panEvent));
    QCOMPARE(panSpy.count(), 1);
    QVERIFY(panSpy.first().at(0).toDouble() < 0.0);
}

void PlotTests::paintedSignalIdentifiesRenderedFrame()
{
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.publicationSequence = 7;
    frame->metadata.timestampNs = 123456;
    frame->metadata.binCount = 1024;
    frame->bins.assign(1024, -100.0F);

    SpectrumPlotWidget widget;
    widget.resize(1000, 600);
    widget.setFrame(frame);
    QSignalSpy paintedSpy(&widget, &SpectrumPlotWidget::framePainted);
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    widget.render(&painter);
    painter.end();

    QCOMPARE(paintedSpy.count(), 1);
    QCOMPARE(paintedSpy.first().at(0).toULongLong(), qulonglong(7));
    QCOMPARE(paintedSpy.first().at(1).toULongLong(), qulonglong(123456));
}

void PlotTests::appearanceSettingsChangeRasterOutput()
{
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.sequence = 1;
    frame->metadata.binCount = 1024;
    frame->bins.assign(1024, -100.0F);

    SpectrumPlotWidget widget;
    widget.resize(1000, 600);
    const QColor traceColor(220, 20, 180);
    widget.setAppearance(traceColor, 3, false, true);
    widget.setFrame(frame);

    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    widget.render(&painter);
    painter.end();

    QCOMPARE(image.pixelColor(1, 1), QColor(232, 236, 240));
    QCOMPARE(image.pixelColor(500, 300), QColor(250, 252, 253));
    bool foundTrace = false;
    for (int y = 30; y < widget.height() - 54 && !foundTrace; ++y) {
        for (int x = 76; x < widget.width() - 20; ++x) {
            if (image.pixelColor(x, y) == traceColor) {
                foundTrace = true;
                break;
            }
        }
    }
    QVERIFY(foundTrace);
}

void PlotTests::sixtyFiveKPointCpuPerformance()
{
    constexpr int iterations = 20;
    std::vector<float> bins(65536, -110.0F);
    for (std::size_t index = 0; index < bins.size(); index += 997) {
        bins[index] = -12.0F;
    }
    std::vector<EnvelopeColumn> columns;

    QElapsedTimer timer;
    timer.start();
    for (int iteration = 0; iteration < iterations; ++iteration) {
        EnvelopeReducer::reduce(bins.data(), bins.size(), 1920, columns);
    }
    const double reductionMs = static_cast<double>(timer.nsecsElapsed())
        / 1.0e6 / static_cast<double>(iterations);

    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.binCount = static_cast<std::uint32_t>(bins.size());
    frame->metadata.centerFrequencyHz = 1.0e9;
    frame->metadata.spanHz = 200.0e6;
    frame->bins = bins;
    SpectrumPlotWidget widget;
    widget.resize(1000, 600);
    widget.setFrame(frame);
    QImage image(widget.size(), QImage::Format_ARGB32_Premultiplied);

    timer.restart();
    for (int iteration = 0; iteration < iterations; ++iteration) {
        image.fill(Qt::transparent);
        QPainter painter(&image);
        widget.render(&painter);
    }
    const double renderMs = static_cast<double>(timer.nsecsElapsed())
        / 1.0e6 / static_cast<double>(iterations);

    qInfo().nospace() << "65,536 points: envelope=" << reductionMs
                      << " ms, full raster widget=" << renderMs << " ms";
    QVERIFY2(reductionMs < 50.0, "Envelope reduction is unexpectedly slow");
    QVERIFY2(renderMs < 100.0, "CPU raster rendering is unexpectedly slow");
}

} // namespace rtsa

QTEST_MAIN(rtsa::PlotTests)

#include "tst_Plot.moc"
