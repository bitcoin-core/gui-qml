// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <qml/execution.h>
#include <qml/models/walletlistmodel.h>
#include <qml/models/walletmodel.h>
#include <scheduler.h>
#include <test/mocks/mocknode.h>
#include <util/translation.h>
#include <wallet/types.h>

#include <QSettings>
#include <QLocale>
#include <QThread>
#include <atomic>
#include <deque>

namespace {
class FakeWalletLoader : public interfaces::WalletLoader
{
public:
    std::vector<std::pair<std::string, std::string>> wallet_dir_entries;
    std::atomic<int> reads{0};
    std::atomic<QThread*> read_thread{nullptr};
    std::function<void()> during_read;
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
        if (during_read) during_read();
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
    using Lane = execution::Lane;
    using Turns = std::deque<std::function<void()>>;
    Turns core, utility, publications;
    std::optional<Lane> reject;
    execution::Executor executor{[this](Lane lane, execution::UtilityWorker worker, auto turn) {
        if (reject == lane) { reject.reset(); return false; }
        if (lane == Lane::Core) core.push_back(std::move(turn));
        else if (lane == Lane::Utility && worker == execution::UtilityWorker::IO) utility.push_back(std::move(turn));
        else return false;
        return true;
    }};

    ~CatalogExecution() { settle(); }
    presentation::Post post()
    {
        return [this](auto turn) { publications.push_back(std::move(turn)); };
    }
    static bool step(Turns& turns)
    {
        if (turns.empty()) return false;
        auto turn = std::move(turns.front());
        turns.pop_front();
        turn();
        return true;
    }
    bool publishNext() { return step(publications); }
    void publishAll() { while (publishNext()) {} }
    void drain() { while (step(core) || step(utility)) {} }
    void settle()
    {
        do { drain(); publishAll(); } while (!core.empty() || !utility.empty());
    }
    bool retire(WalletListModel& model)
    {
        model.close();
        const auto retired = std::make_shared<bool>(false);
        model.whenRetired([retired] { *retired = true; });
        settle();
        return *retired && model.phase() == WalletListModel::ReaderPhase::Stopped;
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
    void catalogRetirementWaitsForRunningDirectoryRead();
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
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"beta", "sqlite"}, {"alpha", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.executor, environment.post()};
    QSignalSpy reset{&model, &QAbstractItemModel::modelReset};
    QSignalSpy changed{&model, &QAbstractItemModel::dataChanged};
    model.listWalletDir(); QCOMPARE(loader.reads.load(), 0); QCOMPARE(model.rowCount(), 0);
    QVERIFY(environment.step(environment.core)); QCOMPARE(loader.reads.load(), 1); QCOMPARE(model.rowCount(), 0);
    QVERIFY(environment.step(environment.utility)); QCOMPARE(model.rowCount(), 0); QVERIFY(model.projectionPending());
    QVERIFY(environment.publishNext()); QCOMPARE(model.rowCount(), 2); QVERIFY(!model.projectionPending());
    QCOMPARE(model.data(model.index(0), WalletListModel::NameRole).toString(), QStringLiteral("alpha"));
    const auto reads = loader.reads.load(); const auto resets = reset.size();
    const QPersistentModelIndex stable{model.index(0)};
    if (scenario == 1) environment.reject = execution::Lane::Core;
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
    QVERIFY(environment.retire(model));
}


void WalletListModelTests::manualRefreshBurstReadsDirectoryOnce()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"beta", "sqlite"}, {"alpha", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.executor, environment.post()};
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Idle);
    QVERIFY(!model.fresh()); QVERIFY(!model.walletDirLoaded());
    for (int i = 0; i < 16; ++i) model.refresh();
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Scheduled);
    QVERIFY(model.projectionPending()); QCOMPARE(loader.reads.load(), 0);
    QCOMPARE(environment.core.size(), size_t{1});
    QVERIFY(environment.step(environment.core));
    QCOMPARE(loader.reads.load(), 1); QCOMPARE(model.rowCount(), 0);
    QVERIFY(environment.publishNext());
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Applying);
    QVERIFY(model.projectionPending()); QVERIFY(!model.fresh()); QCOMPARE(model.rowCount(), 0);
    QVERIFY(environment.step(environment.utility));
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
    QVERIFY(environment.retire(model));
}

