#include "services/ConfigurationStore.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

namespace rtsa {

class SettingsTests final : public QObject {
    Q_OBJECT

private slots:
    void missingSettingsUseSafeDefaults();
    void validSettingsRoundTrip();
    void saveRequiresAtomicSync();
    void corruptValueRejectsWholeSnapshot();
    void legacySettingsAreMigrated();
    void versionOneSettingsAreMigrated();
    void versionTwoSettingsAreMigrated();
    void versionThreeSettingsAreMigrated();
    void unsupportedVersionUsesSafeDefaults();
};

void SettingsTests::missingSettingsUseSafeDefaults()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings storage(directory.filePath(QStringLiteral("missing.ini")), QSettings::IniFormat);

    const SettingsLoadResult result = ConfigurationStore::load(storage);
    QVERIFY(!result.loadedFromStorage);
    QVERIFY(result.settings.isValid());
    QCOMPARE(result.settings.centerFrequencyMHz, 1000.0);
    QVERIFY(result.warning.isEmpty());
}

void SettingsTests::validSettingsRoundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings storage(directory.filePath(QStringLiteral("valid.ini")), QSettings::IniFormat);
    AppSettings expected;
    expected.centerFrequencyMHz = 2450.5;
    expected.spanMHz = 80.0;
    expected.binCount = 65536;
    expected.sourceFrameRate = 500.0;
    expected.unthrottled = true;
    expected.tone1Enabled = false;
    expected.tone1FrequencyMHz = 987.5;
    expected.tone1WidthMHz = 4.5;
    expected.tone2WidthMHz = 6.5;
    expected.sweepStartFrequencyMHz = 970.0;
    expected.sweepStopFrequencyMHz = 1025.0;
    expected.sweepPeriodSeconds = 7.5;
    expected.sweepDirection = 2;
    expected.transientDurationSeconds = 1.25;
    expected.traceMode = 2;
    expected.averageCount = 32;
    expected.plotColorPreset = 6;
    expected.customTraceColorHex = QStringLiteral("#ff4081");
    expected.plotLineWidth = 3;
    expected.plotGridVisible = false;
    expected.plotTheme = 4;
    expected.customThemeColorHex = QStringLiteral("#07131e");
    expected.displayViewMode = 1;
    expected.waterfallColormap = 2;
    expected.waterfallHistoryDepth = 256;

    const SettingsSaveResult saved = ConfigurationStore::save(storage, expected);
    QVERIFY2(saved.success, qPrintable(saved.errorMessage));
    const SettingsLoadResult loaded = ConfigurationStore::load(storage);
    QVERIFY2(loaded.loadedFromStorage, qPrintable(loaded.warning));
    QVERIFY(!loaded.migratedLegacy);
    QCOMPARE(loaded.settings.centerFrequencyMHz, expected.centerFrequencyMHz);
    QCOMPARE(loaded.settings.binCount, expected.binCount);
    QCOMPARE(loaded.settings.traceMode, expected.traceMode);
    QCOMPARE(loaded.settings.displayViewMode, expected.displayViewMode);
    QCOMPARE(loaded.settings.waterfallColormap, expected.waterfallColormap);
    QCOMPARE(loaded.settings.waterfallHistoryDepth, expected.waterfallHistoryDepth);
    QCOMPARE(loaded.settings.tone1Enabled, expected.tone1Enabled);
    QCOMPARE(loaded.settings.tone1FrequencyMHz, expected.tone1FrequencyMHz);
    QCOMPARE(loaded.settings.tone1WidthMHz, expected.tone1WidthMHz);
    QCOMPARE(loaded.settings.tone2WidthMHz, expected.tone2WidthMHz);
    QCOMPARE(loaded.settings.sweepStartFrequencyMHz, expected.sweepStartFrequencyMHz);
    QCOMPARE(loaded.settings.sweepStopFrequencyMHz, expected.sweepStopFrequencyMHz);
    QCOMPARE(loaded.settings.unthrottled, expected.unthrottled);
    QCOMPARE(loaded.settings.sweepPeriodSeconds, expected.sweepPeriodSeconds);
    QCOMPARE(loaded.settings.sweepDirection, expected.sweepDirection);
    QCOMPARE(loaded.settings.transientDurationSeconds,
             expected.transientDurationSeconds);
    QCOMPARE(loaded.settings.plotColorPreset, expected.plotColorPreset);
    QCOMPARE(loaded.settings.customTraceColorHex, expected.customTraceColorHex);
    QCOMPARE(loaded.settings.plotLineWidth, expected.plotLineWidth);
    QCOMPARE(loaded.settings.plotGridVisible, expected.plotGridVisible);
    QCOMPARE(loaded.settings.plotTheme, expected.plotTheme);
    QCOMPARE(loaded.settings.customThemeColorHex, expected.customThemeColorHex);
}

void SettingsTests::saveRequiresAtomicSync()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings storage(directory.filePath(QStringLiteral("atomic.ini")),
                      QSettings::IniFormat);
    storage.setAtomicSyncRequired(false);

    const SettingsSaveResult saved = ConfigurationStore::save(storage, AppSettings {});
    QVERIFY2(saved.success, qPrintable(saved.errorMessage));
    QVERIFY(storage.isAtomicSyncRequired());
}

