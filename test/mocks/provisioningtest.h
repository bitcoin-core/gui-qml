// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_TEST_MOCKS_PROVISIONINGTEST_H
#define BITCOIN_QML_TEST_MOCKS_PROVISIONINGTEST_H
#include <qml/walletqmlcontroller.h>
#include <QtTest/QtTest>
#include <memory>
inline QString AwaitWalletNameCheck(WalletQmlController& controller, const QString& name)
{
    std::unique_ptr<WalletProvisioningCheck> check{controller.createProvisioningCheck()};
    if (!check) { QTest::qFail("Wallet name check was not admitted", __FILE__, __LINE__); return QStringLiteral("Check unavailable"); }
    check->setName(name);
    if (!QTest::qWaitFor([&] { return !check->pending(); }, 5000) || !check->ready()) {
        QTest::qFail("Wallet name check did not produce a matching outcome", __FILE__, __LINE__); return QStringLiteral("Check did not complete");
    }
    return check->nameError();
}
inline bool AwaitWalletPathCheck(WalletQmlController& controller, const QString& path)
{
    std::unique_ptr<WalletProvisioningCheck> check{controller.createProvisioningCheck()};
    if (!check) { QTest::qFail("Wallet path check was not admitted", __FILE__, __LINE__); return false; }
    check->setPath(path);
    if (!QTest::qWaitFor([&] { return !check->pending(); }, 5000) || !check->ready()) {
        QTest::qFail("Wallet path check did not produce a matching outcome", __FILE__, __LINE__); return false;
    }
    return check->pathExists();
}
#endif // BITCOIN_QML_TEST_MOCKS_PROVISIONINGTEST_H