void WalletListModelTests::manualRejectedDiscoveryRetainsOrderedInputsForRetry()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"named", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.executor, environment.post()};
    environment.reject = execution::Lane::Core;
    model.refresh(); environment.settle();
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Failed);
    QVERIFY(!model.projectionPending()); QVERIFY(!model.fresh()); QVERIFY(!model.readError().isEmpty());
    QCOMPARE(loader.reads.load(), 0); QCOMPARE(model.rowCount(), 0); QVERIFY(environment.core.empty() && environment.utility.empty());
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
    QVERIFY(environment.retire(model));
}

void WalletListModelTests::manualRefreshDuringOwnedReadRetainsEvents()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.executor, environment.post()};
    model.refresh(); QVERIFY(environment.step(environment.core));
    QCOMPARE(loader.reads.load(), 1);
    loader.wallet_dir_entries = {{"beta", "sqlite"}};
    for (int i = 0; i < 16; ++i) model.refresh();
    model.setWalletLoadState({}, WalletListModel::LoadState::Loading);
    model.setWalletLoadState({}, WalletListModel::LoadState::LoadError, "superseded failure");
    model.setWalletLoadState({}, WalletListModel::LoadState::Open);
    model.setWalletInfo({}, 42, 2);
    model.setOperationId({}, "default-opening");
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::ReadingWithPendingWork);
    QVERIFY(environment.step(environment.utility));
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
    QVERIFY(environment.retire(model));
}

void WalletListModelTests::manualReaderBackendIngressDuringGetterPreservesNewWork()
{
    CatalogExecution environment;
    std::weak_ptr<walletcatalog::Reader> weak;
    int reads{0};
    walletcatalog::Phase before{walletcatalog::Phase::Idle}, after{walletcatalog::Phase::Idle};
    auto reader = std::make_shared<walletcatalog::Reader>(environment.executor, [&] {
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
    QVERIFY(environment.step(environment.core));
    QCOMPARE(reads, 1); QCOMPARE(before, walletcatalog::Phase::Reading); QCOMPARE(after, walletcatalog::Phase::ReadingWithPendingWork);
    QVERIFY(environment.step(environment.utility));
    {
        const auto update = reader->take();
        QCOMPARE(update.observation.phase, walletcatalog::Phase::Scheduled); QVERIFY(!update.observation.fresh);
        QVERIFY(update.snapshot); QCOMPARE(update.snapshot->rows.size(), size_t{1});
        QCOMPARE(update.snapshot->rows.front().name, QStringLiteral("alpha"));
        reader->published(update.snapshot);
    }
    environment.drain();
    {
        const auto update = reader->take();
        QCOMPARE(reads, 2); QCOMPARE(update.observation.phase, walletcatalog::Phase::Current); QVERIFY(update.observation.fresh);
        QVERIFY(update.snapshot); QCOMPARE(update.snapshot->rows.size(), size_t{2});
        QCOMPARE(update.snapshot->rows.front().name, QString{});
        QCOMPARE(update.snapshot->rows.front().load_state, walletcatalog::LoadState::Open);
        QCOMPARE(update.snapshot->rows.back().name, QStringLiteral("beta"));
    }
    reader->close();
    bool retired{false}; reader->whenRetired([&] { retired = true; });
    environment.settle();
    QCOMPARE(reader->observation().phase, walletcatalog::Phase::Stopped); QVERIFY(retired);
}

void WalletListModelTests::manualFailedDiscoveryRetainsStaleRowsAndRecovers()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.executor, environment.post()};
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
    QVERIFY(environment.retire(model));
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
    WalletListModel model{node, environment.executor, environment.post()};
    model.setBackendAvailability(availability); model.refresh(); environment.settle();
    QCOMPARE(model.backendAvailability(), availability); QCOMPARE(loader.reads.load(), 0);
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Idle);
    QVERIFY(!model.walletDirLoaded()); QVERIFY(!model.fresh()); QVERIFY(!model.projectionPending());
    QCOMPARE(model.rowCount(), 0); QVERIFY(model.readError().isEmpty());
    model.setBackendAvailability(WalletListModel::BackendAvailability::Available); environment.settle();
    QCOMPARE(loader.reads.load(), 1); QVERIFY(model.walletDirLoaded()); QVERIFY(model.fresh());
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Current); QCOMPARE(model.rowCount(), 0);
    QVERIFY(environment.retire(model));
}

