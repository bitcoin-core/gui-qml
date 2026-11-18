// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <qml/models/walletlistmodel.h>
#include <qml/models/walletqmlmodel.h>
#include <scheduler.h>
#include <test/mocks/mocknode.h>
#include <util/translation.h>
#include <wallet/types.h>

#include <QSettings>
#include <QLocale>
#include <QThread>
#include <test/mocks/manualexecutionenvironment.h>
#include <atomic>
#include <deque>

namespace {
class FakeWalletLoader : public interfaces::WalletLoader
{
public:
    std::vector<std::pair<std::string, std::string>> wallet_dir_entries;
    std::atomic<int> reads{0};
    std::atomic<QThread*> read_thread{nullptr};
    bool fail_read{false};

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
    util::Result<interfaces::WalletMigrationResult> migrateWallet(const std::string&, const SecureString&, bool) override
    {
        return util::Error{Untranslated("Unexpected migrateWallet call")};
    }
    bool isEncrypted(const std::string&) override { return false; }
    std::vector<std::pair<std::string, std::string>> listWalletDir() override
    {
        ++reads; read_thread.store(QThread::currentThread());
        if (fail_read) throw std::runtime_error("directory failed");
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
bool AwaitCatalog(WalletListModel& model)
{
    return QTest::qWaitFor([&] { return !model.projectionPending(); }, 5000) && model.readError().isEmpty();
}
struct CatalogExecution {
    ManualRoutedExecution work;
    std::deque<std::function<void()>> publications;
    ~CatalogExecution() { work.scheduler->close(); settle(); }
    execution::SnapshotServices services()
    {
        return {work.scheduler, [this](auto turn) { publications.push_back(std::move(turn)); }};
    }
    bool publishNext()
    {
        if (publications.empty()) return false;
        auto turn = std::move(publications.front()); publications.pop_front(); turn(); return true;
    }
    void settle()
    {
        do { work.drain(); while (publishNext()) {} } while (!work.transportIdle());
    }
};
} // namespace

class WalletListModelTests : public QObject
{
    Q_OBJECT

private:
    QLocale m_previous_locale;

private Q_SLOTS:
    void init();
    void cleanup();
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
    void balancesUseGermanLocale();
    void manualCatalogOwnsPhasesAndRetainsPriorRows_data();
    void manualCatalogOwnsPhasesAndRetainsPriorRows();
    void manualRefreshBurstReadsDirectoryOnce();
    void manualRejectedDiscoveryRetainsOrderedInputsForRetry();
    void manualRefreshDuringOwnedReadRetainsEvents();
    void manualReaderBackendIngressDuringGetterPreservesNewWork();
    void manualFailedDiscoveryRetainsStaleRowsAndRecovers();
    void manualBackendAvailabilityPreventsDiscovery_data();
    void manualBackendAvailabilityPreventsDiscovery();
    void manualBackendAvailabilityDoesNotChangeRowLoadState();
    void manualDefaultWalletHasExplicitLoadingAndFailureAbsence();
    void manualDefaultWalletObservationBeforeDiscovery();
    void manualOlderFailureCannotReplaceNewerDiscovery();
    void manualStopFencesReadAndPublication_data();
    void manualStopFencesReadAndPublication();
    void manualInterleavedPublicationRequiresExactDiffBase();
    void exactFacadeRoleSurvivesOldFacadeDestruction();
};

void WalletListModelTests::init()
{
    m_previous_locale = QLocale();
    // Use decimal points unless a test explicitly selects another locale.
    QLocale::setDefault(QLocale::c());
    QSettings settings;
    settings.remove("walletDisplayNames");
    settings.sync();
}

void WalletListModelTests::cleanup()
{
    QLocale::setDefault(m_previous_locale);
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
    QVERIFY(AwaitCatalog(model));

    QVERIFY(loader.read_thread.load() != QThread::currentThread());
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
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(model.rowCount(), 2);

    loader.wallet_dir_entries = {
        {"beta_wallet", "sqlite"},
    };
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));

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
    QVERIFY(AwaitCatalog(model));

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
    QVERIFY(AwaitCatalog(model));

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
    QVERIFY(AwaitCatalog(model));

    QSignalSpy data_changed_spy(&model, &QAbstractItemModel::dataChanged);

