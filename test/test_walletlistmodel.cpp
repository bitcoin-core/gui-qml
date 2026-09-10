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
#include <wallet/types.h>

#include <QSettings>
#include <QLocale>

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
    util::Result<std::unique_ptr<interfaces::Wallet>> restoreWallet(const fs::path&, const std::string&, std::vector<bilingual_str>&, bool) override
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

void ConfigureExpectedWalletLoader(MockNode& node, FakeWalletLoader& loader)
{
    node.wallet_loader_fn = [&loader]() -> interfaces::WalletLoader& { return loader; };
    node.ExpectAtLeast(node.calls.walletLoader, 1);
}
} // namespace

class WalletListModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void listWalletDirMapsNameAndLoadStateRoles();
    void listWalletDirRemovesMissingEntries();
    void listWalletDirSortsCaseInsensitivelyAndPreservesDuplicateRows();
    void displayNameRoleUsesStoredAlias();
    void setWalletLoadStateUpdatesLoadStateRole();
    void setWalletLoadStateSortsLoadedRowsFirst();
    void setWalletLoadStateBeforeListWalletDirSeedsInitialRows();
    void setWalletLoadStateAddsNewLoadedWalletAfterInitialList();
    void setWalletLoadStateRemovesOpenOnlyWalletOnUnload();
    void setWalletLoadStateLoadingExposesLoadingRoleAndClearsOnOpen();
    void setWalletLoadStateLoadErrorExposesErrorMessageRoleAndClearsOnClosed();
    void setWalletInfoUpdatesBalanceAndKeySchemeRolesForRowOnly();
    void listWalletDirPreservesBalanceAndKeySchemeAcrossRebuilds();
    void walletDirLoadedFlipsAfterFirstList();
    void balancesFollowDisplayUnitAcrossWalletRefreshes();
};

void WalletListModelTests::init()
{
    QSettings settings;
    settings.remove("walletDisplayNames");
    settings.sync();
}

void WalletListModelTests::listWalletDirMapsNameAndLoadStateRoles()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
        {"beta_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

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

void WalletListModelTests::listWalletDirRemovesMissingEntries()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
        {"beta_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();
    QCOMPARE(model.rowCount(), 2);

    loader.wallet_dir_entries = {
        {"beta_wallet", "sqlite"},
    };
    model.listWalletDir();

    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"beta_wallet"});
}

void WalletListModelTests::listWalletDirSortsCaseInsensitivelyAndPreservesDuplicateRows()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"zulu_wallet", "sqlite"},
        {"alpha_wallet", "sqlite"},
        {"Alpha_wallet", "sqlite"},
        {"alpha_wallet", "bdb"},
        {"bravo_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();

    QCOMPARE(model.rowCount(), 5);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"Alpha_wallet"});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::FormatRole).toString(), QString{"sqlite"});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::FormatRole).toString(), QString{"bdb"});
    QCOMPARE(model.data(model.index(2, 0), WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(model.index(2, 0), WalletListModel::FormatRole).toString(), QString{"sqlite"});
    QCOMPARE(model.data(model.index(3, 0), WalletListModel::NameRole).toString(), QString{"bravo_wallet"});
    QCOMPARE(model.data(model.index(4, 0), WalletListModel::NameRole).toString(), QString{"zulu_wallet"});
}

void WalletListModelTests::displayNameRoleUsesStoredAlias()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    QSettings settings;
    settings.setValue("walletDisplayNames/alpha_wallet", "Personal");
    settings.sync();

    WalletListModel model{node, nullptr};
    model.listWalletDir();

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::DisplayNameRole).toString(), QString{"Personal"});
}

void WalletListModelTests::setWalletLoadStateUpdatesLoadStateRole()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
        {"beta_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();

    QSignalSpy data_changed_spy(&model, &QAbstractItemModel::dataChanged);

    model.setWalletLoadState("alpha_wallet", WalletListModel::LoadState::Open);

    QCOMPARE(data_changed_spy.count(), 1);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::NameRole).toString(), QString{"beta_wallet"});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));

    model.setWalletLoadState("alpha_wallet", WalletListModel::LoadState::Closed);

    QCOMPARE(data_changed_spy.count(), 2);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
}

