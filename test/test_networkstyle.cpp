// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/guiconstants.h>
#include <qml/networkstyle.h>

class NetworkStyleTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void instantiate_knownNetworks_haveExpectedNames();
    void getTitleAddText_perNetwork();
};

void NetworkStyleTests::instantiate_knownNetworks_haveExpectedNames()
{
    const NetworkStyle* mainnet_style = NetworkStyle::instantiate(ChainType::MAIN);
    QVERIFY(mainnet_style != nullptr);
    QCOMPARE(mainnet_style->getAppName(), QString(QAPP_APP_NAME_DEFAULT));
    delete mainnet_style;

    const NetworkStyle* signet_style = NetworkStyle::instantiate(ChainType::SIGNET);
    QVERIFY(signet_style != nullptr);
    QCOMPARE(signet_style->getAppName(), QString(QAPP_APP_NAME_SIGNET));
    delete signet_style;

    const NetworkStyle* regtest_style = NetworkStyle::instantiate(ChainType::REGTEST);
    QVERIFY(regtest_style != nullptr);
    QCOMPARE(regtest_style->getAppName(), QString(QAPP_APP_NAME_REGTEST));
    delete regtest_style;
}

void NetworkStyleTests::getTitleAddText_perNetwork()
{
    // getTitleAddText() supplies the per-network suffix of the tray tooltip and
    // window title (e.g. "Bitcoin Core client [regtest]"). Mainnet has none.
    const struct {
        ChainType net;
        QString expected;
    } cases[] = {
        {ChainType::MAIN, QString()},
        {ChainType::TESTNET, QStringLiteral("[test]")},
        {ChainType::TESTNET4, QStringLiteral("[testnet4]")},
        {ChainType::SIGNET, QStringLiteral("[signet]")},
        {ChainType::REGTEST, QStringLiteral("[regtest]")},
    };
    for (const auto& c : cases) {
        const NetworkStyle* style = NetworkStyle::instantiate(c.net);
        QVERIFY(style != nullptr);
        QCOMPARE(style->getTitleAddText(), c.expected);
        delete style;
    }
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(NetworkStyleTests)
#else
QTEST_MAIN(NetworkStyleTests)
#endif
#include <test_networkstyle.moc>
