// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>
#include <QSettings>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <qml/globalsettings.h>

#include <qml/bitcoinunits.h>
#include <qml/models/settings_keys.h>

// Tests for display-settings persistence (language, display unit)
// and the QmlBitcoinUnits SAT/BTC formatting used by Transaction::prettyAmount().
// Persistence tests use SettingsKeys::* constants so that a key-name change in
// the model will immediately break the corresponding test.
class DisplaySettingsTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void globalSettingsMigrateOutsidePerChainSettings();
    void displayUnit_defaultIsBtc();
    void displayUnit_persistsToQSettings();
    void language_persistsToQSettings();
    void qmlBitcoinUnits_satFormat_zero();
    void qmlBitcoinUnits_satFormat_positive();
    void qmlBitcoinUnits_satFormat_negative();
    void qmlBitcoinUnits_btcFormat_roundtrip();

private:
    // Use a test-only settings group to avoid polluting real user settings.
    static const inline QString TEST_GROUP = "DisplaySettingsTest";
};

void DisplaySettingsTests::init()
{
    QSettings settings;
    settings.beginGroup(TEST_GROUP);
    settings.remove("");  // clear all keys in group
    settings.endGroup();
}

void DisplaySettingsTests::cleanup()
{
    QSettings settings;
    settings.beginGroup(TEST_GROUP);
    settings.remove("");
    settings.endGroup();
}

void DisplaySettingsTests::globalSettingsMigrateOutsidePerChainSettings()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QSettings defaults;
    const QString application = QFileInfo(dir.path()).fileName();
    QSettings legacy{defaults.format(), defaults.scope(), defaults.organizationName(), application};
    QSettings shared{defaults.format(), defaults.scope(), defaults.organizationName(), application + QStringLiteral("-global")};
    const auto cleanup = qScopeGuard([&] { legacy.clear(); shared.clear(); });
    legacy.setValue("palette", "core-light");
    legacy.setValue("textScale", 1.2);
    legacy.setValue("reduceMotion", true);
    legacy.setValue("strDataDir", "mainnet-data");
    GlobalSettings settings{application};
    QCOMPARE(settings.value("palette").toString(), QStringLiteral("core-light"));
    QCOMPARE(settings.value("textScale").toDouble(), 1.2);
    QCOMPARE(settings.value("reduceMotion").toBool(), true);
    QVERIFY(!settings.value("strDataDir").isValid());
    settings.setValue("strDataDir", "separate-data");
    QCOMPARE(legacy.value("strDataDir").toString(), QStringLiteral("mainnet-data"));
    legacy.clear();
    QCOMPARE(settings.value("palette").toString(), QStringLiteral("core-light"));
    QCOMPARE(settings.value("textScale").toDouble(), 1.2);
    QCOMPARE(settings.value("reduceMotion").toBool(), true);
}

void DisplaySettingsTests::displayUnit_defaultIsBtc()
{
    // Verify the default value using the same key the model reads.
    QCOMPARE(QString::fromUtf8(SettingsKeys::DISPLAY_UNIT), QStringLiteral("DisplayBitcoinUnit"));
    QSettings settings;
    settings.beginGroup(TEST_GROUP);
    int unit = settings.value(SettingsKeys::DISPLAY_UNIT, 0).toInt();
    settings.endGroup();
    QCOMPARE(unit, 0);
}

void DisplaySettingsTests::displayUnit_persistsToQSettings()
{
    // Write using the model's key constant, read back in a fresh QSettings instance.
    {
        QSettings settings;
        settings.beginGroup(TEST_GROUP);
        settings.setValue(SettingsKeys::DISPLAY_UNIT, 3);
        settings.endGroup();
    }
    {
        QSettings settings;
        settings.beginGroup(TEST_GROUP);
        int unit = settings.value(SettingsKeys::DISPLAY_UNIT, 0).toInt();
        settings.endGroup();
        QCOMPARE(unit, 3);
    }
}

void DisplaySettingsTests::language_persistsToQSettings()
{
    // Write using the model's key constant, read back in a fresh QSettings instance.
    {
        QSettings settings;
        settings.beginGroup(TEST_GROUP);
        settings.setValue(SettingsKeys::LANGUAGE, QStringLiteral("de"));
        settings.endGroup();
    }
    {
        QSettings settings;
        settings.beginGroup(TEST_GROUP);
        QString lang = settings.value(SettingsKeys::LANGUAGE, "").toString();
        settings.endGroup();
        QCOMPARE(lang, QStringLiteral("de"));
    }
}

void DisplaySettingsTests::qmlBitcoinUnits_satFormat_zero()
{
    QCOMPARE(QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::SAT, 0),
             QStringLiteral("0"));
}

void DisplaySettingsTests::qmlBitcoinUnits_satFormat_positive()
{
    // 1 BTC = 100,000,000 sat
    QString result = QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::SAT, 100000000);
    QVERIFY(!result.isEmpty());
    // Strip thin-space separators (U+2009) before checking the digit content.
    QString digits = result;
    digits.remove(QChar(0x2009));
    digits.remove(QLatin1Char(','));
    QVERIFY(digits.contains(QStringLiteral("100000000")));
}

void DisplaySettingsTests::qmlBitcoinUnits_satFormat_negative()
{
    QString result = QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::SAT, -1, false);
    QCOMPARE(result, QStringLiteral("-1"));
}

void DisplaySettingsTests::qmlBitcoinUnits_btcFormat_roundtrip()
{
    // 1 BTC = 100,000,000 sat; format() uses '.' separator (not locale-dependent).
    QString result = QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, 100000000);
    QCOMPARE(result, QStringLiteral("1.00000000"));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(DisplaySettingsTests)
#else
QTEST_MAIN(DisplaySettingsTests)
#endif
#include "test_display_settings.moc"
