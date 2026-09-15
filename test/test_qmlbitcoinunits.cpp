// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/bitcoinunits.h>

#include <consensus/amount.h>
#include <limits>

class QmlBitcoinUnitsTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void localized_amounts_data();
    void localized_amounts();
    void localized_default_and_canonical_are_independent();
    void format_btc_basic();
    void format_btc_negative();
    void format_btc_thinSpaceSeparators();
    void format_mbtc_and_ubtc();
    void format_sat_noDecimals();
    void display_unit_mapping_and_labels();
};

void QmlBitcoinUnitsTests::localized_amounts_data()
{
    QTest::addColumn<QString>("locale_name");
    QTest::addColumn<int>("unit");
    QTest::addColumn<qint64>("amount");
    QTest::addColumn<QString>("expected");
    QTest::newRow("us-sats") << QString{"en_US"} << 3 << qint64{129999999546} << QString{"129,999,999,546"};
    QTest::newRow("german-sats") << QString{"de_DE"} << 3 << qint64{129999999546} << QString{"129.999.999.546"};
    QTest::newRow("french-sats") << QString{"fr_FR"} << 3 << qint64{129999999546} << QString::fromUtf8("129\u202f999\u202f999\u202f546");
    QTest::newRow("indian-sats") << QString{"en_IN"} << 3 << qint64{123456789} << QString{"12,34,56,789"};
    QTest::newRow("spanish-btc") << QString{"es_ES"} << 0 << qint64{123456789} << QString{"1,23456789"};
    QTest::newRow("chilean-btc-precision") << QString{"es_CL"} << 0 << qint64{123456789} << QString{"1,23456789"};
    QTest::newRow("negative-satoshi") << QString{"de_DE"} << 0 << qint64{-1} << QString{"-0,00000001"};
    QTest::newRow("zero") << QString{"en_US"} << 0 << qint64{0} << QString{"0.00000000"};
    QTest::newRow("mbtc") << QString{"de_DE"} << 1 << qint64{123456789} << QString{"1.234,56789"};
    QTest::newRow("bits") << QString{"en_US"} << 2 << qint64{123456789} << QString{"1,234,567.89"};
    QTest::newRow("maximum-supply") << QString{"en_US"} << 0 << qint64{MAX_MONEY} << QString{"21,000,000.00000000"};
    QTest::newRow("beyond-double-precision") << QString{"en_US"} << 3 << qint64{9007199254740993LL} << QString{"9,007,199,254,740,993"};
    QTest::newRow("minimum-integer") << QString{"en_US"} << 3 << std::numeric_limits<qint64>::min() << QString{"-9,223,372,036,854,775,808"};
}

void QmlBitcoinUnitsTests::localized_amounts()
{
    QFETCH(QString, locale_name);
    QFETCH(int, unit);
    QFETCH(qint64, amount);
    QFETCH(QString, expected);
    QCOMPARE(QmlBitcoinUnits::formatForDisplay(QmlBitcoinUnits::fromDisplayUnit(unit), amount, false, QLocale{locale_name}), expected);
}

void QmlBitcoinUnitsTests::localized_default_and_canonical_are_independent()
{
    const QLocale previous;
    QLocale::setDefault(QLocale{"de_DE"});
    const auto localized = QmlBitcoinUnits::formatForDisplay(QmlBitcoinUnits::Unit::BTC, 123456789);
    const auto canonical = QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, 123456789, false, QmlBitcoinUnits::SeparatorStyle::NEVER);
    QLocale::setDefault(previous);
    QCOMPARE(localized, QString{"1,23456789"});
    QCOMPARE(canonical, QString{"1.23456789"});
}

void QmlBitcoinUnitsTests::format_btc_basic()
{
    QCOMPARE(QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, 0), QString("0.00000000"));
    QCOMPARE(QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, COIN), QString("1.00000000"));
    QCOMPARE(QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, 123456789), QString("1.23456789"));
}

void QmlBitcoinUnitsTests::format_btc_negative()
{
    QCOMPARE(QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, -1), QString("-0.00000001"));
}

void QmlBitcoinUnitsTests::format_btc_thinSpaceSeparators()
{
    const QString formatted = QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, 12345 * COIN);
    QCOMPARE(formatted, QString("12") + QChar(0x2009) + QString("345.00000000"));
}

void QmlBitcoinUnitsTests::format_mbtc_and_ubtc()
{
    QCOMPARE(QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::mBTC, COIN, false, QmlBitcoinUnits::SeparatorStyle::NEVER), QString("1000.00000"));
    QCOMPARE(QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::uBTC, COIN, false, QmlBitcoinUnits::SeparatorStyle::NEVER), QString("1000000.00"));
    QCOMPARE(QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::uBTC, 123, false, QmlBitcoinUnits::SeparatorStyle::NEVER), QString("1.23"));
}

void QmlBitcoinUnitsTests::format_sat_noDecimals()
{
    QCOMPARE(QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::SAT, 123), QString("123"));
}

void QmlBitcoinUnitsTests::display_unit_mapping_and_labels()
{
    QCOMPARE(QmlBitcoinUnits::fromDisplayUnit(0), QmlBitcoinUnits::Unit::BTC);
    QCOMPARE(QmlBitcoinUnits::fromDisplayUnit(1), QmlBitcoinUnits::Unit::mBTC);
    QCOMPARE(QmlBitcoinUnits::fromDisplayUnit(2), QmlBitcoinUnits::Unit::uBTC);
    QCOMPARE(QmlBitcoinUnits::fromDisplayUnit(3), QmlBitcoinUnits::Unit::SAT);
    QCOMPARE(QmlBitcoinUnits::fromDisplayUnit(99), QmlBitcoinUnits::Unit::BTC);

    QCOMPARE(QmlBitcoinUnits::label(QmlBitcoinUnits::Unit::mBTC), QString("mBTC"));
    QCOMPARE(QmlBitcoinUnits::label(QmlBitcoinUnits::Unit::uBTC), QString("bits"));
    QCOMPARE(QmlBitcoinUnits::displayLabel(QmlBitcoinUnits::Unit::SAT, 1), QString("sat"));
    QCOMPARE(QmlBitcoinUnits::displayLabel(QmlBitcoinUnits::Unit::SAT, 2), QString("sats"));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(QmlBitcoinUnitsTests)
#else
QTEST_MAIN(QmlBitcoinUnitsTests)
#endif
#include "test_qmlbitcoinunits.moc"
