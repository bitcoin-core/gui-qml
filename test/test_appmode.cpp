// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <qml/appmode.h>
#include <QSignalSpy>
#include <QtGlobal>
#include <QtTest/QtTest>

class AppModeTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void walletCapabilityGuardsActivation()
    {
        AppMode mode{AppMode::DESKTOP, true};
#ifdef ENABLE_WALLET
        QVERIFY(mode.walletCompiled());
        QVERIFY(mode.walletEnabled());
#else
        QVERIFY(!mode.walletCompiled());
        QVERIFY(!mode.walletEnabled());
#endif
        QSignalSpy changed{&mode, &AppMode::walletEnabledChanged};
        mode.setWalletEnabled(false);
        QCOMPARE(changed.count(), mode.walletCompiled() ? 1 : 0);
        QVERIFY(!mode.walletEnabled());
        changed.clear();
        mode.setWalletEnabled(true);
        QCOMPARE(mode.walletEnabled(), mode.walletCompiled());
        QCOMPARE(changed.count(), mode.walletCompiled() ? 1 : 0);
        mode.setWalletEnabled(true);
        QCOMPARE(changed.count(), mode.walletCompiled() ? 1 : 0);
    }

    void animationsFollowTestEnvironment()
    {
        AppMode mode{AppMode::DESKTOP, true};
        // The property exists for QML and mirrors the test switch exactly.
        QCOMPARE(mode.animationsEnabled(), !qEnvironmentVariableIsSet("BITCOIN_QML_TEST_NO_ANIMATIONS"));
    }
};

#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(AppModeTests)
#include <test_appmode.moc>
