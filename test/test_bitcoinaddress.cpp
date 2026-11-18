// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/models/bitcoinaddress.h>

class BitcoinAddressTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void longLeadingWhitespaceIsNormalizedImmediatelyWithoutTruncation_data() {
        QTest::addColumn<int>("spaces");
        QTest::newRow("10k") << 10'000;
        QTest::newRow("1M") << 1'000'000;
    }
    void longLeadingWhitespaceIsNormalizedImmediatelyWithoutTruncation() {
        QFETCH(int, spaces);
        BitcoinAddress address;
        QSignalSpy changed{&address, &BitcoinAddress::addressChanged};
        const QString text = QString(spaces, QLatin1Char{' '}) + "bc1qexampleaddress";
        const auto cursor = address.setAddress(text, static_cast<int>(text.size()));
        QCOMPARE(address.address(), QStringLiteral("bc1qexampleaddress"));
        QCOMPARE(cursor, address.formattedAddress().size());
        QCOMPARE(address.normalizedCursor(), cursor);
        QCOMPARE(changed.size(), 1);
    }
    void onlyTheExistingFirstNinetyValidCharactersMatterForCursorMapping() {
        BitcoinAddress address;
        const QString accepted(90, QLatin1Char{'a'});
        const QString text = accepted + QString(100'000, QLatin1Char{' '});
        const auto cursor = address.setAddress(text, text.size());
        QCOMPARE(address.address(), accepted); QCOMPARE(cursor, address.formattedAddress().size());
    }
    void laterEditReplacesAnEarlierLongEdit() {
        BitcoinAddress address;
        address.setAddress(QString(10'000, QLatin1Char{' '}) + "original", 10'008);
        QCOMPARE(address.address(), QStringLiteral("original"));
        address.setAddress("successor", 9);
        QCOMPARE(address.address(), QStringLiteral("successor"));
    }
    void pasteReplacesTheSelectedRangeAndReturnsTheCursor() {
        BitcoinAddress address{"abcd1234"};
        const QString pasted = QString(10'000, QLatin1Char{' '}) + "xy";
        // Formatted "abcd 1234": replace "1234" (characters 5 to 9) with the paste.
        QCOMPARE(address.pasteAddress(pasted, 5, 9), 7);
        QCOMPARE(address.address(), QStringLiteral("abcdxy")); QCOMPARE(address.normalizedCursor(), 7);
    }
    void setAddress_acceptsSupportedAddressCharacters();
    void setAddress_rejectsUnsupportedCharactersImmediately();
    void formattedAddress_groupsCharactersInFours();
    void ellipsesAddress_keepsVisiblePrefixAndSuffix();
};

void BitcoinAddressTests::setAddress_acceptsSupportedAddressCharacters()
{
    const QString input{QStringLiteral("0123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")};
    BitcoinAddress address;

    address.setAddress(input, static_cast<int>(input.size()));

    QCOMPARE(address.address(), input);
}

void BitcoinAddressTests::setAddress_rejectsUnsupportedCharactersImmediately()
{
    BitcoinAddress address{QStringLiteral("bc1qexampleaddress")};
    QSignalSpy formatted_address_changed{&address, &BitcoinAddress::formattedAddressChanged};
    const QString input{address.formattedAddress() + QStringLiteral(".[]IO")};

    const int cursor_position{address.setAddress(input, static_cast<int>(input.size()))};

    QCOMPARE(address.address(), QStringLiteral("bc1qexampleaddress"));
    QCOMPARE(address.formattedAddress(), QStringLiteral("bc1q exam plea ddre ss"));
    QCOMPARE(cursor_position, static_cast<int>(address.formattedAddress().size()));
    QCOMPARE(formatted_address_changed.count(), 1);
}

void BitcoinAddressTests::formattedAddress_groupsCharactersInFours()
{
    QCOMPARE(
        BitcoinAddress::formattedAddress("abcd1234efgh5678"),
        QString("abcd 1234 efgh 5678"));
}

void BitcoinAddressTests::ellipsesAddress_keepsVisiblePrefixAndSuffix()
{
    QCOMPARE(
        BitcoinAddress::ellipsesAddress("abcd1234efgh5678ijkl"),
        QString("abcd 1234 ... 5678 ijkl"));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(BitcoinAddressTests)
#else
QTEST_MAIN(BitcoinAddressTests)
#endif
#include <test_bitcoinaddress.moc>