    model.setWalletLoadState("alpha_wallet", WalletListModel::LoadState::Open);
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(data_changed_spy.count(), 1);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::NameRole).toString(), QString{"beta_wallet"});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));

    model.setWalletLoadState("alpha_wallet", WalletListModel::LoadState::Closed);
    QVERIFY(AwaitCatalog(model));

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
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"alpha_wallet"});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::NameRole).toString(), QString{"bravo_wallet"});
    QCOMPARE(model.data(model.index(2, 0), WalletListModel::NameRole).toString(), QString{"charlie_wallet"});
    QCOMPARE(model.data(model.index(3, 0), WalletListModel::NameRole).toString(), QString{"zulu_wallet"});

    QSignalSpy model_reset_spy(&model, &QAbstractItemModel::modelReset);

    model.setWalletLoadState("zulu_wallet", WalletListModel::LoadState::Open);
    QVERIFY(AwaitCatalog(model));
    model.setWalletLoadState("bravo_wallet", WalletListModel::LoadState::Open);
    QVERIFY(AwaitCatalog(model));

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
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(model.rowCount(), 0);

    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));

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
    QVERIFY(AwaitCatalog(model));

    QSignalSpy model_reset_spy(&model, &QAbstractItemModel::modelReset);
    model.setWalletLoadState("created_wallet", WalletListModel::LoadState::Open);
    QVERIFY(AwaitCatalog(model));

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
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(model.rowCount(), 0);

    QSignalSpy wallet_list_changed_spy(&model, &WalletListModel::walletListChanged);
    QSignalSpy model_reset_spy(&model, &QAbstractItemModel::modelReset);

    model.setWalletLoadState("external_wallet", WalletListModel::LoadState::Open);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(model_reset_spy.count(), 1);
    QCOMPARE(wallet_list_changed_spy.count(), 1);
    QCOMPARE(wallet_list_changed_spy.at(0).at(0).toBool(), true);
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::NameRole).toString(), QString{"external_wallet"});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::FormatRole).toString(), QString{});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));

    model.setWalletLoadState("external_wallet", WalletListModel::LoadState::Closed);
    QVERIFY(AwaitCatalog(model));
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
    QVERIFY(AwaitCatalog(model));

    model.setWalletLoadState("alpha_wallet", WalletListModel::LoadState::Loading);
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(),
             static_cast<int>(WalletListModel::LoadState::Loading));
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::ErrorMessageRole).toString(), QString{});
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::LoadStateRole).toInt(),
             static_cast<int>(WalletListModel::LoadState::Closed));

    model.setWalletLoadState("alpha_wallet", WalletListModel::LoadState::Open);
    QVERIFY(AwaitCatalog(model));

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
    QVERIFY(AwaitCatalog(model));

    model.setWalletLoadState("alpha_wallet",
                             WalletListModel::LoadState::LoadError,
                             "Disk is full");
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::LoadStateRole).toInt(),
             static_cast<int>(WalletListModel::LoadState::LoadError));
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::ErrorMessageRole).toString(),
             QString{"Disk is full"});

    model.setWalletLoadState("alpha_wallet", WalletListModel::LoadState::Closed);
    QVERIFY(AwaitCatalog(model));

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
    QVERIFY(AwaitCatalog(model));

    QSignalSpy data_changed_spy(&model, &QAbstractItemModel::dataChanged);
    model.setWalletInfo("alpha_wallet", 167930, /*keySchemeKind=*/2);
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(data_changed_spy.count(), 1);
    const QList<QVariant> args = data_changed_spy.takeFirst();
    const QModelIndex top_left = args.at(0).value<QModelIndex>();
    const QModelIndex bottom_right = args.at(1).value<QModelIndex>();
    QCOMPARE(top_left.row(), 0);
    QCOMPARE(bottom_right.row(), 0);
    const QVector<int> roles = args.at(2).value<QVector<int>>();
    QVERIFY(roles.contains(WalletListModel::BalanceRole));
    QVERIFY(roles.contains(WalletListModel::BalanceKnownRole));
    QVERIFY(roles.contains(WalletListModel::KeySchemeKindRole));

    QCOMPARE(model.data(model.index(0, 0), WalletListModel::BalanceRole).toString(), QString{"0.00167930"});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::KeySchemeKindRole).toInt(), 2);
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::BalanceRole).toString(), QString{});
    QVERIFY(!model.data(model.index(1, 0), WalletListModel::BalanceKnownRole).toBool());
    QVERIFY(model.data(model.index(0, 0), WalletListModel::BalanceKnownRole).toBool());
    QCOMPARE(model.data(model.index(1, 0), WalletListModel::KeySchemeKindRole).toInt(), -1);

    // No-op when nothing changed.
    model.setWalletInfo("alpha_wallet", 167930, /*keySchemeKind=*/2);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(data_changed_spy.count(), 0);
    // Opening metadata can arrive before its first observed balance. Preserve
    // the key type while distinguishing absence from a known zero.
    model.setWalletInfo("alpha_wallet", 0, /*keySchemeKind=*/2, /*balanceKnown=*/false);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(data_changed_spy.count(), 1);
    QVERIFY(!model.data(model.index(0, 0), WalletListModel::BalanceKnownRole).toBool());
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::BalanceRole).toString(), QString{});
    QCOMPARE(model.data(model.index(0, 0), WalletListModel::KeySchemeKindRole).toInt(), 2);
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
    QVERIFY(AwaitCatalog(model));
    model.setWalletInfo("alpha_wallet", 123000000, /*keySchemeKind=*/1);
    QVERIFY(AwaitCatalog(model));

    // Rebuild with the same wallet still present plus a new one.
    loader.wallet_dir_entries = {
        {"alpha_wallet", "sqlite"},
        {"beta_wallet", "sqlite"},
    };
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));

    const int alpha_row = model.data(model.index(0, 0), WalletListModel::NameRole).toString() == "alpha_wallet" ? 0 : 1;
    QCOMPARE(model.data(model.index(alpha_row, 0), WalletListModel::BalanceRole).toString(), QString{"1.23000000"});
    QCOMPARE(model.data(model.index(alpha_row, 0), WalletListModel::KeySchemeKindRole).toInt(), 1);

    const int beta_row = 1 - alpha_row;
    QCOMPARE(model.data(model.index(beta_row, 0), WalletListModel::BalanceRole).toString(), QString{});
    QCOMPARE(model.data(model.index(beta_row, 0), WalletListModel::KeySchemeKindRole).toInt(), -1);
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
    QVERIFY(AwaitCatalog(model));
    model.setWalletInfo("Charlie", 13900000000LL, 0);
    QVERIFY(AwaitCatalog(model));
    model.setWalletInfo("Other", 1, 0);
    QVERIFY(AwaitCatalog(model));
    const auto amount = [&model](int row) {
        return model.data(model.index(row, 0), WalletListModel::BalanceRole).toString().remove(QLocale().groupSeparator());
    };
    QCOMPARE(amount(0), QStringLiteral("139.00000000"));
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.setDisplayUnit(3);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(changed.count(), 2);
    QCOMPARE(changed.at(0).at(0).value<QModelIndex>().row(), 0);
    QCOMPARE(changed.at(1).at(0).value<QModelIndex>().row(), 1);
    QCOMPARE(amount(0), QStringLiteral("13900000000"));
    QCOMPARE(amount(1), QStringLiteral("1"));
    // Switching wallets republishes metadata and reopening the selector rebuilds rows.
    model.setWalletInfo("Charlie", 13900000000LL, 0);
    QVERIFY(AwaitCatalog(model));
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(amount(0), QStringLiteral("13900000000"));
    model.setDisplayUnit(1);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(amount(0), QStringLiteral("139000.00000"));
    model.setDisplayUnit(2);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(amount(0), QStringLiteral("139000000.00"));
    model.setDisplayUnit(0);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(amount(0), QStringLiteral("139.00000000"));
    QCOMPARE(amount(1), QStringLiteral("0.00000001"));
}