void WalletListModelTests::setWalletLoadStateSortsLoadedRowsFirst()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"zulu_wallet", "sqlite"},
        {"alpha_wallet", "sqlite"},
        {"bravo_wallet", "sqlite"},
        {"charlie_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::NameRole).toString(), QString{"bravo_wallet"});
    QCOMPARE(model.data(model.index(2, 0), WalletListModel::NameRole).toString(), QString{"charlie_wallet"});
    QCOMPARE(model.data(model.index(3, 0), WalletListModel::NameRole).toString(), QString{"zulu_wallet"});

    QSignalSpy model_reset_spy(&model, &QAbstractItemModel::modelReset);

    model.setWalletLoadState("zulu_wallet", WalletListModel::LoadState::Open);
    model.setWalletLoadState("bravo_wallet", WalletListModel::LoadState::Open);

    QCOMPARE(model_reset_spy.count(), 2);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"bravo_wallet"});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::NameRole).toString(), QString{"zulu_wallet"});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));
    QCOMPARE(model.data(model.index(2, 0), WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(model.index(2, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
    QCOMPARE(model.data(model.index(3, 0), WalletListModel::NameRole).toString(), QString{"charlie_wallet"});
    QCOMPARE(model.data(model.index(3, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
}

void WalletListModelTests::setWalletLoadStateBeforeListWalletDirSeedsInitialRows()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
        {"beta_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.setWalletLoadState("beta_wallet", WalletListModel::LoadState::Open);
    QCOMPARE(model.rowCount(), 0);

    model.listWalletDir();

    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"beta_wallet"});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
}

void WalletListModelTests::setWalletLoadStateAddsNewLoadedWalletAfterInitialList()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();

    QSignalSpy model_reset_spy(&model, &QAbstractItemModel::modelReset);
    model.setWalletLoadState("created_wallet", WalletListModel::LoadState::Open);

    QCOMPARE(model_reset_spy.count(), 1);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"created_wallet"});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::FormatRole).toString(), QString{});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));
}

void WalletListModelTests::setWalletLoadStateRemovesOpenOnlyWalletOnUnload()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();
    QCOMPARE(model.rowCount(), 0);

    QSignalSpy wallet_list_changed_spy(&model, &WalletListModel::walletListChanged);
    QSignalSpy model_reset_spy(&model, &QAbstractItemModel::modelReset);

    model.setWalletLoadState("external_wallet", WalletListModel::LoadState::Open);
    QCOMPARE(model_reset_spy.count(), 1);
    QCOMPARE(wallet_list_changed_spy.count(), 1);
    QCOMPARE(wallet_list_changed_spy.at(0).at(0).toBool(), true);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"external_wallet"});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::FormatRole).toString(), QString{});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));

    model.setWalletLoadState("external_wallet", WalletListModel::LoadState::Closed);
    QCOMPARE(model_reset_spy.count(), 2);
    QCOMPARE(wallet_list_changed_spy.count(), 2);
    QCOMPARE(wallet_list_changed_spy.at(1).at(0).toBool(), false);
    QCOMPARE(model.rowCount(), 0);
}

void WalletListModelTests::setWalletLoadStateLoadingExposesLoadingRoleAndClearsOnOpen()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
        {"beta_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();

    model.setWalletLoadState("alpha_wallet", WalletListModel::LoadState::Loading);

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(),
             static_cast<int>(WalletListModel::LoadState::Loading));
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::ErrorMessageRole).toString(), QString{});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::LoadStateRole).toInt(),
             static_cast<int>(WalletListModel::LoadState::Closed));

    model.setWalletLoadState("alpha_wallet", WalletListModel::LoadState::Open);

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(),
             static_cast<int>(WalletListModel::LoadState::Open));
}

void WalletListModelTests::setWalletLoadStateLoadErrorExposesErrorMessageRoleAndClearsOnClosed()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();

    model.setWalletLoadState("alpha_wallet",
                             WalletListModel::LoadState::LoadError,
                             "Disk is full");

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(),
             static_cast<int>(WalletListModel::LoadState::LoadError));
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::ErrorMessageRole).toString(),
             QString{"Disk is full"});

    model.setWalletLoadState("alpha_wallet", WalletListModel::LoadState::Closed);

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(),
             static_cast<int>(WalletListModel::LoadState::Closed));
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::ErrorMessageRole).toString(), QString{});
}

