// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <qml/models/walletlistmodel.h>
#include <scheduler.h>
#include <test/mocks/mocknode.h>
#include <util/translation.h>

#include <gmock/gmock.h>

namespace {
class FakeWalletLoader : public interfaces::WalletLoader
{
public:
    std::vector<std::pair<std::string, std::string>> wallet_dir_entries;

    void registerRpcs() override {}
    bool verify() override { return true; }
    bool load() override { return true; }
    void start(CScheduler&) override {}
    void stop() override {}
    void setMockTime(int64_t) override {}
    void schedulerMockForward(std::chrono::seconds) override {}
    util::Result<std::unique_ptr<interfaces::Wallet>> createWallet(const std::string&, const SecureString&, uint64_t, std::vector<bilingual_str>&) override
    {
        return util::Error{Untranslated("Unexpected createWallet call")};
    }
    util::Result<std::unique_ptr<interfaces::Wallet>> loadWallet(const std::string&, std::vector<bilingual_str>&) override
    {
        return util::Error{Untranslated("Unexpected loadWallet call")};
    }
    std::string getWalletDir() override { return {}; }
    util::Result<std::unique_ptr<interfaces::Wallet>> restoreWallet(const fs::path&, const std::string&, std::vector<bilingual_str>&) override
    {
        return util::Error{Untranslated("Unexpected restoreWallet call")};
    }
    util::Result<interfaces::WalletMigrationResult> migrateWallet(const std::string&, const SecureString&) override
    {
        return util::Error{Untranslated("Unexpected migrateWallet call")};
    }
    bool isEncrypted(const std::string&) override { return false; }
    std::vector<std::pair<std::string, std::string>> listWalletDir() override
    {
        return wallet_dir_entries;
    }
    std::vector<std::unique_ptr<interfaces::Wallet>> getWallets() override { return {}; }
    std::unique_ptr<interfaces::Handler> handleLoadWallet(LoadWalletFn) override
    {
        return interfaces::MakeCleanupHandler([] {});
    }
};

void ExpectWalletLoader(MockNode& node, FakeWalletLoader& loader)
{
    using ::testing::AtLeast;
    using ::testing::ReturnRef;

    ON_CALL(node, walletLoader()).WillByDefault(ReturnRef(loader));
    EXPECT_CALL(node, walletLoader()).Times(AtLeast(1)).WillRepeatedly(ReturnRef(loader));
}
} // namespace

class WalletListModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void listWalletDirMapsNameAndLoadStateRoles();
    void setOpenWalletNamesUpdatesLoadStateRole();
};

void WalletListModelTests::listWalletDirMapsNameAndLoadStateRoles()
{
    using ::testing::StrictMock;

    StrictMock<MockNode> node;
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
        {"beta_wallet", "sqlite"},
    };
    ExpectWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.roleNames().value(WalletListModel::NameRole), QByteArray{"name"});
    QCOMPARE(model.roleNames().value(WalletListModel::LoadStateRole), QByteArray{"loadState"});

    const QModelIndex first = model.index(0, 0);
    const QModelIndex second = model.index(1, 0);
    QCOMPARE(model.data(first, WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(second, WalletListModel::NameRole).toString(), QString{"beta_wallet"});
    QCOMPARE(model.data(first, WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
    QCOMPARE(model.data(second, WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
}

void WalletListModelTests::setOpenWalletNamesUpdatesLoadStateRole()
{
    using ::testing::StrictMock;

    StrictMock<MockNode> node;
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
        {"beta_wallet", "sqlite"},
    };
    ExpectWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();

    QSignalSpy data_changed_spy(&model, &QAbstractItemModel::dataChanged);

    model.setOpenWalletNames({"beta_wallet"});

    QCOMPARE(data_changed_spy.count(), 1);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));

    model.setOpenWalletNames({});

    QCOMPARE(data_changed_spy.count(), 2);
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
}

int RunWalletListModelTests(int argc, char* argv[])
{
    WalletListModelTests tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_walletlistmodel.moc"