void SettingsTests::corruptValueRejectsWholeSnapshot()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings storage(directory.filePath(QStringLiteral("corrupt.ini")), QSettings::IniFormat);
    QVERIFY(ConfigurationStore::save(storage, AppSettings {}).success);
    storage.setValue(QStringLiteral("Spectrum/centerFrequencyMHz"),
                     QStringLiteral("not-a-number"));
    storage.sync();

    const SettingsLoadResult result = ConfigurationStore::load(storage);
    QVERIFY(!result.loadedFromStorage);
    QCOMPARE(result.settings.centerFrequencyMHz, 1000.0);
    QCOMPARE(result.settings.spanMHz, 200.0);
    QVERIFY(!result.warning.isEmpty());
}

void SettingsTests::legacySettingsAreMigrated()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings storage(directory.filePath(QStringLiteral("legacy.ini")), QSettings::IniFormat);
    storage.setValue(QStringLiteral("Spectrum/centerFrequencyMHz"), 915.0);
    storage.setValue(QStringLiteral("Spectrum/spanMHz"), 40.0);
    storage.setValue(QStringLiteral("Spectrum/binCount"), 8192);
    storage.sync();

    const SettingsLoadResult result = ConfigurationStore::load(storage);
    QVERIFY(result.loadedFromStorage);
    QVERIFY(result.migratedLegacy);
    QCOMPARE(result.settings.centerFrequencyMHz, 915.0);
    QCOMPARE(result.settings.spanMHz, 40.0);
    QCOMPARE(result.settings.binCount, 8192);
}

void SettingsTests::versionOneSettingsAreMigrated()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings storage(directory.filePath(QStringLiteral("version-one.ini")),
                      QSettings::IniFormat);
    storage.setValue(QStringLiteral("schemaVersion"), 1);
    storage.setValue(QStringLiteral("Spectrum/centerFrequencyMHz"), 433.92);
    storage.setValue(QStringLiteral("Spectrum/sweepPeriodSeconds"), 2.5);
    storage.sync();

    const SettingsLoadResult result = ConfigurationStore::load(storage);
    QVERIFY(result.loadedFromStorage);
    QVERIFY(result.migratedLegacy);
    QCOMPARE(result.settings.centerFrequencyMHz, 433.92);
    QCOMPARE(result.settings.sweepPeriodSeconds, 2.5);
    QCOMPARE(result.settings.sweepDirection, 0);
    QCOMPARE(result.settings.transientDurationSeconds, 0.1);
    QCOMPARE(result.settings.tone1WidthMHz, 1.5);
    QCOMPARE(result.settings.sweepStartFrequencyMHz, 930.0);
}

void SettingsTests::versionTwoSettingsAreMigrated()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings storage(directory.filePath(QStringLiteral("version-two.ini")),
                      QSettings::IniFormat);
    storage.setValue(QStringLiteral("schemaVersion"), 2);
    storage.setValue(QStringLiteral("Spectrum/centerFrequencyMHz"), 2400.0);
    storage.setValue(QStringLiteral("Spectrum/sweepDirection"), 1);
    storage.setValue(QStringLiteral("Spectrum/transientDurationSeconds"), 0.75);
    storage.sync();

    const SettingsLoadResult result = ConfigurationStore::load(storage);
    QVERIFY(result.loadedFromStorage);
    QVERIFY(result.migratedLegacy);
    QCOMPARE(result.settings.centerFrequencyMHz, 2400.0);
    QCOMPARE(result.settings.sweepDirection, 1);
    QCOMPARE(result.settings.transientDurationSeconds, 0.75);
    QCOMPARE(result.settings.unthrottled, false);
    QCOMPARE(result.settings.tone2WidthMHz, 2.5);
    QCOMPARE(result.settings.sweepStopFrequencyMHz, 1070.0);
}

void SettingsTests::versionThreeSettingsAreMigrated()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings storage(directory.filePath(QStringLiteral("version-three.ini")),
                      QSettings::IniFormat);
    storage.setValue(QStringLiteral("schemaVersion"), 3);
    storage.setValue(QStringLiteral("Spectrum/centerFrequencyMHz"), 5800.0);
    storage.setValue(QStringLiteral("Spectrum/traceMode"), 2);
    storage.sync();

    const SettingsLoadResult result = ConfigurationStore::load(storage);
    QVERIFY(result.loadedFromStorage);
    QVERIFY(result.migratedLegacy);
    QCOMPARE(result.settings.centerFrequencyMHz, 5800.0);
    QCOMPARE(result.settings.traceMode, 2);
    QCOMPARE(result.settings.plotColorPreset, 0);
    QCOMPARE(result.settings.plotLineWidth, 1);
    QCOMPARE(result.settings.plotGridVisible, true);
    QCOMPARE(result.settings.plotTheme, 0);
}

void SettingsTests::unsupportedVersionUsesSafeDefaults()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings storage(directory.filePath(QStringLiteral("future.ini")), QSettings::IniFormat);
    storage.setValue(QStringLiteral("schemaVersion"), AppSettings::schemaVersion + 1);
    storage.setValue(QStringLiteral("Spectrum/centerFrequencyMHz"), 1234.0);
    storage.sync();

    const SettingsLoadResult result = ConfigurationStore::load(storage);
    QVERIFY(!result.loadedFromStorage);
    QCOMPARE(result.settings.centerFrequencyMHz, 1000.0);
    QVERIFY(!result.warning.isEmpty());
}

} // namespace rtsa

QTEST_APPLESS_MAIN(rtsa::SettingsTests)

#include "tst_Settings.moc"