void WalletListModelTests::manualBackendAvailabilityDoesNotChangeRowLoadState()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.executor, environment.post()};
    model.refresh(); model.setWalletLoadState({}, WalletListModel::LoadState::Open); environment.settle();
    model.setBackendAvailability(WalletListModel::BackendAvailability::Disabled); environment.settle();
    QCOMPARE(loader.reads.load(), 1); QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.backendAvailability(), WalletListModel::BackendAvailability::Disabled); QVERIFY(!model.fresh());
    QCOMPARE(model.data(model.index(0), WalletListModel::LoadStateRole).toInt(), int(WalletListModel::LoadState::Open));
    model.setBackendAvailability(WalletListModel::BackendAvailability::Available); environment.settle();
    QCOMPARE(loader.reads.load(), 2); QVERIFY(model.fresh());
    QCOMPARE(model.data(model.index(0), WalletListModel::LoadStateRole).toInt(), int(WalletListModel::LoadState::Open));
    QVERIFY(environment.retire(model));
}

void WalletListModelTests::manualDefaultWalletHasExplicitLoadingAndFailureAbsence()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"", "sqlite"}, {"named", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.executor, environment.post()};
    model.refresh(); environment.settle();
    const auto row = [&] { return model.index(0); };
    QCOMPARE(model.data(row(), WalletListModel::NameRole).toString(), QString{});
    QCOMPARE(model.data(row(), WalletListModel::WalletIdRole).toString(), QString{});
    QCOMPARE(model.data(row(), WalletListModel::LoadStateRole).toInt(), int(WalletListModel::LoadState::Closed));
    QVERIFY(!model.data(row(), WalletListModel::DisplayNameRole).toString().isEmpty());
    QVERIFY(model.data(row(), WalletListModel::RowErrorRole).toString().isEmpty());
    QVERIFY(model.data(row(), WalletListModel::OpenWalletRole).isValid());
    QCOMPARE(model.data(row(), WalletListModel::OpenWalletRole).value<WalletModel*>(), nullptr);
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
    QVERIFY(environment.retire(model));
}

void WalletListModelTests::manualDefaultWalletObservationBeforeDiscovery()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"other", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.executor, environment.post()};
    model.setWalletLoadState({}, WalletListModel::LoadState::Open); model.setWalletInfo({}, 12, 1); environment.settle();
    QCOMPARE(model.rowCount(), 0); QCOMPARE(loader.reads.load(), 0); QVERIFY(!model.fresh());
    model.refresh(); environment.settle();
    QCOMPARE(model.rowCount(), 2); QCOMPARE(loader.reads.load(), 1);
    QCOMPARE(model.data(model.index(0), WalletListModel::WalletIdRole).toString(), QString{});
    QCOMPARE(model.data(model.index(0), WalletListModel::BalanceSatoshiRole).toLongLong(), qint64{12});
    model.setWalletLoadState({}, WalletListModel::LoadState::Closed); environment.settle();
    QCOMPARE(model.rowCount(), 1); QCOMPARE(loader.reads.load(), 1);
    QCOMPARE(model.data(model.index(0), WalletListModel::WalletIdRole).toString(), QStringLiteral("other"));
    QVERIFY(environment.retire(model));
}

void WalletListModelTests::manualOlderFailureCannotReplaceNewerDiscovery()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.executor, environment.post()};
    model.refresh(); environment.settle();
    loader.fail_read = true; model.refresh(); QVERIFY(environment.step(environment.core));
    loader.fail_read = false; loader.wallet_dir_entries = {{"beta", "sqlite"}}; model.refresh();
    QVERIFY(environment.step(environment.utility)); QVERIFY(environment.publishNext());
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Scheduled);
    QVERIFY(model.readError().isEmpty()); QVERIFY(model.projectionPending()); QVERIFY(!model.fresh());
    environment.settle(); QCOMPARE(loader.reads.load(), 3);
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Current); QVERIFY(model.fresh()); QVERIFY(model.readError().isEmpty());
    QCOMPARE(model.data(model.index(0), WalletListModel::WalletIdRole).toString(), QStringLiteral("beta"));
    QVERIFY(environment.retire(model));
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
    WalletListModel model{node, environment.executor, environment.post()};
    QSignalSpy reset{&model, &QAbstractItemModel::modelReset};
    model.refresh();
    if (read_finished) QVERIFY(environment.step(environment.core));
    bool retired{false};
    model.whenRetired([&] { retired = true; });
    model.close();
    QVERIFY(!retired);
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Stopping); QVERIFY(!model.fresh()); QVERIFY(!model.projectionPending());
    model.refresh(); model.setWalletLoadState({}, WalletListModel::LoadState::Open);
    environment.settle();
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Stopped);
    QCOMPARE(loader.reads.load(), read_finished ? 1 : 0); QCOMPARE(reset.size(), 0); QCOMPARE(model.rowCount(), 0);
    QVERIFY(retired);
}

