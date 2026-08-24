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

} // namespace rtsa

QTEST_MAIN(rtsa::WaterfallTests)

#include "tst_Waterfall.moc"