void WalletListModelTests::balancesUseGermanLocale()
{
    QLocale::setDefault(QLocale{"de_DE"});
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha_wallet", "sqlite"}};
    ConfigureExpectedWalletLoader(node, loader);
    WalletListModel model{node};
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));
    model.setWalletInfo("alpha_wallet", 123456789, 0);
    QVERIFY(AwaitCatalog(model));
    const auto amount = [&model] {
        return model.data(model.index(0, 0), WalletListModel::BalanceRole).toString();
    };

    QCOMPARE(amount(), QStringLiteral("1,23456789"));
    model.setDisplayUnit(1);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(amount(), QStringLiteral("1.234,56789"));
    model.setDisplayUnit(2);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(amount(), QStringLiteral("1.234.567,89"));
    model.setDisplayUnit(3);
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(amount(), QStringLiteral("123.456.789"));
    model.setDisplayUnit(0);
    QVERIFY(AwaitCatalog(model));
    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(amount(), QStringLiteral("1,23456789"));
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
    QVERIFY(AwaitCatalog(model));

    QCOMPARE(model.walletDirLoaded(), true);
    QCOMPARE(wallet_dir_loaded_spy.count(), 1);

    model.listWalletDir();
    QVERIFY(AwaitCatalog(model));
    QCOMPARE(wallet_dir_loaded_spy.count(), 1);
}

