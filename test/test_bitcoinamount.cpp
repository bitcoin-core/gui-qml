// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <QLocale>
#include <QScopeGuard>

#include <qml/bitcoinamount.h>

class BitcoinAmountTests : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void satsToBtcString_basic();
    void satsToBtcString_negative();
    void btcToSats_roundtrip();
    void sanitize_clampsAndFilters();
    void display_flow_btc();
    void display_flow_mbtc_and_ubtc();
    void display_flow_sat();
    void displayWithUnit_formatsAmountAndUnit();
    void localizedDisplayWithUnit_usesLocaleAndKeepsCanonicalDisplay();
    void flipUnit_changesLabelAndDisplaySignal();
};

void BitcoinAmountTests::satsToBtcString_basic()
{
    QCOMPARE(BitcoinAmount::satsToBtcString(0), QString("0.00000000"));
    QCOMPARE(BitcoinAmount::satsToBtcString(1), QString("0.00000001"));
    QCOMPARE(BitcoinAmount::satsToBtcString(COIN), QString("1.00000000"));
    QCOMPARE(BitcoinAmount::satsToBtcString(21 * COIN), QString("21.00000000"));
    QCOMPARE(BitcoinAmount::satsToBtcString(100000123), QString("1.00000123"));
}

void BitcoinAmountTests::satsToBtcString_negative()
{
    QCOMPARE(BitcoinAmount::satsToBtcString(-1), QString("-0.00000001"));
}

void BitcoinAmountTests::btcToSats_roundtrip()
{
    BitcoinAmount amt;
    amt.setUnit(BitcoinAmount::Unit::BTC);
    amt.fromDisplay("1.23456789");
    QCOMPARE(amt.toDisplay(), QString("1.23456789"));
}

void BitcoinAmountTests::sanitize_clampsAndFilters()
{
    BitcoinAmount amt;
    amt.setUnit(BitcoinAmount::Unit::BTC);
    amt.fromDisplay("abc1.2.3xyz");
    QCOMPARE(amt.toDisplay(), QString("1.20000000"));
}

void BitcoinAmountTests::display_flow_btc()
{
    BitcoinAmount amt;
    amt.setUnit(BitcoinAmount::Unit::BTC);
    amt.setSatoshi(2 * COIN);
    QCOMPARE(amt.toDisplay(), QString("2.00000000"));
}

void BitcoinAmountTests::display_flow_mbtc_and_ubtc()
{
    BitcoinAmount amt;
    amt.setUnit(BitcoinAmount::Unit::mBTC);
    amt.setSatoshi(COIN);
    QCOMPARE(amt.toDisplay(), QString("1000.00000"));
    QCOMPARE(amt.unitLabel(), QString("mBTC"));

    amt.fromDisplay("1.23456789");
    QCOMPARE(amt.satoshi(), qint64{123456});
    QCOMPARE(amt.toDisplay(), QString("1.23456"));

    amt.setUnit(BitcoinAmount::Unit::uBTC);
    QCOMPARE(amt.toDisplay(), QString("1234.56"));
    QCOMPARE(amt.unitLabel(), QString("bits"));

    amt.fromDisplay("1.239");
    QCOMPARE(amt.satoshi(), qint64{123});
    QCOMPARE(amt.toDisplay(), QString("1.23"));
}

void BitcoinAmountTests::display_flow_sat()
{
    BitcoinAmount amt;
    amt.setUnit(BitcoinAmount::Unit::SAT);
    amt.setSatoshi(123);
    QCOMPARE(amt.toDisplay(), QString("123"));
}

void BitcoinAmountTests::displayWithUnit_formatsAmountAndUnit()
{
    BitcoinAmount amt;
    QVERIFY(amt.displayWithUnit().isEmpty());

    amt.setSatoshi(COIN);
    QCOMPARE(amt.displayWithUnit(), QStringLiteral("1.00000000 ₿"));

    amt.setUnit(BitcoinAmount::Unit::SAT);
    QCOMPARE(amt.displayWithUnit(), QStringLiteral("100000000 sats"));

    amt.setSatoshi(1);
    QCOMPARE(amt.displayWithUnit(), QStringLiteral("1 sat"));
}

void BitcoinAmountTests::localizedDisplayWithUnit_usesLocaleAndKeepsCanonicalDisplay()
{
    const QLocale previous;
    const auto restore_locale = qScopeGuard([previous] { QLocale::setDefault(previous); });
    QLocale::setDefault(QLocale{"de_DE"});

    BitcoinAmount amount;
    QVERIFY(amount.localizedDisplayWithUnit().isEmpty());
    amount.setSatoshi(123'456'789);
    QCOMPARE(amount.localizedDisplayWithUnit(), QStringLiteral("1,23456789 BTC"));
    QCOMPARE(amount.displayWithUnit(), QStringLiteral("1.23456789 ₿"));

    amount.setUnit(BitcoinAmount::Unit::SAT);
    QCOMPARE(amount.localizedDisplayWithUnit(), QStringLiteral("123.456.789 sat"));
    QCOMPARE(amount.displayWithUnit(), QStringLiteral("123456789 sats"));
}

void BitcoinAmountTests::flipUnit_changesLabelAndDisplaySignal()
{
    BitcoinAmount amt;
    amt.setSatoshi(100);
    QSignalSpy spy(&amt, &BitcoinAmount::displayChanged);
    amt.flipUnit();
    QVERIFY(spy.count() >= 1);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(BitcoinAmountTests)
#else
QTEST_MAIN(BitcoinAmountTests)
#endif
#include "test_bitcoinamount.moc"
