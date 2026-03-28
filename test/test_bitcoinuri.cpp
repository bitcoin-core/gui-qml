// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <chainparams.h>
#include <qml/models/bitcoinuri.h>
#include <util/chaintype.h>

// A known valid mainnet P2WPKH bech32 address used across most tests.
static const QString TEST_ADDRESS = QStringLiteral("bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4");
// A known valid mainnet P2PKH (legacy) address (from bitcoin/src/test/util_tests.cpp).
static const QString P2PKH_ADDRESS = QStringLiteral("1KqbBpLy5FARmTPD4VZnDDpYjkUvkr82Pm");

class BitcoinUriTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void parse_valid_uri_with_amount_label_and_message();
    void parse_valid_uri_address_only();
    void parse_rejects_empty_input();
    void parse_rejects_bitcoin_double_slash_scheme();
    void parse_rejects_invalid_scheme();
    void parse_rejects_invalid_amount();
    void parse_rejects_unknown_required_parameter();
    void parse_rejects_invalid_address();
    void parse_ignores_unknown_optional_parameter();
    void parse_valid_uri_with_zero_amount();
    void parse_valid_uri_uppercase_scheme();
    void parse_valid_uri_with_empty_amount_param();
    void parse_valid_uri_legacy_p2pkh_address();

    // Duplicate / invalid amount edge cases
    void parse_rejects_comma_in_amount();
    void parse_duplicate_amount_last_wins();
    void parse_rejects_invalid_duplicate_amount();

    // req- prefix on known parameters must succeed
    void parse_known_required_label();
    void parse_known_required_message();
    void parse_known_required_amount();

    // Percent-decoding behaviour (QUrl::FullyDecoded — more correct than Bitcoin Core Qt)
    void parse_decodes_percent_encoded_label();
};

void BitcoinUriTests::initTestCase()
{
    SelectParams(ChainType::MAIN);
}

void BitcoinUriTests::parse_valid_uri_with_amount_label_and_message()
{
    const QString uri = QStringLiteral("bitcoin:%1?amount=0.12345678&label=Invoice%2019&message=Rent")
        .arg(TEST_ADDRESS);

    const BitcoinUriParseResult result = BitcoinUri::Parse(uri);
    QVERIFY(result.success);
    QCOMPARE(result.address, TEST_ADDRESS);
    QCOMPARE(result.amount_sats, CAmount{12345678});
    QVERIFY(result.has_amount);
    QVERIFY(result.has_label);
    QCOMPARE(result.label, QStringLiteral("Invoice 19"));
    QVERIFY(result.has_message);
    QCOMPARE(result.message, QStringLiteral("Rent"));
}

void BitcoinUriTests::parse_valid_uri_address_only()
{
    const QString uri = QStringLiteral("bitcoin:%1").arg(TEST_ADDRESS);

    const BitcoinUriParseResult result = BitcoinUri::Parse(uri);
    QVERIFY(result.success);
    QCOMPARE(result.address, TEST_ADDRESS);
    QVERIFY(!result.has_amount);
    QVERIFY(!result.has_label);
    QVERIFY(!result.has_message);
}

void BitcoinUriTests::parse_rejects_empty_input()
{
    const BitcoinUriParseResult result = BitcoinUri::Parse(QString());
    QVERIFY(!result.success);
    QVERIFY(!result.error.isEmpty());
}

void BitcoinUriTests::parse_rejects_bitcoin_double_slash_scheme()
{
    const QString uri = QStringLiteral("bitcoin://%1?amount=0.1").arg(TEST_ADDRESS);

    const BitcoinUriParseResult result = BitcoinUri::Parse(uri);
    QVERIFY(!result.success);
    QVERIFY(result.error.contains(QStringLiteral("bitcoin://")));
}

void BitcoinUriTests::parse_rejects_invalid_scheme()
{
    const BitcoinUriParseResult result = BitcoinUri::Parse(QStringLiteral("notbitcoin:addr?amount=0.1"));
    QVERIFY(!result.success);
}

void BitcoinUriTests::parse_rejects_invalid_amount()
{
    // More than 8 decimal places is not a valid satoshi amount.
    const QString uri = QStringLiteral("bitcoin:%1?amount=0.123456789").arg(TEST_ADDRESS);

    const BitcoinUriParseResult result = BitcoinUri::Parse(uri);
    QVERIFY(!result.success);
    QVERIFY(result.error.contains(QStringLiteral("Invalid Bitcoin amount")));
}

void BitcoinUriTests::parse_rejects_unknown_required_parameter()
{
    const QString uri = QStringLiteral("bitcoin:%1?req-foo=1").arg(TEST_ADDRESS);

    const BitcoinUriParseResult result = BitcoinUri::Parse(uri);
    QVERIFY(!result.success);
    QVERIFY(result.error.contains(QStringLiteral("Unsupported required parameter")));
}

void BitcoinUriTests::parse_rejects_invalid_address()
{
    const BitcoinUriParseResult result = BitcoinUri::Parse(
        QStringLiteral("bitcoin:not-a-valid-address?amount=0.1"));
    QVERIFY(!result.success);
}

void BitcoinUriTests::parse_ignores_unknown_optional_parameter()
{
    const QString uri = QStringLiteral("bitcoin:%1?unknown=value").arg(TEST_ADDRESS);

    const BitcoinUriParseResult result = BitcoinUri::Parse(uri);
    QVERIFY(result.success);
    QCOMPARE(result.address, TEST_ADDRESS);
}