void WalletListModelTests::manualCatalogOwnsPhasesAndRetainsPriorRows_data()
{
    QTest::addColumn<int>("scenario");
    QTest::newRow("success") << 0;
    QTest::newRow("rejection") << 1;
    QTest::newRow("failure") << 2;
    QTest::newRow("quiet-close") << 3;
}
void WalletListModelTests::manualCatalogOwnsPhasesAndRetainsPriorRows()
{
    QFETCH(int, scenario);
    ManualExecutionEnvironment environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"beta", "sqlite"}, {"alpha", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, {environment.scheduler, [&](auto turn) { environment.post(std::move(turn)); }}};
    QSignalSpy reset{&model, &QAbstractItemModel::modelReset};
    QSignalSpy changed{&model, &QAbstractItemModel::dataChanged};
    model.listWalletDir(); QCOMPARE(loader.reads.load(), 0); QCOMPARE(model.rowCount(), 0);
    QVERIFY(environment.step()); QCOMPARE(loader.reads.load(), 1); QCOMPARE(model.rowCount(), 0);
    QVERIFY(environment.step()); QCOMPARE(model.rowCount(), 0); QVERIFY(model.projectionPending());
    QVERIFY(environment.publishNext()); QCOMPARE(model.rowCount(), 2); QVERIFY(!model.projectionPending());
    QCOMPARE(model.data(model.index(0), WalletListModel::NameRole).toString(), QStringLiteral("alpha"));
    const auto reads = loader.reads.load(); const auto resets = reset.size();
    const QPersistentModelIndex stable{model.index(0)};
    if (scenario == 1) environment.rejectNext(true);
    if (scenario == 2) loader.fail_read = true;
    if (scenario == 1 || scenario == 2) model.listWalletDir();
    else model.setWalletInfo("alpha", 123, 1);
    QCOMPARE(loader.reads.load(), reads);
    environment.drain();
    if (scenario == 3) {
        const auto notices = changed.size(); model.close();
        QCOMPARE(reset.size(), resets); QCOMPARE(changed.size(), notices);
        environment.publishAll(); QCOMPARE(reset.size(), resets); QCOMPARE(changed.size(), notices);
    } else {
        environment.publishAll();
        QCOMPARE(model.rowCount(), 2); QCOMPARE(reset.size(), resets); QVERIFY(stable.isValid());
        QCOMPARE(stable.data(WalletListModel::NameRole).toString(), QStringLiteral("alpha"));
        if (scenario == 0) { QCOMPARE(changed.size(), 1); QCOMPARE(stable.data(WalletListModel::BalanceSatoshiRole).toLongLong(), qint64{123}); }
        else { QVERIFY(!model.readError().isEmpty()); QCOMPARE(stable.data(WalletListModel::BalanceKnownRole).toBool(), false); }
    }
    model.close(); environment.drain(); environment.publishAll(); QVERIFY(environment.quiescent());
}


void WalletListModelTests::manualRefreshBurstReadsDirectoryOnce()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"beta", "sqlite"}, {"alpha", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.services()};
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Idle);
    QVERIFY(!model.fresh()); QVERIFY(!model.walletDirLoaded());
    for (int i = 0; i < 16; ++i) model.refresh();
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Scheduled);
    QVERIFY(model.projectionPending()); QCOMPARE(loader.reads.load(), 0);
    QCOMPARE(environment.work.queuedCountIf([](const auto& turn) { return turn.lane == execution::Lane::Core; }), 1);
    QVERIFY(environment.work.step(execution::Lane::Core));
    QCOMPARE(loader.reads.load(), 1); QCOMPARE(model.rowCount(), 0);
    QVERIFY(environment.publishNext());
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Applying);
    QVERIFY(model.projectionPending()); QVERIFY(!model.fresh()); QCOMPARE(model.rowCount(), 0);
    QVERIFY(environment.work.step(execution::Lane::Domain));
    QVERIFY(environment.publishNext());
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Current);
    QVERIFY(model.fresh()); QVERIFY(!model.projectionPending()); QCOMPARE(model.rowCount(), 2);
    model.setWalletLoadState("alpha", WalletListModel::LoadState::Open);
    model.setWalletInfo("alpha", 7, 1);
    model.setOperationId("alpha", "operation-7");
    model.setDisplayUnit(3); model.refreshDisplayNames();
    environment.settle();
    QCOMPARE(loader.reads.load(), 1);
    QVERIFY(!model.projectionPending()); QVERIFY(model.fresh());
    QCOMPARE(model.data(model.index(0), WalletListModel::BalanceSatoshiRole).toLongLong(), qint64{7});
    QCOMPARE(model.data(model.index(0), WalletListModel::OperationIdRole).toString(), QStringLiteral("operation-7"));
    model.close(); environment.settle(); QVERIFY(environment.work.quiescent());
}