void WalletListModelTests::setWalletInfoUpdatesBalanceAndKeySchemeRolesForRowOnly()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
        {"beta_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();

    QSignalSpy data_changed_spy(&model, &QAbstractItemModel::dataChanged);
    model.setWalletInfo("alpha_wallet", 167930, /*keySchemeKind=*/2);

    QCOMPARE(data_changed_spy.count(), 1);
    const QList<QVariant> args = data_changed_spy.takeFirst();
    const QModelIndex top_left = args.at(0).value<QModelIndex>();
    const QModelIndex bottom_right = args.at(1).value<QModelIndex>();
    QCOMPARE(top_left.row(), 0);
    QCOMPARE(bottom_right.row(), 0);
    const QVector<int> roles = args.at(2).value<QVector<int>>();
    QVERIFY(roles.contains(WalletListModel::BalanceRole));
    QVERIFY(roles.contains(WalletListModel::KeySchemeKindRole));

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::BalanceRole).toString(), QString{"0.00167930"});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::KeySchemeKindRole).toInt(), 2);
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::BalanceRole).toString(), QString{});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::KeySchemeKindRole).toInt(), 0);

    // No-op when nothing changed.
    model.setWalletInfo("alpha_wallet", 167930, /*keySchemeKind=*/2);
    QCOMPARE(data_changed_spy.count(), 0);
}

void WalletListModelTests::listWalletDirPreservesBalanceAndKeySchemeAcrossRebuilds()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
    };
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    model.listWalletDir();
    model.setWalletInfo("alpha_wallet", 123000000, /*keySchemeKind=*/1);

    // Rebuild with the same wallet still present plus a new one.
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
        {"beta_wallet", "sqlite"},
    };
    model.listWalletDir();

    const int alpha_row = model.data(model.index(0, 0), WalletListModel::NameRole).toString() == "alpha_wallet" ? 0 : 1;
    QCOMPARE(model.data(model.index(alpha_row, 0), WalletListModel::BalanceRole).toString(), QString{"1.23000000"});
    QCOMPARE(model.data(model.index(alpha_row, 0), WalletListModel::KeySchemeKindRole).toInt(), 1);

    const int beta_row = 1 - alpha_row;
    QCOMPARE(model.data(model.index(beta_row, 0), WalletListModel::BalanceRole).toString(), QString{});
    QCOMPARE(model.data(model.index(beta_row, 0), WalletListModel::KeySchemeKindRole).toInt(), 0);
}

void WalletListModelTests::balancesFollowDisplayUnitAcrossWalletRefreshes()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"Charlie", "sqlite"}, {"Other", "sqlite"}};
    ConfigureExpectedWalletLoader(node, loader);
    WalletListModel model{node};
    model.listWalletDir();
    model.setWalletInfo("Charlie", 13900000000LL, 0);
    model.setWalletInfo("Other", 1, 0);
    const auto amount = [&model](int row) {
        return model.data(model.index(row, 0), WalletListModel::BalanceRole).toString().remove(QLocale().groupSeparator());
    };
    QCOMPARE(amount(0), QStringLiteral("139.00000000"));
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.setDisplayUnit(3);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.at(0).at(1).value<QModelIndex>().row(), 1);
    QCOMPARE(amount(0), QStringLiteral("13900000000"));
    QCOMPARE(amount(1), QStringLiteral("1"));
    // Switching wallets republishes metadata and reopening the selector rebuilds rows.
    model.setWalletInfo("Charlie", 13900000000LL, 0);
    model.listWalletDir();
    QCOMPARE(amount(0), QStringLiteral("13900000000"));
    model.setDisplayUnit(1);
    QCOMPARE(amount(0), QStringLiteral("139000.00000"));
    model.setDisplayUnit(2);
    QCOMPARE(amount(0), QStringLiteral("139000000.00"));
    model.setDisplayUnit(0);
    QCOMPARE(amount(0), QStringLiteral("139.00000000"));
    QCOMPARE(amount(1), QStringLiteral("0.00000001"));
}

void WalletListModelTests::walletDirLoadedFlipsAfterFirstList()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedWalletLoader(node, loader);

    WalletListModel model{node, nullptr};
    QSignalSpy wallet_dir_loaded_spy(&model, &WalletListModel::walletDirLoadedChanged);

    QCOMPARE(model.walletDirLoaded(), false);
    model.listWalletDir();

    QCOMPARE(model.walletDirLoaded(), true);
    QCOMPARE(wallet_dir_loaded_spy.count(), 1);

    model.listWalletDir();
    QCOMPARE(wallet_dir_loaded_spy.count(), 1);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(WalletListModelTests)
#else
QTEST_MAIN(WalletListModelTests)
#endif

#include "test_walletlistmodel.moc"
