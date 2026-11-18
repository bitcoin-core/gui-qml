// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/amountparse.h>

#include <QtTest/QtTest>

class AmountParseTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void defaultResultRejectsAnAmount()
    {
        const amountinput::Result result;
        QCOMPARE(result.state, amountinput::State::Invalid);
        QCOMPARE(result.satoshi, CAmount{0});
    }
    void exactInput_data();
    void exactInput();
};

void AmountParseTests::exactInput_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("decimals");
    QTest::addColumn<QChar>("separator");
    QTest::addColumn<int>("state");
    QTest::addColumn<qint64>("satoshi");
    const auto row = [](const char* name, const QString& text, int decimals, QChar separator, amountinput::State state, qint64 satoshi = 0) {
        QTest::newRow(name) << text << decimals << separator << static_cast<int>(state) << satoshi;
    };
    using enum amountinput::State;
    row("btc-dot", "0.02", 8, u'.', Valid, 2'000'000);
    row("btc-comma", "0,02", 8, u',', Valid, 2'000'000);
    row("canonical-dot-in-comma-locale", "0.02", 8, u',', Valid, 2'000'000);
    row("milli", "20", 5, u',', Valid, 2'000'000);
    row("micro", "20000", 2, u',', Valid, 2'000'000);
    row("sats", "2000000", 0, u',', Valid, 2'000'000);
    row("btc-one-sat", "0.00000001", 8, u'.', Valid, 1);
    row("milli-one-sat", "0.00001", 5, u'.', Valid, 1);
    row("micro-one-sat", "0.01", 2, u'.', Valid, 1);
    row("sat-one", "1", 0, u'.', Valid, 1);
    row("btc-max", "21000000.00000000", 8, u'.', Valid, MAX_MONEY);
    row("milli-max", "21000000000.00000", 5, u'.', Valid, MAX_MONEY);
    row("micro-max", "21000000000000.00", 2, u'.', Valid, MAX_MONEY);
    row("sat-max", "2100000000000000", 0, u'.', Valid, MAX_MONEY);
    row("btc-above-max", "21000000.00000001", 8, u'.', Invalid);
    row("milli-above-max", "21000000000.00001", 5, u'.', Invalid);
    row("micro-above-max", "21000000000000.01", 2, u'.', Invalid);
    row("sat-above-max", "2100000000000001", 0, u'.', Invalid);
    row("whole-above-max", "21000001", 8, u'.', Invalid);
    row("whole-overflow", "9999999999999999999999999999", 8, u'.', Invalid);
    row("signed-overflow", "9223372036854775808", 0, u'.', Invalid);
    row("zero", "0", 8, u'.', Valid);
    row("zero-fraction", "0.00000000", 8, u'.', Valid);
    row("leading-zeroes", "00000000000000000000000000000000000000000000000.02", 8, u'.', Valid, 2'000'000);
    row("leading-decimal", ".02", 8, u'.', Valid, 2'000'000);
    row("local-leading-decimal", ",02", 8, u',', Valid, 2'000'000);
    row("trimmed-paste", " \t0,02\n", 8, u',', Valid, 2'000'000);
    row("empty", "", 8, u'.', Empty);
    row("whitespace", " \t\n", 8, u'.', Empty);
    row("bare-dot", ".", 8, u'.', Incomplete);
    row("bare-comma", ",", 8, u',', Incomplete);
    row("trailing-dot", "1.", 8, u'.', Incomplete);
    row("trailing-comma", "0,", 8, u',', Incomplete);
    row("wrong-locale", "0,02", 8, u'.', Invalid);
    row("duplicate-dot", "1.2.3", 8, u'.', Invalid);
    row("mixed-separators", "1,2.3", 8, u',', Invalid);
    row("mixed-reverse", "1.2,3", 8, u',', Invalid);
    row("spaces-not-grouping", "1 000", 8, u'.', Invalid);
    row("thin-space-not-grouping", QString::fromUtf8("1\xe2\x80\x89" "000"), 8, u'.', Invalid);
    row("apostrophe-not-grouping", "1'000", 8, u'.', Invalid);
    row("junk-prefix", "abc1.2", 8, u'.', Invalid);
    row("junk-suffix", "1.2BTC", 8, u'.', Invalid);
    row("negative", "-1", 8, u'.', Invalid);
    row("positive-sign", "+1", 8, u'.', Invalid);
    row("exponent", "1e2", 8, u'.', Invalid);
    row("nan", "NaN", 8, u'.', Invalid);
    row("btc-precision", "0.000000001", 8, u'.', Invalid);
    row("milli-precision", "1.234567", 5, u'.', Invalid);
    row("micro-precision", "1.239", 2, u'.', Invalid);
    row("sat-fraction", "1.0", 0, u'.', Invalid);
    row("sat-local-fraction", "1,0", 0, u',', Invalid);
    row("negative-precision", "1", -1, u'.', Invalid);
    row("unsupported-precision", "1", 9, u'.', Invalid);
    row("length-limit", QString(128, u'0'), 8, u'.', Valid);
    row("too-long", QString(129, u'0'), 8, u'.', Invalid);
    row("too-long-whitespace", QString(129, u' '), 8, u'.', Invalid);
    row("arabic-decimal", QString::fromUtf8("٠٫٠٢"), 8, QChar{0x066b}, Valid, 2'000'000);
    row("fullwidth-digits", QString::fromUtf8("０.０２"), 8, u'.', Valid, 2'000'000);
}

void AmountParseTests::exactInput()
{
    QFETCH(QString, text); QFETCH(int, decimals); QFETCH(QChar, separator); QFETCH(int, state); QFETCH(qint64, satoshi);
    const auto result = amountinput::Parse(text, decimals, separator);
    QCOMPARE(static_cast<int>(result.state), state);
    QCOMPARE(result.satoshi, satoshi);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(AmountParseTests)
#else
QTEST_MAIN(AmountParseTests)
#endif
#include <test_amountparse.moc>