void WalletListModelTests::manualRejectedDiscoveryRetainsOrderedInputsForRetry()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"named", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.services()};
    environment.work.rejectLane(execution::Lane::Core);
    model.refresh(); environment.settle();
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Failed);
    QVERIFY(!model.projectionPending()); QVERIFY(!model.fresh()); QVERIFY(!model.readError().isEmpty());
    QCOMPARE(loader.reads.load(), 0); QCOMPARE(model.rowCount(), 0); QVERIFY(environment.work.transportIdle());
    // A later ordinary registry observation retries the retained discovery;
    // there is no polling loop and no lost default-wallet identity or metadata.
    model.setWalletLoadState({}, WalletListModel::LoadState::Open);
    model.setWalletInfo({}, 21, 1);
    environment.settle();
    QCOMPARE(loader.reads.load(), 1); QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Current); QVERIFY(model.fresh()); QVERIFY(!model.projectionPending());
    QVERIFY(model.readError().isEmpty());
    QCOMPARE(model.data(model.index(0), WalletListModel::WalletIdRole).toString(), QString{});
    QCOMPARE(model.data(model.index(0), WalletListModel::LoadStateRole).toInt(), int(WalletListModel::LoadState::Open));
    QCOMPARE(model.data(model.index(0), WalletListModel::BalanceSatoshiRole).toLongLong(), qint64{21});
    model.close(); environment.settle(); QVERIFY(environment.work.quiescent());
}

void WalletListModelTests::manualRefreshDuringOwnedReadRetainsEvents()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.services()};
    model.refresh(); QVERIFY(environment.work.step(execution::Lane::Core));
    QCOMPARE(loader.reads.load(), 1);
    loader.wallet_dir_entries = {{"beta", "sqlite"}};
    for (int i = 0; i < 16; ++i) model.refresh();
    model.setWalletLoadState({}, WalletListModel::LoadState::Loading);
    model.setWalletLoadState({}, WalletListModel::LoadState::LoadError, "superseded failure");
    model.setWalletLoadState({}, WalletListModel::LoadState::Open);
    model.setWalletInfo({}, 42, 2);
    model.setOperationId({}, "default-opening");
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::ReadingWithPendingWork);
    QVERIFY(environment.work.step(execution::Lane::Domain));
    QVERIFY(environment.publishNext());
    QVERIFY(model.projectionPending()); QVERIFY(!model.fresh());
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Scheduled);
    environment.settle();
    QCOMPARE(loader.reads.load(), 2); QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), WalletListModel::WalletIdRole).toString(), QString{});
    QCOMPARE(model.data(model.index(0), WalletListModel::LoadStateRole).toInt(), int(WalletListModel::LoadState::Open));
    QCOMPARE(model.data(model.index(0), WalletListModel::RowErrorRole).toString(), QString{});
    QCOMPARE(model.data(model.index(0), WalletListModel::BalanceSatoshiRole).toLongLong(), qint64{42});
    QCOMPARE(model.data(model.index(0), WalletListModel::OperationIdRole).toString(), QStringLiteral("default-opening"));
    QCOMPARE(model.data(model.index(1), WalletListModel::WalletIdRole).toString(), QStringLiteral("beta"));
    model.setWalletLoadState({}, WalletListModel::LoadState::Closed); environment.settle();
    QCOMPARE(model.rowCount(), 1); QCOMPARE(loader.reads.load(), 2);
    model.close(); environment.settle(); QVERIFY(environment.work.quiescent());
}

void WalletListModelTests::manualReaderBackendIngressDuringGetterPreservesNewWork()
{
    CatalogExecution environment;
    std::weak_ptr<walletcatalog::Reader> weak;
    int reads{0};
    walletcatalog::Phase before{walletcatalog::Phase::Idle}, after{walletcatalog::Phase::Idle};
    auto reader = std::make_shared<walletcatalog::Reader>(environment.work.scheduler, [&] {
        ++reads;
        if (reads == 1) {
            const auto owner = weak.lock();
            before = owner->observation().phase;
            // Legal backend ingress into the non-QObject owner during an
            // admitted getter. No model or GUI object is touched here.
            for (int i = 0; i < 8; ++i) owner->request(walletcatalog::Input{walletcatalog::Input::Kind::Directory});
            walletcatalog::Input open{walletcatalog::Input::Kind::Load}; open.load = walletcatalog::LoadState::Open;
            owner->request(std::move(open));
            after = owner->observation().phase;
            return walletcatalog::Reader::Directory{{"alpha", "sqlite"}};
        }
        return walletcatalog::Reader::Directory{{"beta", "sqlite"}};
    }, [] {});
    weak = reader;
    reader->request(walletcatalog::Input{walletcatalog::Input::Kind::Directory});
    QVERIFY(environment.work.step(execution::Lane::Core));
    QCOMPARE(reads, 1); QCOMPARE(before, walletcatalog::Phase::Reading); QCOMPARE(after, walletcatalog::Phase::ReadingWithPendingWork);
    QVERIFY(environment.work.step(execution::Lane::Domain));
    {
        const auto update = reader->take();
        QCOMPARE(update.observation.phase, walletcatalog::Phase::Scheduled); QVERIFY(!update.observation.fresh);
        QVERIFY(update.snapshot); QCOMPARE(update.snapshot->rows.size(), size_t{1});
        QCOMPARE(update.snapshot->rows.front().name, QStringLiteral("alpha"));
        reader->published(update.snapshot);
    }
    environment.work.drain();
    {
        const auto update = reader->take();
        QCOMPARE(reads, 2); QCOMPARE(update.observation.phase, walletcatalog::Phase::Current); QVERIFY(update.observation.fresh);
        QVERIFY(update.snapshot); QCOMPARE(update.snapshot->rows.size(), size_t{2});
        QCOMPARE(update.snapshot->rows.front().name, QString{});
        QCOMPARE(update.snapshot->rows.front().load_state, walletcatalog::LoadState::Open);
        QCOMPARE(update.snapshot->rows.back().name, QStringLiteral("beta"));
    }
    reader->close(); environment.settle();
    QCOMPARE(reader->observation().phase, walletcatalog::Phase::Stopped); QVERIFY(environment.work.quiescent());
}

