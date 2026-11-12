// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_util.h>

class BlocksonlyIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

public:
    explicit BlocksonlyIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void mempoolToolsFollowNodeCapabilities()
    {
        QTRY_VERIFY(!m_app.nodeModel().mempoolInformationAvailable());
        QVERIFY(qmltest::OpenSettings(m_app));
        QVERIFY(qmltest::WaitForVisible(m_app, "settingsView"));
        QVERIFY(!qmltest::Find(m_app, "settingsSidebar_mempool"));
        QVERIFY(qmltest::Find(m_app, "settingsSidebar_about"));
    }
};

std::unique_ptr<QObject> CreateBlocksonlyTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<BlocksonlyIntegrationTests>(app);
}

#include <test_blocksonly.moc>
