// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>
#include <QSettings>

#include <qml/appmode.h>
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

    void displayUnit_defaultIsBtc();
    void displayUnit_persistsToQSettings();
    void language_persistsToQSettings();
    void adaptiveSidebarLayout_defaultsToLegacy();
    void adaptiveSidebarLayout_persistsToQSettings();
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

void DisplaySettingsTests::adaptiveSidebarLayout_defaultsToLegacy()
{
    QSettings settings;
    const bool had_layout_setting = settings.contains(SettingsKeys::ADAPTIVE_SIDEBAR_LAYOUT);
    const QVariant previous_layout_setting = settings.value(SettingsKeys::ADAPTIVE_SIDEBAR_LAYOUT);
    settings.remove(SettingsKeys::ADAPTIVE_SIDEBAR_LAYOUT);

    AppMode app_mode{AppMode::DESKTOP, true};
    QVERIFY(!app_mode.adaptiveSidebarLayout());

    if (had_layout_setting) {
        settings.setValue(SettingsKeys::ADAPTIVE_SIDEBAR_LAYOUT, previous_layout_setting);
    } else {
        settings.remove(SettingsKeys::ADAPTIVE_SIDEBAR_LAYOUT);
    }
}

void DisplaySettingsTests::adaptiveSidebarLayout_persistsToQSettings()
{
    QSettings settings;
    const bool had_layout_setting = settings.contains(SettingsKeys::ADAPTIVE_SIDEBAR_LAYOUT);
    const QVariant previous_layout_setting = settings.value(SettingsKeys::ADAPTIVE_SIDEBAR_LAYOUT);
    settings.remove(SettingsKeys::ADAPTIVE_SIDEBAR_LAYOUT);

    {
        AppMode app_mode{AppMode::DESKTOP, true};
        QSignalSpy changed_spy{&app_mode, &AppMode::adaptiveSidebarLayoutChanged};
        app_mode.setAdaptiveSidebarLayout(true);
        if (!app_mode.adaptiveSidebarLayoutAvailable()) {
            QVERIFY(!app_mode.adaptiveSidebarLayout());
            QCOMPARE(changed_spy.count(), 0);
            QVERIFY(!settings.contains(SettingsKeys::ADAPTIVE_SIDEBAR_LAYOUT));
        } else {
            QVERIFY(app_mode.adaptiveSidebarLayout());
            QCOMPARE(changed_spy.count(), 1);
            QVERIFY(settings.value(SettingsKeys::ADAPTIVE_SIDEBAR_LAYOUT).toBool());
        }
    }

    AppMode restored_app_mode{AppMode::DESKTOP, true};
    QCOMPARE(restored_app_mode.adaptiveSidebarLayout(), restored_app_mode.adaptiveSidebarLayoutAvailable());

    if (had_layout_setting) {
        settings.setValue(SettingsKeys::ADAPTIVE_SIDEBAR_LAYOUT, previous_layout_setting);
    } else {
        settings.remove(SettingsKeys::ADAPTIVE_SIDEBAR_LAYOUT);
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