void WalletListModelTests::manualFailedDiscoveryRetainsStaleRowsAndRecovers()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.services()};
    model.refresh(); environment.settle();
    QSignalSpy reset{&model, &QAbstractItemModel::modelReset};
    const QPersistentModelIndex original{model.index(0)};
    loader.fail_read = true; model.refresh();
    QVERIFY(!model.fresh()); QVERIFY(model.projectionPending());
    environment.settle();
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Failed);
    QVERIFY(!model.fresh()); QVERIFY(!model.readError().isEmpty()); QVERIFY(!model.projectionPending());
    QVERIFY(model.walletDirLoaded()); QCOMPARE(model.rowCount(), 1); QCOMPARE(reset.size(), 0);
    QVERIFY(original.isValid()); QCOMPARE(original.data(WalletListModel::WalletIdRole).toString(), QStringLiteral("alpha"));
    const auto failure = model.readError();
    model.setWalletInfo("alpha", 9, 1); environment.settle();
    QCOMPARE(loader.reads.load(), 2); QCOMPARE(model.readError(), failure); QVERIFY(!model.fresh());
    QCOMPARE(original.data(WalletListModel::BalanceSatoshiRole).toLongLong(), qint64{9});
    loader.fail_read = false; loader.wallet_dir_entries = {{"beta", "sqlite"}};
    model.refresh(); environment.settle();
    QCOMPARE(loader.reads.load(), 3); QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Current);
    QVERIFY(model.fresh()); QVERIFY(model.readError().isEmpty()); QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0), WalletListModel::WalletIdRole).toString(), QStringLiteral("beta"));
    model.close(); environment.settle(); QVERIFY(environment.work.quiescent());
}

void WalletListModelTests::manualBackendAvailabilityPreventsDiscovery_data()
{
    QTest::addColumn<WalletListModel::BackendAvailability>("availability");
    QTest::newRow("uninitialized") << WalletListModel::BackendAvailability::Uninitialized;
    QTest::newRow("disabled") << WalletListModel::BackendAvailability::Disabled;
    QTest::newRow("unavailable") << WalletListModel::BackendAvailability::Unavailable;
}
void WalletListModelTests::manualBackendAvailabilityPreventsDiscovery()
{
    QFETCH(WalletListModel::BackendAvailability, availability);
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.services()};
    model.setBackendAvailability(availability); model.refresh(); environment.settle();
    QCOMPARE(model.backendAvailability(), availability); QCOMPARE(loader.reads.load(), 0);
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Idle);
    QVERIFY(!model.walletDirLoaded()); QVERIFY(!model.fresh()); QVERIFY(!model.projectionPending());
    QCOMPARE(model.rowCount(), 0); QVERIFY(model.readError().isEmpty());
    model.setBackendAvailability(WalletListModel::BackendAvailability::Available); environment.settle();
    QCOMPARE(loader.reads.load(), 1); QVERIFY(model.walletDirLoaded()); QVERIFY(model.fresh());
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Current); QCOMPARE(model.rowCount(), 0);
    model.close(); environment.settle(); QVERIFY(environment.work.quiescent());
}