void BitcoinUriTests::parse_valid_uri_with_zero_amount()
{
    const QString uri = QStringLiteral("bitcoin:%1?amount=0").arg(TEST_ADDRESS);
    const BitcoinUriParseResult result = BitcoinUri::Parse(uri);
    QVERIFY(result.success);
    QVERIFY(result.has_amount);
    QCOMPARE(result.amount_sats, CAmount{0});
}

void BitcoinUriTests::parse_valid_uri_uppercase_scheme()
{
    // BIP21 scheme matching must be case-insensitive.
    const QString uri = QStringLiteral("BITCOIN:%1?amount=0.001").arg(TEST_ADDRESS);
    const BitcoinUriParseResult result = BitcoinUri::Parse(uri);
    QVERIFY(result.success);
    QCOMPARE(result.address, TEST_ADDRESS);
}

void BitcoinUriTests::parse_valid_uri_with_empty_amount_param()
{
    // An empty amount value must be treated as absent, not an error.
    const QString uri = QStringLiteral("bitcoin:%1?amount=").arg(TEST_ADDRESS);
    const BitcoinUriParseResult result = BitcoinUri::Parse(uri);
    QVERIFY(result.success);
    QVERIFY(!result.has_amount);
}

void BitcoinUriTests::parse_valid_uri_legacy_p2pkh_address()
{
    const QString uri = QStringLiteral("bitcoin:%1").arg(P2PKH_ADDRESS);
    const BitcoinUriParseResult result = BitcoinUri::Parse(uri);
    QVERIFY(result.success);
    QCOMPARE(result.address, P2PKH_ADDRESS);
}

// ---------------------------------------------------------------------------
// Duplicate / invalid amount edge cases
// ---------------------------------------------------------------------------

void BitcoinUriTests::parse_rejects_comma_in_amount()
{
    // ParseMoney only accepts digits and '.'; a comma must be rejected.
    const QString uri = QStringLiteral("bitcoin:%1?amount=1,000").arg(TEST_ADDRESS);
    const BitcoinUriParseResult result = BitcoinUri::Parse(uri);
    QVERIFY(!result.success);
    QVERIFY(result.error.contains(QStringLiteral("Invalid Bitcoin amount")));
}

void BitcoinUriTests::parse_duplicate_amount_last_wins()
{
    // When amount appears twice, the last valid value must win — matching
    // the behaviour documented in Bitcoin Core's src/qt/test/uritests.cpp.
    const QString uri = QStringLiteral("bitcoin:%1?amount=1&amount=2").arg(TEST_ADDRESS);
    const BitcoinUriParseResult result = BitcoinUri::Parse(uri);
    QVERIFY(result.success);
    QVERIFY(result.has_amount);
    QCOMPARE(result.amount_sats, CAmount{200000000}); // 2 BTC
}

void BitcoinUriTests::parse_rejects_invalid_duplicate_amount()
{
    // A valid first amount followed by an invalid second amount must fail.
    // Bitcoin Core documents the same behaviour.
    const QString uri = QStringLiteral("bitcoin:%1?amount=1&amount=bad").arg(TEST_ADDRESS);
    const BitcoinUriParseResult result = BitcoinUri::Parse(uri);
    QVERIFY(!result.success);
    QVERIFY(result.error.contains(QStringLiteral("Invalid Bitcoin amount")));
}

// ---------------------------------------------------------------------------
// req- prefix on known parameters must succeed (BIP21 §req-* semantics)
// ---------------------------------------------------------------------------

void BitcoinUriTests::parse_known_required_label()
{
    const QString uri = QStringLiteral("bitcoin:%1?req-label=Merchant").arg(TEST_ADDRESS);
    const BitcoinUriParseResult result = BitcoinUri::Parse(uri);
    QVERIFY(result.success);
    QVERIFY(result.has_label);
    QCOMPARE(result.label, QStringLiteral("Merchant"));
}

void BitcoinUriTests::parse_known_required_message()
{
    const QString uri = QStringLiteral("bitcoin:%1?req-message=Donation").arg(TEST_ADDRESS);
    const BitcoinUriParseResult result = BitcoinUri::Parse(uri);
    QVERIFY(result.success);
    QVERIFY(result.has_message);
    QCOMPARE(result.message, QStringLiteral("Donation"));
}

void BitcoinUriTests::parse_known_required_amount()
{
    const QString uri = QStringLiteral("bitcoin:%1?req-amount=0.5").arg(TEST_ADDRESS);
    const BitcoinUriParseResult result = BitcoinUri::Parse(uri);
    QVERIFY(result.success);
    QVERIFY(result.has_amount);
    QCOMPARE(result.amount_sats, CAmount{50000000}); // 0.5 BTC
}

// ---------------------------------------------------------------------------
// Percent-decoding behaviour
// ---------------------------------------------------------------------------

void BitcoinUriTests::parse_decodes_percent_encoded_label()
{
    // gui-qml uses QUrl::FullyDecoded, so percent-encoded sequences in label
    // and message are decoded before use. This is more correct per BIP21 /
    // RFC 3986 than Bitcoin Core Qt, which leaves them encoded (documented
    // there with the comment "Escape sequences are not supported").
    const QString uri = QStringLiteral("bitcoin:%1?label=%3F%26test&message=hello%20world")
        .arg(TEST_ADDRESS);
    const BitcoinUriParseResult result = BitcoinUri::Parse(uri);
    QVERIFY(result.success);
    QCOMPARE(result.label,   QStringLiteral("?&test"));
    QCOMPARE(result.message, QStringLiteral("hello world"));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(BitcoinUriTests)
#else
QTEST_MAIN(BitcoinUriTests)
#endif
#include "test_bitcoinuri.moc"
