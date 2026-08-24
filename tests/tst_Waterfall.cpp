#include "plot/Colormap.h"
#include "plot/WaterfallPlotWidget.h"

#include <QElapsedTimer>
#include <QPainter>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

namespace rtsa {

class WaterfallTests final : public QObject {
    Q_OBJECT

private slots:
    void colormapTableGeneratesValidEntries();
    void amplitudeColorIndexQuantization();
    void waterfallWidgetReceivesFramesAndMaintainsHistory();
    void waterfallClearResetsBuffer();
    void waterfallSignalsEmitOnInteraction();
    void waterfallOffscreenPaintPerformance();
    void waterfallAlwaysPlacesNewestFrameAtTop();
    void historyIsPreservedAcrossParameterChangesAndResize();
};

void WaterfallTests::colormapTableGeneratesValidEntries()
{
    for (const ColormapPreset preset : {
             ColormapPreset::ClassicRainbow,
             ColormapPreset::RohdeSchwarz,
             ColormapPreset::Ironbow,
             ColormapPreset::DeepOcean,
             ColormapPreset::Grayscale }) {
        const QVector<QRgb> table = Colormap::createColorTable(preset);
        QCOMPARE(table.size(), 256);
        for (int i = 0; i < 256; ++i) {
            QVERIFY(qAlpha(table[i]) == 255);
        }
    }

    // Grayscale monotonicity check
    const QVector<QRgb> grayTable = Colormap::createColorTable(ColormapPreset::Grayscale);
    QCOMPARE(qRed(grayTable[0]), 0);
    QCOMPARE(qRed(grayTable[255]), 255);
    QCOMPARE(qRed(grayTable[128]), 128);
    QCOMPARE(qGreen(grayTable[128]), 128);
    QCOMPARE(qBlue(grayTable[128]), 128);
}

void WaterfallTests::amplitudeColorIndexQuantization()
{
    constexpr float ref = 0.0F;
    constexpr float bottom = -100.0F;

    QCOMPARE(Colormap::mapToColorIndex(0.0F, ref, bottom), static_cast<std::uint8_t>(255));
    QCOMPARE(Colormap::mapToColorIndex(10.0F, ref, bottom), static_cast<std::uint8_t>(255));
    QCOMPARE(Colormap::mapToColorIndex(-100.0F, ref, bottom), static_cast<std::uint8_t>(0));
    QCOMPARE(Colormap::mapToColorIndex(-120.0F, ref, bottom), static_cast<std::uint8_t>(0));
    QCOMPARE(Colormap::mapToColorIndex(-50.0F, ref, bottom), static_cast<std::uint8_t>(127));
}

void WaterfallTests::waterfallWidgetReceivesFramesAndMaintainsHistory()
{
    WaterfallPlotWidget widget;
    widget.resize(800, 400);
    widget.setHistoryDepth(128);
    widget.setAmplitudeScale(0.0F, -100.0F);
    QCOMPARE(widget.historyDepth(), 128);

    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.binCount = 1024;
    frame->metadata.centerFrequencyHz = 1.0e9;
    frame->metadata.spanHz = 100.0e6;
    frame->bins.assign(1024, -100.0F);
    frame->bins[512] = -10.0F; // Peak in the middle

    for (int i = 0; i < 200; ++i) {
        frame->metadata.sequence = i + 1;
        widget.addFrame(frame);
    }

    QVERIFY(widget.plotRect().isValid());
    QVERIFY(widget.plotRect().width() > 0);
}

void WaterfallTests::waterfallClearResetsBuffer()
{
    WaterfallPlotWidget widget;
    widget.resize(600, 300);
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.binCount = 512;
    frame->bins.assign(512, -20.0F);

    widget.addFrame(frame);
    widget.clear();
    QCOMPARE(widget.lastPaintMilliseconds(), 0.0);
}

void WaterfallTests::waterfallSignalsEmitOnInteraction()
{
    WaterfallPlotWidget widget;
    widget.resize(800, 400);

    QSignalSpy panSpy(&widget, &WaterfallPlotWidget::frequencyPanRequested);
    QSignalSpy resetSpy(&widget, &WaterfallPlotWidget::frequencyRangeResetRequested);

    QVERIFY(panSpy.isValid());
    QVERIFY(resetSpy.isValid());
}

void WaterfallTests::waterfallOffscreenPaintPerformance()
{
    WaterfallPlotWidget widget;
    widget.resize(1920, 600);
    widget.setHistoryDepth(512);

    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.binCount = 16384;
    frame->metadata.centerFrequencyHz = 1.0e9;
    frame->metadata.spanHz = 200.0e6;
    frame->bins.resize(16384);
    for (std::size_t i = 0; i < 16384; ++i) {
        frame->bins[i] = -100.0F + static_cast<float>(i % 50);
    }

    for (int i = 0; i < 64; ++i) {
        frame->metadata.sequence = i + 1;
        widget.addFrame(frame);
    }

    QImage offscreen(widget.size(), QImage::Format_ARGB32_Premultiplied);
    offscreen.fill(Qt::black);
    QPainter painter(&offscreen);

    QElapsedTimer timer;
    timer.start();
    widget.render(&painter);
    painter.end();

    const double paintMs = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
    qInfo().nospace() << "Waterfall 1920x600 CPU Raster paint time: " << paintMs << " ms";
    QVERIFY2(paintMs < 50.0, "Waterfall CPU Raster render exceeded 50 ms");
}

void WaterfallTests::waterfallAlwaysPlacesNewestFrameAtTop()
{
    WaterfallPlotWidget widget;
    widget.resize(600, 300);
    widget.setHistoryDepth(64);
    widget.setAmplitudeScale(0.0F, -100.0F);

    auto noiseFrame = std::make_shared<SpectrumFrame>();
    noiseFrame->metadata.binCount = 512;
    noiseFrame->metadata.centerFrequencyHz = 1.0e9;
    noiseFrame->metadata.spanHz = 100.0e6;
    noiseFrame->bins.assign(512, -100.0F);

    // Push 50 noise frames
    for (int i = 0; i < 50; ++i) {
        noiseFrame->metadata.sequence = i + 1;
        widget.addFrame(noiseFrame);
    }

    // Now push a frame with strong 0 dBFS peak on the left (e.g. bin 50)
    auto peakFrame = std::make_shared<SpectrumFrame>();
    peakFrame->metadata.binCount = 512;
    peakFrame->metadata.centerFrequencyHz = 1.0e9;
    peakFrame->metadata.spanHz = 100.0e6;
    peakFrame->metadata.sequence = 51;
    peakFrame->bins.assign(512, -100.0F);
    peakFrame->bins[50] = 0.0F;
    widget.addFrame(peakFrame);

    QImage img(widget.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    QPainter p(&img);
    widget.render(&p);
    p.end();

    // The top line of plotRect() near x of bin 50 must have high brightness (Hot White / Red),
    // whereas lines lower down must be dark navy blue.
    const QRect rect = widget.plotRect();
    const int peakX = rect.left() + static_cast<int>(std::round(rect.width() * (50.0 / 512.0)));
    const QColor topPixel = img.pixelColor(peakX, rect.top() + 2);
    const QColor oldPixel = img.pixelColor(peakX, rect.bottom() - 10);

    // Top pixel must be significantly brighter than old bottom noise pixel
    QVERIFY2(topPixel.red() > 200, "Newest peak was not rendered at the top of the waterfall plot");
    QVERIFY2(oldPixel.red() < 50, "Older noise line was unexpectedly bright");
}

void WaterfallTests::historyIsPreservedAcrossParameterChangesAndResize()
{
    WaterfallPlotWidget widget;
    widget.resize(600, 300);
    widget.setHistoryDepth(64);
    widget.setAmplitudeScale(0.0F, -100.0F);

    auto frame1 = std::make_shared<SpectrumFrame>();
    frame1->metadata.binCount = 512;
    frame1->metadata.centerFrequencyHz = 1.0e9;
    frame1->metadata.spanHz = 100.0e6;
    frame1->bins.assign(512, -100.0F);
    frame1->bins[50] = 0.0F; // Peak at bin 50

    for (int i = 0; i < 20; ++i) {
        frame1->metadata.sequence = i + 1;
        widget.addFrame(frame1);
    }

    // Now send frame2 with peak at a different position (e.g. bin 200)
    auto frame2 = std::make_shared<SpectrumFrame>();
    frame2->metadata.binCount = 512;
    frame2->metadata.centerFrequencyHz = 1.5e9;
    frame2->metadata.spanHz = 100.0e6;
    frame2->bins.assign(512, -100.0F);
    frame2->bins[200] = 0.0F;

    for (int i = 20; i < 40; ++i) {
        frame2->metadata.sequence = i + 1;
        widget.addFrame(frame2);
    }

    // Render to check that both the newest peak (bin 200 at top) and older peak (bin 50 lower down) exist
    QImage img(widget.size(), QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::black);
    QPainter p(&img);
    widget.render(&p);
    p.end();

    const QRect rect = widget.plotRect();
    const int newPeakX = rect.left() + static_cast<int>(std::round(rect.width() * (200.0 / 512.0)));
    const int oldPeakX = rect.left() + static_cast<int>(std::round(rect.width() * (50.0 / 512.0)));

    // New peak should be bright near the top
    const QColor newTopPixel = img.pixelColor(newPeakX, rect.top() + 4);
    QVERIFY2(newTopPixel.red() > 180, "New peak was not rendered at top");

    // Old peak should still exist in the lower part of the waterfall
    int maxOldRed = 0;
    for (int y = rect.top() + 10; y <= rect.bottom(); ++y) {
        maxOldRed = std::max(maxOldRed, img.pixelColor(oldPeakX, y).red());
    }
    QVERIFY2(maxOldRed > 180, "Old peak was incorrectly erased from history");
}

} // namespace rtsa

QTEST_MAIN(rtsa::WaterfallTests)

#include "tst_Waterfall.moc"