void WalletListModelTests::catalogRetirementWaitsForRunningDirectoryRead()
{
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedWalletLoader(node, loader);
    loader.wallet_dir_entries = {{"alpha", "sqlite"}};
    QSemaphore entered, release;
    loader.during_read = [&] { entered.release(); release.acquire(); };
    execution::Executor executor{execution::Executor::Config{}};
    auto core = executor.makeQueue(execution::Lane::Core);
    const auto core_thread = std::make_shared<std::atomic<QThread*>>(nullptr);
    QVERIFY(core.post([core_thread] { *core_thread = QThread::currentThread(); }));
    WalletListModel model{node, executor, presentation::QueuedPost()};
    QSignalSpy reset{&model, &QAbstractItemModel::modelReset};
    std::atomic_bool retired{false};
    model.whenRetired([&] { retired = true; });
    const auto stop = qScopeGuard([&] {
        release.release();
        model.close();
        QVERIFY(QTest::qWaitFor([&] { return retired.load(); }, 5000));
        executor.stopAndJoin();
    });

    model.refresh();
    QVERIFY(entered.tryAcquire(1, 5000));
    QVERIFY(loader.read_thread.load() == core_thread->load());
    QVERIFY(loader.read_thread.load() != QThread::currentThread());
    model.close();
    bool heartbeat{false};
    QTimer::singleShot(0, &model, [&] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QVERIFY(!retired.load());
    QCOMPARE(model.phase(), WalletListModel::ReaderPhase::Stopping);
    release.release();
    QTRY_VERIFY(retired.load());
    QTRY_COMPARE(model.phase(), WalletListModel::ReaderPhase::Stopped);
    QCOMPARE(loader.reads.load(), 1);
    QCOMPARE(reset.size(), 0);
    QCOMPARE(model.rowCount(), 0);
}

void WalletListModelTests::manualInterleavedPublicationRequiresExactDiffBase()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.executor, environment.post()};
    model.refresh(); model.setWalletInfo("alpha", 1, 0); environment.settle();
    QSignalSpy reset{&model, &QAbstractItemModel::modelReset};
    model.setWalletInfo("alpha", 2, 0);
    QVERIFY(environment.step(environment.core)); QVERIFY(environment.step(environment.utility));
    model.setWalletInfo("alpha", 1, 0);
    QVERIFY(environment.step(environment.core));
    // The second diff was built against balance 1 while balance 2 had not yet
    // reached GUI. Publish balance 2 before delivering the second Utility turn.
    QVERIFY(environment.publishNext());
    QCOMPARE(model.data(model.index(0), WalletListModel::BalanceSatoshiRole).toLongLong(), qint64{2});
    QVERIFY(environment.step(environment.utility)); environment.settle();
    QCOMPARE(model.data(model.index(0), WalletListModel::BalanceSatoshiRole).toLongLong(), qint64{1});
    QCOMPARE(reset.size(), 1); QCOMPARE(loader.reads.load(), 1); QVERIFY(!model.projectionPending());
    QVERIFY(environment.retire(model));
}

void WalletListModelTests::exactFacadeRoleSurvivesOldFacadeDestruction()
{
    CatalogExecution environment;
    MockNode node; FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"", "sqlite"}};
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    WalletListModel model{node, environment.executor, environment.post()};
    auto old_facade = std::make_unique<WalletModel>();
    auto new_facade = std::make_unique<WalletModel>();
    model.refresh(); model.setOpenWallet({}, old_facade.get()); environment.settle();
    QCOMPARE(model.data(model.index(0), WalletListModel::OpenWalletRole).value<WalletModel*>(), old_facade.get());
    QSignalSpy changed{&model, &QAbstractItemModel::dataChanged};
    model.setOpenWallet({}, new_facade.get()); environment.settle();
    QCOMPARE(changed.size(), 1);
    QVERIFY(changed.at(0).at(2).value<QList<int>>().contains(WalletListModel::OpenWalletRole));
    old_facade.reset(); environment.settle();
    QCOMPARE(model.data(model.index(0), WalletListModel::OpenWalletRole).value<WalletModel*>(), new_facade.get());
    QCOMPARE(changed.size(), 1);
    new_facade.reset(); environment.settle();
    QCOMPARE(model.data(model.index(0), WalletListModel::OpenWalletRole).value<WalletModel*>(), nullptr);
    QCOMPARE(changed.size(), 2); QCOMPARE(loader.reads.load(), 1);
    QVERIFY(environment.retire(model));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(WalletListModelTests)
#else
QTEST_MAIN(WalletListModelTests)
#endif

#include <test_walletlistmodel.moc>