void WalletListModelTests::manualBackendAvailabilityDoesNotChangeRowLoadState()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.services()};
    model.refresh(); model.setWalletLoadState({}, WalletListModel::LoadState::Open); environment.settle();
    model.setBackendAvailability(WalletListModel::BackendAvailability::Disabled); environment.settle();
    QCOMPARE(loader.reads.load(), 1); QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.backendAvailability(), WalletListModel::BackendAvailability::Disabled); QVERIFY(!model.fresh());
    QCOMPARE(model.data(model.index(0), WalletListModel::LoadStateRole).toInt(), int(WalletListModel::LoadState::Open));
    model.setBackendAvailability(WalletListModel::BackendAvailability::Available); environment.settle();
    QCOMPARE(loader.reads.load(), 2); QVERIFY(model.fresh());
    QCOMPARE(model.data(model.index(0), WalletListModel::LoadStateRole).toInt(), int(WalletListModel::LoadState::Open));
    model.close(); environment.settle(); QVERIFY(environment.work.quiescent());
}

void WalletListModelTests::manualDefaultWalletHasExplicitLoadingAndFailureAbsence()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"", "sqlite"}, {"named", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.services()};
    model.refresh(); environment.settle();
    const auto row = [&] { return model.index(0); };
    QCOMPARE(model.data(row(), WalletListModel::NameRole).toString(), QString{});
    QCOMPARE(model.data(row(), WalletListModel::WalletIdRole).toString(), QString{});
    QCOMPARE(model.data(row(), WalletListModel::LoadStateRole).toInt(), int(WalletListModel::LoadState::Closed));
    QVERIFY(!model.data(row(), WalletListModel::DisplayNameRole).toString().isEmpty());
    QVERIFY(model.data(row(), WalletListModel::RowErrorRole).toString().isEmpty());
    QVERIFY(model.data(row(), WalletListModel::OpenWalletRole).isValid());
    QCOMPARE(model.data(row(), WalletListModel::OpenWalletRole).value<WalletQmlModel*>(), nullptr);
    model.setWalletLoadState({}, WalletListModel::LoadState::Loading); environment.settle();
    QCOMPARE(model.data(row(), WalletListModel::LoadStateRole).toInt(), int(WalletListModel::LoadState::Loading));
    model.setWalletLoadState({}, WalletListModel::LoadState::LoadError, "default failed"); environment.settle();
    QCOMPARE(model.data(row(), WalletListModel::LoadStateRole).toInt(), int(WalletListModel::LoadState::LoadError));
    QCOMPARE(model.data(row(), WalletListModel::RowErrorRole).toString(), QStringLiteral("default failed"));
    model.setWalletLoadState({}, WalletListModel::LoadState::Closed); environment.settle();
    QCOMPARE(model.data(row(), WalletListModel::LoadStateRole).toInt(), int(WalletListModel::LoadState::Closed));
    QCOMPARE(model.data(row(), WalletListModel::RowErrorRole).toString(), QString{});
    model.setWalletLoadState({}, WalletListModel::LoadState::Open); model.setWalletInfo({}, 0, 2); environment.settle();
    QCOMPARE(model.data(row(), WalletListModel::LoadStateRole).toInt(), int(WalletListModel::LoadState::Open));
    QVERIFY(model.data(row(), WalletListModel::BalanceKnownRole).toBool());
    QCOMPARE(model.data(row(), WalletListModel::BalanceSatoshiRole).toLongLong(), qint64{0});
    QCOMPARE(model.data(row(), WalletListModel::KeySchemeKindRole).toInt(), 2);
    QCOMPARE(loader.reads.load(), 1);
    model.close(); environment.settle(); QVERIFY(environment.work.quiescent());
}

void WalletListModelTests::manualDefaultWalletObservationBeforeDiscovery()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"other", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.services()};
    model.setWalletLoadState({}, WalletListModel::LoadState::Open); model.setWalletInfo({}, 12, 1); environment.settle();
    QCOMPARE(model.rowCount(), 0); QCOMPARE(loader.reads.load(), 0); QVERIFY(!model.fresh());
    model.refresh(); environment.settle();
    QCOMPARE(model.rowCount(), 2); QCOMPARE(loader.reads.load(), 1);
    QCOMPARE(model.data(model.index(0), WalletListModel::WalletIdRole).toString(), QString{});
    QCOMPARE(model.data(model.index(0), WalletListModel::BalanceSatoshiRole).toLongLong(), qint64{12});
    model.setWalletLoadState({}, WalletListModel::LoadState::Closed); environment.settle();
    QCOMPARE(model.rowCount(), 1); QCOMPARE(loader.reads.load(), 1);
    QCOMPARE(model.data(model.index(0), WalletListModel::WalletIdRole).toString(), QStringLiteral("other"));
    model.close(); environment.settle(); QVERIFY(environment.work.quiescent());
}

