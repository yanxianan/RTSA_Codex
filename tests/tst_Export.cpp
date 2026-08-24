#include "services/SpectrumExporter.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>
#include <limits>

namespace rtsa {

class ExportTests final : public QObject {
    Q_OBJECT

private slots:
    void writesCompleteCsvSnapshot();
    void rejectsInvalidFrameWithoutPartialFile();
    void writesDynamicUnitAndCalibrationMetadata_data();
    void writesDynamicUnitAndCalibrationMetadata();
    void rejectsNonFiniteBins_data();
    void rejectsNonFiniteBins();
};

void ExportTests::writesCompleteCsvSnapshot()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.sequence = 42;
    frame->metadata.timestampNs = 123456789;
    frame->metadata.centerFrequencyHz = 100.0e6;
    frame->metadata.spanHz = 20.0e6;
    frame->metadata.binCount = 4;
    frame->bins = { -100.0F, -20.5F, -30.25F, -90.0F };

    const QString path = directory.filePath(QStringLiteral("snapshot.csv"));
    const ExportResult result = SpectrumExporter::writeCsv(frame, path);
    QVERIFY2(result.success, qPrintable(result.errorMessage));
    QCOMPARE(result.exportedBins, std::size_t(4));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray data = file.readAll();
    QVERIFY(data.contains("# sequence,42\n"));
    QVERIFY(data.contains("# amplitude_unit,dBFS\n"));
    QVERIFY(data.contains("# calibrated,false\n"));
    QVERIFY(data.contains("bin,frequency_hz,amplitude_dbfs\n"));
    QVERIFY(data.contains("0,90000000.000000,-100.000000\n"));
    QVERIFY(data.contains("3,105000000.000000,-90.000000\n"));
}

void ExportTests::writesDynamicUnitAndCalibrationMetadata_data()
{
    QTest::addColumn<int>("unitValue");
    QTest::addColumn<QByteArray>("symbol");
    QTest::addColumn<QByteArray>("column");
    QTest::newRow("dbfs") << static_cast<int>(AmplitudeUnit::Dbfs) << QByteArray("dBFS")
                           << QByteArray("amplitude_dbfs");
    QTest::newRow("dbm") << static_cast<int>(AmplitudeUnit::Dbm) << QByteArray("dBm")
                          << QByteArray("amplitude_dbm");
    QTest::newRow("dbc") << static_cast<int>(AmplitudeUnit::Dbc) << QByteArray("dBc")
                          << QByteArray("amplitude_dbc");
    QTest::newRow("linear") << static_cast<int>(AmplitudeUnit::LinearPower) << QByteArray("Power")
                             << QByteArray("power_linear");
}

void ExportTests::writesDynamicUnitAndCalibrationMetadata()
{
    QFETCH(int, unitValue);
    QFETCH(QByteArray, symbol);
    QFETCH(QByteArray, column);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.binCount = 2;
    frame->metadata.unit = static_cast<AmplitudeUnit>(unitValue);
    frame->metadata.calibrated = true;
    frame->bins = { -10.0F, -20.0F };
    const QString path = directory.filePath(QStringLiteral("unit.csv"));

    const ExportResult result = SpectrumExporter::writeCsv(frame, path);
    QVERIFY(result.success);
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray data = file.readAll();
    QVERIFY(data.contains("# amplitude_unit," + symbol + "\n"));
    QVERIFY(data.contains("# calibrated,true\n"));
    QVERIFY(data.contains("bin,frequency_hz," + column + "\n"));
}

void ExportTests::rejectsNonFiniteBins_data()
{
    QTest::addColumn<float>("invalidValue");
    QTest::newRow("nan") << std::numeric_limits<float>::quiet_NaN();
    QTest::newRow("positive-infinity") << std::numeric_limits<float>::infinity();
    QTest::newRow("negative-infinity") << -std::numeric_limits<float>::infinity();
}

void ExportTests::rejectsNonFiniteBins()
{
    QFETCH(float, invalidValue);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.binCount = 2;
    frame->bins = { -10.0F, invalidValue };
    const QString path = directory.filePath(QStringLiteral("invalid-float.csv"));

    const ExportResult result = SpectrumExporter::writeCsv(frame, path);
    QVERIFY(!result.success);
    QVERIFY(!QFile::exists(path));
}

void ExportTests::rejectsInvalidFrameWithoutPartialFile()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto frame = std::make_shared<SpectrumFrame>();
    frame->metadata.binCount = 8;
    frame->bins = { -10.0F };

    const QString path = directory.filePath(QStringLiteral("invalid.csv"));
    const ExportResult result = SpectrumExporter::writeCsv(frame, path);
    QVERIFY(!result.success);
    QVERIFY(!QFile::exists(path));
}

} // namespace rtsa

QTEST_APPLESS_MAIN(rtsa::ExportTests)

#include "tst_Export.moc"