void WalletListModelTests::manualOlderFailureCannotReplaceNewerDiscovery()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.services()};
    model.refresh(); environment.settle();
    loader.fail_read = true; model.refresh(); QVERIFY(environment.work.step(execution::Lane::Core));
    loader.fail_read = false; loader.wallet_dir_entries = {{"beta", "sqlite"}}; model.refresh();
    QVERIFY(environment.work.step(execution::Lane::Domain)); QVERIFY(environment.publishNext());
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Scheduled);
    QVERIFY(model.readError().isEmpty()); QVERIFY(model.projectionPending()); QVERIFY(!model.fresh());
    environment.settle(); QCOMPARE(loader.reads.load(), 3);
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Current); QVERIFY(model.fresh()); QVERIFY(model.readError().isEmpty());
    QCOMPARE(model.data(model.index(0), WalletListModel::WalletIdRole).toString(), QStringLiteral("beta"));
    model.close(); environment.settle(); QVERIFY(environment.work.quiescent());
}

void WalletListModelTests::manualStopFencesReadAndPublication_data()
{
    QTest::addColumn<bool>("read_finished");
    QTest::newRow("queued") << false;
    QTest::newRow("core-result-held") << true;
}
void WalletListModelTests::manualStopFencesReadAndPublication()
{
    QFETCH(bool, read_finished);
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.services()};
    QSignalSpy reset{&model, &QAbstractItemModel::modelReset};
    model.refresh();
    if (read_finished) QVERIFY(environment.work.step(execution::Lane::Core));
    model.close();
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Stopping); QVERIFY(!model.fresh()); QVERIFY(!model.projectionPending());
    model.refresh(); model.setWalletLoadState({}, WalletListModel::LoadState::Open);
    environment.settle();
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Stopped);
    QCOMPARE(loader.reads.load(), read_finished ? 1 : 0); QCOMPARE(reset.size(), 0); QCOMPARE(model.rowCount(), 0);
    QVERIFY(environment.work.quiescent());
}

void WalletListModelTests::manualInterleavedPublicationRequiresExactDiffBase()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.services()};
    model.refresh(); model.setWalletInfo("alpha", 1, 0); environment.settle();
    QSignalSpy reset{&model, &QAbstractItemModel::modelReset};
    model.setWalletInfo("alpha", 2, 0);
    QVERIFY(environment.work.step(execution::Lane::Core)); QVERIFY(environment.work.step(execution::Lane::Domain));
    model.setWalletInfo("alpha", 1, 0);
    QVERIFY(environment.work.step(execution::Lane::Core));
    // The second diff was built against balance 1 while balance 2 had not yet
    // reached GUI. Publish balance 2 before delivering the second Domain turn.
    QVERIFY(environment.publishNext());
    QCOMPARE(model.data(model.index(0), WalletListModel::BalanceSatoshiRole).toLongLong(), qint64{2});
    QVERIFY(environment.work.step(execution::Lane::Domain)); environment.settle();
    QCOMPARE(model.data(model.index(0), WalletListModel::BalanceSatoshiRole).toLongLong(), qint64{1});
    QCOMPARE(reset.size(), 1); QCOMPARE(loader.reads.load(), 1); QVERIFY(!model.projectionPending());
    model.close(); environment.settle(); QVERIFY(environment.work.quiescent());
}

void WalletListModelTests::exactFacadeRoleSurvivesOldFacadeDestruction()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.services()};
    auto old_facade = std::make_unique<WalletQmlModel>();
    auto new_facade = std::make_unique<WalletQmlModel>();
    model.refresh(); model.setOpenWallet({}, old_facade.get()); environment.settle();
    QCOMPARE(model.data(model.index(0), WalletListModel::OpenWalletRole).value<WalletQmlModel*>(), old_facade.get());
    QSignalSpy changed{&model, &QAbstractItemModel::dataChanged};
    model.setOpenWallet({}, new_facade.get()); environment.settle();
    QCOMPARE(changed.size(), 1);
    QVERIFY(changed.at(0).at(2).value<QList<int>>().contains(WalletListModel::OpenWalletRole));
    old_facade.reset(); environment.settle();
    QCOMPARE(model.data(model.index(0), WalletListModel::OpenWalletRole).value<WalletQmlModel*>(), new_facade.get());
    QCOMPARE(changed.size(), 1);
    new_facade.reset(); environment.settle();
    QCOMPARE(model.data(model.index(0), WalletListModel::OpenWalletRole).value<WalletQmlModel*>(), nullptr);
    QCOMPARE(changed.size(), 2); QCOMPARE(loader.reads.load(), 1);
    model.close(); environment.settle(); QVERIFY(environment.work.quiescent());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(WalletListModelTests)
#else
QTEST_MAIN(WalletListModelTests)
#endif

#include <test_walletlistmodel.moc>
