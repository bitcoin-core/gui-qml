// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <QtTest/QtTest>
#include <test/mocks/mockwallet.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/admissionhookscheduler.h>
#include <test/qt_test_registry.h>
#include <qml/models/walletqmlmodel.h>
#include <key_io.h>
#include <util/string.h>
#include <addresstype.h>
#include <algorithm>
#include <set>
#include <tuple>
#include <QPersistentModelIndex>
#include <QElapsedTimer>
#include <QThread>
#include <QScopeGuard>
#include <QCoreApplication>
#include <QEvent>
#include <mutex>
#include <utility>

namespace {
CTxDestination CoinDestination(uint8_t seed) { return WitnessV0KeyHash{uint160{std::vector<unsigned char>(20, seed)}}; }
class CoinWallet final : public StubWallet {
public:
    std::mutex mutex;
    CoinsList coins;
    bool fail_read{false};
    std::atomic<QThread*> read_thread{nullptr}, lock_thread{nullptr}, coin_lookup_thread{nullptr};
    std::atomic<size_t> read_calls{0};
    void setReadFailure(bool failed) { std::lock_guard lock{mutex}; fail_read = failed; }
    void clearCoins() { std::lock_guard lock{mutex}; coins.clear(); }
    std::set<COutPoint> locked;
    std::set<COutPoint> fail_lock;
    std::set<COutPoint> throw_after_lock;
    std::vector<bool> lock_calls;
    std::vector<bool> lockCalls() { std::lock_guard lock{mutex}; return lock_calls; }
    std::map<CTxDestination, std::string> labels;
    CoinsList listCoins() override { ++read_calls; read_thread = QThread::currentThread(); std::lock_guard lock{mutex}; if (fail_read) throw std::runtime_error{"Coin read failed"}; return coins; }
    std::vector<interfaces::WalletTxOut> getCoins(const std::vector<COutPoint>& outputs) override {
        coin_lookup_thread = QThread::currentThread();
        std::lock_guard lock{mutex};
        std::vector<interfaces::WalletTxOut> result;
        for (const auto& target : outputs) {
            interfaces::WalletTxOut value{};
            for (const auto& [address, entries] : coins) {
                (void)address;
                for (const auto& [outpoint, output] : entries) if (outpoint == target) value = output;
            }
            result.push_back(value);
        }
        return result;
    }
    bool isLockedCoin(const COutPoint& p) override { std::lock_guard lock{mutex}; return locked.contains(p); }
    bool lockCoin(const COutPoint& p, bool) override { lock_thread = QThread::currentThread(); std::lock_guard lock{mutex}; lock_calls.push_back(true); if (fail_lock.contains(p)) return false; locked.insert(p); if (throw_after_lock.contains(p)) throw std::runtime_error{"lock result lost"}; return true; }
    bool unlockCoin(const COutPoint& p) override { lock_thread = QThread::currentThread(); std::lock_guard lock{mutex}; lock_calls.push_back(false); locked.erase(p); return true; }
    bool getAddress(const CTxDestination& d, std::string* name, wallet::AddressPurpose*) override {
        std::lock_guard lock{mutex};
        if (!labels.contains(d)) return false;
        if (name) *name = labels[d];
        return true;
    }
    void add(int n, CAmount amount, int64_t date, uint8_t address, std::string label) {
        const auto dest = CoinDestination(address);
        labels[dest] = label;
        // Deliberately group change under a different ancestor address.
        coins[CoinDestination(99)].emplace_back(COutPoint{Txid{}, static_cast<uint32_t>(n)}, interfaces::WalletTxOut{CTxOut{amount, GetScriptForDestination(dest)}, date, 6, false});
    }
};
struct ManualCoins {
    ManualRoutedExecution commands, retirement;
    std::shared_ptr<CoinWallet> wallet{std::make_shared<CoinWallet>()};
    std::shared_ptr<walletopening::Session> opening;
    std::deque<std::function<void()>> publications;
    std::unique_ptr<CoinsListModel> model;
    explicit ManualCoins(int count = 1, bool initialize = true) {
        for (int i = 0; i < count; ++i) wallet->add(i, 1000 + i, 1700000000 + i, 1 + i % 2, "coin " + util::ToString(i));
        opening = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {});
        opening->start(wallet); commands.drain(); opening->take();
        model = std::make_unique<CoinsListModel>();
        model->setOpening(opening, {commands.scheduler, [this](std::function<void()> turn) { publications.push_back(std::move(turn)); }});
        if (initialize) finish();
    }
    ~ManualCoins() {
        if (model) model->closeAdmission();
        commands.drain(); model.reset(); commands.drain();
        opening->retire(); commands.drain(); retirement.drain();
    }
    bool publishOne() {
        if (publications.empty()) return false;
        auto turn = std::move(publications.front()); publications.pop_front(); turn(); return true;
    }
    void finish() {
        do { commands.drain(); while (publishOne()) {} }
        while (!commands.queuedIdle() || !commands.activeIdle());
    }
};

}
class CoinsListModelTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void coinSelectionSessionOwnsExplicitModeDraftAndCatalogReconciliation()
    {
        coinselection::Session session{[](coinselection::Session::Selection values) {
            return std::make_shared<const coinselection::Session::Selection>(std::move(values));
        }};
        QVERIFY(session.ready());
        QCOMPARE(session.inputMode(), coinselection::InputMode::Automatic);
        QVERIFY(session.selection()->empty());

        const COutPoint first{Txid::FromUint256(uint256::ONE), 0};
        const COutPoint second{Txid::FromUint256(uint256::ONE), 1};
        const COutPoint third{Txid::FromUint256(uint256::ONE), 2};
        auto manual = session.apply(coinselection::Session::Intent::ChooseManual);
        QVERIFY(manual.changed); QCOMPARE(manual.mode, coinselection::InputMode::Manual);
        auto selected = session.apply(coinselection::Session::Intent::Select, first);
        QVERIFY(selected.changed); QVERIFY(session.selection()->contains(first));

        session.apply(coinselection::Session::Intent::Begin);
        session.apply(coinselection::Session::Intent::Toggle, second);
        QVERIFY(session.selection()->contains(first)); QVERIFY(session.selection()->contains(second));
        auto canceled = session.apply(coinselection::Session::Intent::Cancel);
        QVERIFY(canceled.changed); QCOMPARE(session.selection()->size(), size_t{1});
        QVERIFY(session.selection()->contains(first));

        QVERIFY(session.reconcileCatalog({first, second}));
        const auto catalog_version = session.catalogVersion();
        QVERIFY(session.reconcileCatalog({second}));
        QCOMPARE(session.catalogVersion(), catalog_version + 1);
        QVERIFY(session.selection()->contains(first));
        QVERIFY(session.missing().contains(first));

        session.apply(coinselection::Session::Intent::Begin);
        auto applied = session.apply(coinselection::Session::Intent::Apply);
        QVERIFY(applied.changed);
        QVERIFY(session.selection()->empty());
        QVERIFY(session.missing().empty());
        QCOMPARE(session.inputMode(), coinselection::InputMode::Manual);

        session.apply(coinselection::Session::Intent::Select, first);
        session.apply(coinselection::Session::Intent::Select, second);
        session.toggleMark(first); session.toggleMark(second);
        session.toggleMark(); // A marked batch consumes its admission snapshot.
        session.toggleMark(third); // A concurrent user mark must survive completion.
        coinselection::Session::Selection succeeded{first}, restore{second};
        QVERIFY(session.applyLockResults(succeeded, restore));
        QVERIFY(!session.selection()->contains(first));
        QVERIFY(session.selection()->contains(second));
        QVERIFY(session.marks()->contains(second));
        QVERIFY(session.marks()->contains(third));

        auto automatic = session.apply(coinselection::Session::Intent::ChooseAutomatic);
        QVERIFY(automatic.changed); QCOMPARE(automatic.mode, coinselection::InputMode::Automatic);
        QVERIFY(session.selection()->empty());
    }

    void successfulLockResultsCanBeNoOpForTheSelectionSession()
    {
        coinselection::Session session{[](coinselection::Session::Selection values) {
            return std::make_shared<const coinselection::Session::Selection>(std::move(values));
        }};
        const COutPoint marked{Txid::FromUint256(uint256::ONE), 1};
        QVERIFY(!session.toggleMark(marked).failed);
        const auto selection_revision = session.revision();
        const auto marks_revision = session.marksRevision();

        QVERIFY(session.applyLockResults({COutPoint{Txid::FromUint256(uint256::ONE), 0}}, {}));
        QVERIFY(session.applyLockResults({}, {marked}));
        QVERIFY(session.applyLockResults({}, {}));
        QCOMPARE(session.revision(), selection_revision);
        QCOMPARE(session.marksRevision(), marks_revision);
    }

    void failedLockResultRetentionIsNotReportedAsSuccessful()
    {
        bool reject_next_retention{false};
        coinselection::Session session{[&reject_next_retention](coinselection::Session::Selection values) {
            if (std::exchange(reject_next_retention, false)) return coinselection::Session::Snapshot{};
            return std::make_shared<const coinselection::Session::Selection>(std::move(values));
        }};
        const COutPoint selected{Txid::FromUint256(uint256::ONE), 0};
        QVERIFY(session.apply(coinselection::Session::Intent::Select, selected).changed);
        const auto revision = session.revision();

        reject_next_retention = true;
        QVERIFY(!session.applyLockResults({selected}, {}));
        QVERIFY(session.selection()->contains(selected));
        QCOMPARE(session.revision(), revision);
    }

    void coinControlAppliesExplicitInputModeAndReusePolicy()
    {
        wallet::CCoinControl control;
        coinselection::ApplyInputPolicy(control, coinselection::InputMode::Automatic);
        QVERIFY(control.m_allow_other_inputs);
        QVERIFY(control.m_avoid_address_reuse);

        // Manual is an explicit restriction even before the user selects an
        // input. Empty Manual must never turn into Automatic by implication.
        wallet::CCoinControl empty_manual;
        coinselection::ApplyInputPolicy(empty_manual, coinselection::InputMode::Manual);
        QVERIFY(!empty_manual.m_allow_other_inputs);
        QVERIFY(empty_manual.m_avoid_address_reuse);

        wallet::CCoinControl selected_manual;
        selected_manual.Select(COutPoint{Txid::FromUint256(uint256::ONE), 0});
        coinselection::ApplyInputPolicy(selected_manual, coinselection::InputMode::Manual);
        QVERIFY(!selected_manual.m_allow_other_inputs);
        QVERIFY(selected_manual.m_avoid_address_reuse);
        QCOMPARE(selected_manual.ListSelected().size(), size_t{1});
    }

    void explicitManualModeSurvivesEmptySelectionAndMissingInputs()
    {
        ManualCoins fixture{1}; auto& coins = *fixture.model;
        const auto id = coins.data(coins.index(0), CoinsListModel::CoinIdRole).toString();
        const auto initial_revision = coins.selectionRevision();
        QVERIFY(coins.setManualInputSelection(true));
        QVERIFY(coins.selectionPending());
        QVERIFY(!coins.manualInputSelection()); // Admission does not publish the new policy.
        QCOMPARE(coins.selectionRevision(), initial_revision);
        fixture.finish();
        QVERIFY(coins.manualInputSelection());
        QCOMPARE(coins.selectionRevision(), initial_revision + 1);
        QVERIFY(coins.selectionSnapshot()->empty());

        QVERIFY(coins.toggleCoinSelectionById(id)); fixture.finish();
        QCOMPARE(coins.selectedCoinsCount(), 1);
        fixture.wallet->clearCoins();
        coins.update(); fixture.finish();
        QVERIFY(coins.manualInputSelection());
        QCOMPARE(coins.selectedCoinsCount(), 1);
        QVERIFY(coins.selectionReconciliationRequired());
        QCOMPARE(coins.missingSelectedCoins(), 1);
        QVERIFY(coins.selectionSnapshot()->contains(COutPoint{Txid{}, 0}));

        // Switching to Automatic is the explicit operation that discards the
        // unavailable manual inputs and releases the reconciliation guard.
        QVERIFY(coins.setManualInputSelection(false)); fixture.finish();
        QVERIFY(!coins.manualInputSelection());
        QCOMPARE(coins.selectedCoinsCount(), 0);
        QVERIFY(!coins.selectionReconciliationRequired());
        QCOMPARE(coins.missingSelectedCoins(), 0);
    }

    void applyingEditedSelectionExplicitlyDropsUnavailableInputs()
    {
        ManualCoins fixture{1}; auto& coins = *fixture.model;
        const auto id = coins.data(coins.index(0), CoinsListModel::CoinIdRole).toString();
        QVERIFY(coins.setManualInputSelection(true)); fixture.finish();
        QVERIFY(coins.toggleCoinSelectionById(id)); fixture.finish();
        fixture.wallet->clearCoins(); coins.update(); fixture.finish();
        QVERIFY(coins.selectionReconciliationRequired());
        QCOMPARE(coins.missingSelectedCoins(), 1);

        coins.beginSelection();
        coins.applySelection();
        fixture.finish();
        QVERIFY(coins.manualInputSelection());
        QVERIFY(coins.selectionSnapshot()->empty());
        QVERIFY(!coins.selectionReconciliationRequired());
    }

    void newerInventoryReconcilesCatalogBeforeAnOlderSelectionReadCanPublish()
    {
        ManualCoins fixture{0}; auto& coins = *fixture.model;
        QCOMPARE(coins.rowCount(), 0);
        const auto original_revision = coins.selectionRevision();
        const auto reads = fixture.wallet->read_calls.load();
        for (int i{0}; i < 3; ++i)
            fixture.wallet->add(i, 1000 + i, 1700000000 + i, 1, "new coin");

        coins.update();
        QVERIFY(coins.setManualInputSelection(true));
        // Capture the newer inventory at the old selection epoch. The Manual
        // intent is ahead of its outcome on Domain, so that intent settles
        // before the snapshot is consumed. Keep the requested Core retry held.
        QVERIFY(fixture.commands.step(execution::Lane::Core));
        QCOMPARE(fixture.wallet->read_calls.load(), reads + 1);
        const auto domain_and_gui = [&] {
            for (int turn{0}; turn < 128; ++turn) {
                bool progressed = fixture.commands.step(execution::Lane::Domain);
                while (fixture.publishOne()) progressed = true;
                if (!progressed) return true;
            }
            return false;
        };
        QVERIFY(domain_and_gui());
        QCOMPARE(coins.rowCount(), 3);
        QVERIFY(coins.manualInputSelection());
        QVERIFY(!coins.selectionPending());
        QCOMPARE(coins.selectionRevision(), original_revision + 1);
        QVERIFY(fixture.commands.queuedCountIf([](const auto& turn) { return turn.lane == execution::Lane::Core; }) == 1);
        QCOMPARE(fixture.wallet->read_calls.load(), reads + 1);

        const COutPoint original{Txid{}, 0};
        const QString id = QString::fromStdString(original.hash.GetHex()) + QStringLiteral(":0");
        coins.beginSelection();
        QVERIFY(coins.toggleCoinSelectionById(id));
        coins.applySelection();
        QVERIFY(domain_and_gui());
        QVERIFY(!coins.selectionPending());
        QVERIFY(coins.manualInputSelection());
        QCOMPARE(coins.selectionSnapshot()->size(), size_t{1});
        QVERIFY(coins.selectionSnapshot()->contains(original));
        QCOMPARE(coins.totalSelectedSatoshi(), qint64{1000});
        QVERIFY(!coins.selectionReconciliationRequired());
        QCOMPARE(fixture.wallet->read_calls.load(), reads + 1);
        QVERIFY(fixture.commands.queuedCountIf([](const auto& turn) { return turn.lane == execution::Lane::Core; }) == 1);
        QVERIFY(fixture.wallet->lockCalls().empty());
    }

    void coinRefreshKeepsSelectionIdentity()
    {
        ManualCoins fixture{2};
        QSignalSpy aggregates{fixture.model.get(), &CoinsListModel::selectedCoinsCountChanged};
        QSignalSpy selection{fixture.model.get(), &CoinsListModel::selectionSnapshotChanged};
        fixture.model->update();
        fixture.finish();
        QVERIFY(!aggregates.isEmpty());
        QCOMPARE(selection.size(), 0);

        fixture.model->toggleCoinSelection(0);
        fixture.finish();
        QCOMPARE(fixture.model->selectedCoinsCount(), 1);
        QVERIFY(!selection.isEmpty());
        aggregates.clear();
        selection.clear();

        fixture.model->update();
        fixture.finish();
        QVERIFY(!aggregates.isEmpty());
        QCOMPARE(selection.size(), 0);
        QCOMPARE(fixture.model->selectedCoinsCount(), 1);
    }

    // #1119: wallet notifications arrive back to back. Each one must update the
    // rows it changes in place. A whole-model reset deletes every row delegate,
    // and on macOS a reset that follows closely on another destroyed a delegate
    // that was still being created, which crashed the app.
    void backToBackRefreshesUpdateRowsInPlaceWithoutResettingTheModel()
    {
        ManualCoins fixture{40};
        QCOMPARE(fixture.model->rowCount(), 40);
        const QPersistentModelIndex first{fixture.model->index(0)}, last{fixture.model->index(39)};
        const auto first_id = first.data(CoinsListModel::CoinIdRole), last_id = last.data(CoinsListModel::CoinIdRole);
        QSignalSpy resets{fixture.model.get(), &QAbstractItemModel::modelAboutToBeReset};
        QSignalSpy layouts{fixture.model.get(), &QAbstractItemModel::layoutAboutToBeChanged};
        QSignalSpy inserted{fixture.model.get(), &QAbstractItemModel::rowsInserted};

        // Two payments arrive while earlier refreshes are still being applied.
        fixture.model->update(); fixture.model->update();
        { std::lock_guard lock{fixture.wallet->mutex}; fixture.wallet->add(40, 5000, 1700000100, 1, "arrival one"); }
        fixture.model->update();
        { std::lock_guard lock{fixture.wallet->mutex}; fixture.wallet->add(41, 6000, 1700000101, 2, "arrival two"); }
        fixture.model->update(); fixture.model->update();
        fixture.finish();

        QCOMPARE(fixture.model->rowCount(), 42);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(layouts.size(), 0);
        QVERIFY(inserted.size() >= 1);
        // The rows that were already shown are the same rows, not rebuilt ones.
        QVERIFY(first.isValid()); QVERIFY(last.isValid());
        QCOMPARE(first.data(CoinsListModel::CoinIdRole), first_id);
        QCOMPARE(last.data(CoinsListModel::CoinIdRole), last_id);
    }

    void cleanupAdmissionKeepsOriginalCoinOwner_data() {
        QTest::addColumn<int>("reservation"); QTest::addColumn<int>("outcome");
        for (int reservation : {1, 2, 3}) for (int outcome : {0, 1, 2})
            QTest::newRow(qPrintable(QString::number(reservation) + QLatin1Char('-') + QString::number(outcome))) << reservation << outcome;
    }
    void cleanupAdmissionKeepsOriginalCoinOwner() {
        QFETCH(int, reservation); QFETCH(int, outcome);
        ManualRoutedExecution commands, retirement;
        auto scheduler = std::make_shared<AdmissionHookScheduler>(commands.scheduler);
        auto wallet = std::make_shared<CoinWallet>(); wallet->add(0, 7000, 1700000000, 1, "original coin");
        auto opening = std::make_shared<walletopening::Session>(scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {});
        opening->start(wallet); commands.drain(); opening->take();
        std::deque<std::function<void()>> old_posts, current_posts;
        size_t old_posted{0}, current_posted{0};
        auto model = std::make_unique<CoinsListModel>();
        const auto settle = [&] {
            for (int turn{0}; turn < 512 && (turn == 0 || !commands.queuedIdle() || !retirement.queuedIdle() || !old_posts.empty() || !current_posts.empty()); ++turn) {
                commands.drain(); retirement.drain();
                for (auto* queue : {&old_posts, &current_posts}) while (!queue->empty()) {
                    auto work = std::move(queue->front()); queue->pop_front(); work();
                }
                QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
        };
        const auto cleanup = qScopeGuard([&] {
            scheduler->disarm(); if (model) model->closeAdmission(); model.reset(); settle();
            opening->retire(); settle();
        });
        auto trigger = scheduler->armCleanup(reservation, [&] {
            if (outcome == 0) model->closeAdmission();
            else if (outcome == 1) model.reset();
            else model->setOpening(opening, {scheduler, [&](auto work) { ++current_posted; current_posts.push_back(std::move(work)); }});
        });
        model->setOpening(opening, {scheduler, [&](auto work) { ++old_posted; old_posts.push_back(std::move(work)); }});
        QCOMPARE(trigger->calls.load(), size_t{1}); QVERIFY(trigger->originalId.load() != 0);
        settle();
        QCOMPARE(old_posted, size_t{0}); QVERIFY(wallet->lockCalls().empty());
        if (outcome == 2) {
            QVERIFY(model); QCOMPARE(model->rowCount(), 1); QCOMPARE(model->totalSatoshi(), qint64{7000});
            QCOMPARE(model->data(model->index(0), CoinsListModel::LabelRole).toString(), QStringLiteral("original coin"));
            QVERIFY(current_posted > 0); QCOMPARE(wallet->read_calls.load(), size_t{1});
        } else {
            QCOMPARE(wallet->read_calls.load(), size_t{0});
            if (outcome == 0) { QVERIFY(model); QCOMPARE(model->rowCount(), 0); }
            else QVERIFY(!model);
        }
        scheduler->disarm(); if (model) model->closeAdmission(); model.reset(); settle(); opening->retire(); settle();
        QCOMPARE(commands.scheduler->pending(), size_t{0}); QCOMPARE(retirement.scheduler->pending(), size_t{0});
    }
    void quietCloseDuringQtRows_data() {
        QTest::addColumn<int>("mutation"); QTest::addColumn<bool>("about");
        for (int mutation{0}; mutation < 3; ++mutation)
            for (bool about : {false, true}) QTest::newRow(qPrintable(QString::number(mutation) + (about ? "-about" : "-ended"))) << mutation << about;
    }
    void quietCloseDuringQtRows() {
        QFETCH(int, mutation); QFETCH(bool, about); ManualCoins f{3, mutation != 0};
        auto* model = f.model.get(); bool closed{false}; int at_end{-1};
        const auto close = [&] { const auto rows = model->rowCount(); model->closeAdmission(); QCOMPARE(model->rowCount(), rows); closed = true; };
        if (mutation == 0) {
            connect(model, about ? &QAbstractItemModel::rowsAboutToBeInserted : &QAbstractItemModel::rowsInserted, this, close);
            connect(model, &QAbstractItemModel::rowsInserted, this, [&] { at_end = model->rowCount(); });
        } else if (mutation == 1) {
            connect(model, about ? &QAbstractItemModel::rowsAboutToBeRemoved : &QAbstractItemModel::rowsRemoved, this, close);
            connect(model, &QAbstractItemModel::rowsRemoved, this, [&] { at_end = model->rowCount(); }); model->setSearchText("no matching coin");
        } else {
            connect(model, about ? &QAbstractItemModel::rowsAboutToBeMoved : &QAbstractItemModel::rowsMoved, this, close);
            connect(model, &QAbstractItemModel::rowsMoved, this, [&] { at_end = model->rowCount(); }); model->setSortDescending(false);
        }
        f.finish(); QVERIFY(closed); QCOMPARE(at_end, mutation == 0 ? 1 : mutation == 1 ? 2 : 3); QCOMPARE(model->rowCount(), 0);
        f.opening->retire(); f.commands.scheduler->closeAdmission(); f.commands.drain(); f.retirement.drain();
        QCOMPARE(f.commands.scheduler->pending(), size_t{0});
    }
    void quietCloseReleasesIdleSnapshotsBeforeFacadeDestruction() {
        ManualCoins fixture{129};
        fixture.model->setSortBy("amount"); fixture.finish();
        QCOMPARE(fixture.model->rowCount(), 129);
        fixture.model->closeAdmission();
        QCOMPARE(fixture.model->rowCount(), 0);
        QVERIFY(!fixture.model->selectionSnapshot());
        fixture.opening->retire(); fixture.commands.scheduler->closeAdmission();
        fixture.commands.drain(); fixture.retirement.drain();
        // Keeping the closed facade alive must not keep its Native value
        // ownership reservations alive or block the executor's final drain.
        QVERIFY(fixture.model);
        QCOMPARE(fixture.commands.scheduler->pending(), size_t{0});
    }
    void manualNativeDomainAndBoundedGuiPublication() {
        ManualCoins fixture{257, false};
        QCOMPARE(fixture.model->rowCount(), 0); QCOMPARE(fixture.model->coinCount(), 0);
        fixture.commands.drain();
        QCOMPARE(fixture.model->rowCount(), 0); QCOMPARE(fixture.model->totalSatoshi(), 0);
        QVERIFY(fixture.publishOne());
        QVERIFY(fixture.model->rowCount() <= 32); QVERIFY(fixture.model->loading());
        int previous = fixture.model->rowCount();
        while (fixture.publishOne()) {
            QVERIFY(std::abs(fixture.model->rowCount() - previous) <= 32);
            previous = fixture.model->rowCount();
        }
        QCOMPARE(fixture.model->rowCount(), 257); QCOMPARE(fixture.model->coinCount(), 257);
        QCOMPARE(fixture.model->totalSatoshi(), qint64{257 * 1000 + 256 * 257 / 2});
        QVERIFY(!fixture.model->loading());
    }
    void anExternalLockIsNotSilentlyUpgradedAndInvalidatesManualAvailability()
    {
        ManualCoins fixture{1}; auto& coins = *fixture.model;
        const auto id = coins.data(coins.index(0), CoinsListModel::CoinIdRole).toString();
        QVERIFY(coins.setManualInputSelection(true));
        QVERIFY(coins.toggleCoinSelectionById(id)); QVERIFY(coins.toggleMarkById(id)); fixture.finish();
        {
            std::lock_guard lock{fixture.wallet->mutex};
            fixture.wallet->locked.insert(COutPoint{Txid{}, 0});
        }
        QVERIFY(!coins.data(coins.index(0), CoinsListModel::LockedRole).toBool());
        QVERIFY(coins.lockMarkedCoins(true)); const auto owner = coins.lockOperation(); fixture.finish();
        QVERIFY(owner && owner->result() && owner->result()->value);
        const auto original_receipt = owner->result();
        QVERIFY(original_receipt && original_receipt->value);
        const auto& result = *original_receipt->value;
        QVERIFY(result.failed); QVERIFY(result.already_locked); QVERIFY(!result.uncertain);
        QCOMPARE(result.changed, size_t{0}); QCOMPARE(result.items.size(), size_t{1});
        QCOMPARE(result.items[0].state, CoinsListModel::LockItemOutcome::State::AlreadyLocked);
        QVERIFY(fixture.wallet->lockCalls().empty());
        QCOMPARE(coins.markedCount(), 1); QVERIFY(coins.manualInputSelection());
        QCOMPARE(coins.selectedCoinsCount(), 1); QVERIFY(coins.selectionReconciliationRequired());
        QVERIFY(coins.lockError().contains(QStringLiteral("already locked")));
    }

    void fiveThousandCoinsPublishInBoundedTurns()
    {
        ManualCoins fixture{5000, false};
        QElapsedTimer total; total.start();
        fixture.commands.drain();
        const auto worker_ns = total.nsecsElapsed();
        QCOMPARE(fixture.model->rowCount(), 0);
        qint64 maximum_turn_ns{0};
        int previous{0}, turns{0};
        while (!fixture.publications.empty()) {
            QElapsedTimer turn; turn.start();
            QVERIFY(fixture.publishOne());
            maximum_turn_ns = std::max(maximum_turn_ns, turn.nsecsElapsed());
            const auto count = fixture.model->rowCount();
            QVERIFY(count - previous >= 0 && count - previous <= 32);
            previous = count; ++turns;
        }
        QCOMPARE(fixture.model->rowCount(), 5000);
        QVERIFY(turns >= 157);
        QVERIFY(!fixture.model->loading());
        QCOMPARE(fixture.model->totalSatoshi(), qint64{5000 * 1000 + 4999LL * 5000 / 2});
        qInfo().nospace() << "R18 synthetic 5000-coin trace: worker_ns=" << worker_ns
                         << " publication_turns=" << turns << " maximum_turn_ns=" << maximum_turn_ns
                         << " total_ns=" << total.nsecsElapsed();
    }

    void lockedInputCannotReturnWhenAnEditingPickerIsCancelled()
    {
        coinselection::Session session{[](coinselection::Session::Selection value) {
            return std::make_shared<const coinselection::Session::Selection>(std::move(value));
        }};
        const COutPoint locked{Txid{}, 0}, untouched{Txid{}, 1}, temporary{Txid{}, 2};
        session.apply(coinselection::Session::Intent::ChooseManual);
        session.apply(coinselection::Session::Intent::Select, locked);
        session.apply(coinselection::Session::Intent::Select, untouched);
        session.apply(coinselection::Session::Intent::Begin);
        session.apply(coinselection::Session::Intent::Select, temporary);
        QVERIFY(session.applyLockResults({locked}, {}));
        session.apply(coinselection::Session::Intent::Cancel);
        QCOMPARE(*session.selection(), (coinselection::Session::Selection{untouched}));
        QCOMPARE(session.inputMode(), coinselection::InputMode::Manual);
    }

    void pickerDismissalPreservesPartialLockOutcome_data()
    {
        QTest::addColumn<bool>("effect_completed");
        QTest::newRow("dismiss-before-native-effect") << false;
        QTest::newRow("dismiss-after-effect-before-result") << true;
    }
    void pickerDismissalPreservesPartialLockOutcome()
    {
        QFETCH(bool, effect_completed);
        ManualCoins fixture{4}; auto& coins = *fixture.model;
        coins.setSortBy("amount"); coins.setSortDescending(false); fixture.finish();
        QStringList ids;
        for (int row{0}; row < 4; ++row) ids.push_back(coins.data(coins.index(row), CoinsListModel::CoinIdRole).toString());
        QCOMPARE(ids[0], QString::fromStdString(Txid{}.GetHex()) + QStringLiteral(":0"));
        QCOMPARE(ids[3], QString::fromStdString(Txid{}.GetHex()) + QStringLiteral(":3"));
        QVERIFY(coins.setManualInputSelection(true));
        QVERIFY(coins.toggleCoinSelectionById(ids[0]));
        QVERIFY(coins.toggleCoinSelectionById(ids[3])); fixture.finish();
        QCOMPARE(coins.selectedCoinsCount(), 2);
        coins.beginSelection();
        QVERIFY(coins.toggleCoinSelectionById(ids[1]));
        for (int row{0}; row < 3; ++row) QVERIFY(coins.toggleMarkById(ids[row]));
        fixture.finish();
        QCOMPARE(coins.selectedCoinsCount(), 3); QCOMPARE(coins.markedCount(), 3);
        {
            std::lock_guard lock{fixture.wallet->mutex};
            fixture.wallet->fail_lock.insert(COutPoint{Txid{}, 1});
            for (auto& [address, entries] : fixture.wallet->coins) {
                (void)address;
                std::erase_if(entries, [](const auto& entry) { return std::get<0>(entry) == COutPoint{Txid{}, 2}; });
            }
        }
        const auto calls = fixture.commands.coreSubmissions();
        QVERIFY(coins.lockMarkedCoins(true));
        const auto operation = coins.lockOperation(); QVERIFY(operation);
        for (int turn{0}; fixture.commands.coreSubmissions() == calls && turn < 16; ++turn)
            QVERIFY(fixture.commands.step(execution::Lane::Domain));
        QVERIFY(fixture.commands.coreSubmissions() > calls);
        if (effect_completed) QVERIFY(fixture.commands.step(execution::Lane::Core));
        // These are the production picker close semantics. Closing is not
        // cancellation of an already admitted lock operation.
        coins.cancelSelection();
        fixture.finish();
        QVERIFY(operation->result()); QVERIFY(operation->result()->value);
        const auto original_receipt = operation->result();
        QVERIFY(original_receipt && original_receipt->value);
        const auto& result = *original_receipt->value;
        QVERIFY(result.failed); QVERIFY(!result.uncertain);
        QCOMPARE(result.requested, size_t{3}); QCOMPARE(result.changed, size_t{1});
        QCOMPARE(result.items.size(), size_t{3});
        QCOMPARE(result.items[0].state, CoinsListModel::LockItemOutcome::State::Changed);
        QCOMPARE(result.items[1].state, CoinsListModel::LockItemOutcome::State::Failed);
        QCOMPARE(result.items[2].state, CoinsListModel::LockItemOutcome::State::Failed);
        QVERIFY(!fixture.wallet->locked.contains(COutPoint{Txid{}, 2}));
        QCOMPARE(fixture.wallet->lockCalls().size(), size_t{2});
        QVERIFY(coins.manualInputSelection());
        QCOMPARE(*coins.selectionSnapshot(), (CoinsListModel::Selection{COutPoint{Txid{}, 3}}));
        // #974: the failed mark of coin 1 is restored; removed coin 2 is no
        // longer in the wallet and so cannot keep a bulk mark.
        QCOMPARE(coins.markedCount(), 1);
        QVERIFY(!coins.lockError().isEmpty()); QVERIFY(!coins.lockPending());
    }

    void manualReorderPreservesPersistentIdentityAndFencesOldControls() {
        ManualCoins fixture{129}; auto& coins = *fixture.model;
        QPersistentModelIndex persistent{coins.index(0)};
        const auto id = persistent.data(CoinsListModel::CoinIdRole);
        const auto amount = persistent.data(CoinsListModel::AmountSatoshiRole);
        coins.setGroupBy("address"); coins.setSortBy("amount"); coins.setSortDescending(false);
        fixture.commands.drain();
        coins.setSearchText("no matching coin");
        // A held publication from prior controls cannot publish its old list.
        while (fixture.publishOne()) {}
        QCOMPARE(coins.rowCount(), 129);
        fixture.finish(); QCOMPARE(coins.rowCount(), 0); QVERIFY(!persistent.isValid());
        coins.setSearchText(""); fixture.finish();
        QPersistentModelIndex surviving{coins.index(0)};
        const auto survivor = surviving.data(CoinsListModel::CoinIdRole);
        coins.setSortDescending(true); fixture.finish();
        QVERIFY(surviving.isValid()); QCOMPARE(surviving.data(CoinsListModel::CoinIdRole), survivor);
        QVERIFY(!id.toString().isEmpty()); QVERIFY(amount.toLongLong() > 0);
    }
    void iterativeRowEditsPreserveEveryPersistentIdentity() {
        ManualCoins fixture{513};auto& coins=*fixture.model;
        std::vector<QPersistentModelIndex> original;
        std::vector<QVariant> identities;std::vector<qint64> amounts;
        original.reserve(513);identities.reserve(513);amounts.reserve(513);
        for(int row=0;row<coins.rowCount();++row){
            original.emplace_back(coins.index(row));
            identities.push_back(original.back().data(CoinsListModel::CoinIdRole));
            amounts.push_back(original.back().data(CoinsListModel::AmountSatoshiRole).toLongLong());
        }
        coins.setSortBy("amount");coins.setSortDescending(false);fixture.finish();
        QCOMPARE(coins.rowCount(),513);
        for(int row=0;row<coins.rowCount();++row)
            QCOMPARE(coins.data(coins.index(row),CoinsListModel::AmountSatoshiRole).toLongLong(),qint64{1000+row});
        coins.setSortDescending(true);fixture.finish();
        for(size_t row=0;row<original.size();++row){
            QVERIFY(original[row].isValid());
            QCOMPARE(original[row].data(CoinsListModel::CoinIdRole),identities[row]);
        }
        // Remove both leaf and internal nodes from the balanced sequence.
        coins.setMinAmount(1256);fixture.finish();QCOMPARE(coins.rowCount(),257);
        for(size_t row=0;row<original.size();++row){
            if(amounts[row]>=1256){QVERIFY(original[row].isValid());QCOMPARE(original[row].data(CoinsListModel::CoinIdRole),identities[row]);}
            else QVERIFY(!original[row].isValid());
        }
        coins.setMinAmount(-1);fixture.finish();QCOMPARE(coins.rowCount(),513);
        for(int row=0;row<coins.rowCount();++row)
            QCOMPARE(coins.data(coins.index(row),CoinsListModel::AmountSatoshiRole).toLongLong(),qint64{1512-row});
    }
    void rejectedProjectionPreservesTheLastCompleteInventory() {
        ManualCoins fixture{9}; auto& coins = *fixture.model;
        fixture.commands.rejectLane(execution::Lane::Domain);
        coins.setSearchText("no match"); while (fixture.publishOne()) {}
        QCOMPARE(coins.coinCount(), 9); QCOMPARE(coins.rowCount(), 9); QVERIFY(!coins.readError().isEmpty());
        coins.setSearchText("coin"); fixture.finish();
        QCOMPARE(coins.rowCount(), 9); QVERIFY(coins.readError().isEmpty());
    }
    void selectionCommandsUseOriginalDomainOrderAndNullExplicitlyClears() {
        ManualCoins fixture{3}; auto& coins = *fixture.model;
        const auto id = coins.data(coins.index(0), CoinsListModel::CoinIdRole).toString();
        QVERIFY(coins.setManualInputSelection(true));
        QVERIFY(coins.toggleCoinSelectionById(id)); coins.beginSelection();
        QVERIFY(coins.toggleCoinSelectionById(id)); coins.cancelSelection();
        QVERIFY(coins.selectionPending()); QVERIFY(coins.selectionSnapshot()->empty());
        fixture.finish();
        QCOMPARE(coins.selectedCoinsCount(), 1); QCOMPARE(coins.selectionSnapshot()->size(), size_t{1});
        const auto original = coins.selectionSnapshot();
        QVERIFY(coins.requestReplaceSelection({})); fixture.finish();
        QVERIFY(coins.selectionSnapshot()->empty()); QCOMPARE(coins.selectedCoinsCount(), 0);
        QCOMPARE(original->size(), size_t{1});
    }
    void markedBatchRetainsFilteredOutIdentitiesAndClearsOnAdmission() {
        ManualCoins fixture{3}; auto& coins = *fixture.model;
        const auto first = coins.data(coins.index(0), CoinsListModel::CoinIdRole).toString();
        const auto second = coins.data(coins.index(1), CoinsListModel::CoinIdRole).toString();
        QVERIFY(coins.toggleMarkById(first)); QVERIFY(coins.toggleMarkById(second));
        QVERIFY(coins.marksPending()); fixture.finish(); QCOMPARE(coins.markedCount(), 2);
        // Address labels are shared by all coins paying that destination.
        // Filter by the original unique coin identity to hide exactly the
        // other marked coin, independently of repeated fixture destinations.
        coins.setSearchText(second); fixture.finish(); QCOMPARE(coins.rowCount(), 1);
        QCOMPARE(coins.data(coins.index(0), CoinsListModel::CoinIdRole).toString(), second);
        QVERIFY(coins.data(coins.index(0), CoinsListModel::MarkedRole).toBool());
        QCOMPARE(coins.markedCount(), 2);
        QVERIFY(coins.lockMarkedCoins(true)); fixture.finish();
        QCOMPARE(fixture.wallet->lockCalls(), std::vector<bool>({true, true}));
        QCOMPARE(coins.markedCount(), 0); QVERIFY(!coins.lockPending());
        QVERIFY(coins.lockError().isEmpty()); QCOMPARE(coins.lockedCoinsCount(), 2);
    }
    void markedLockReportsEachItemAndRestoresOnlyUnappliedMarks() {
        ManualCoins fixture{3}; auto& coins = *fixture.model;
        for (int row = 0; row < coins.rowCount(); ++row) {
            const auto id = coins.data(coins.index(row), CoinsListModel::CoinIdRole).toString();
            QVERIFY(coins.toggleMarkById(id));
        }
        fixture.finish(); QCOMPARE(coins.markedCount(), 3);
        const COutPoint failed{Txid{}, 1};
        { std::lock_guard lock{fixture.wallet->mutex}; fixture.wallet->fail_lock.insert(failed); }

        QVERIFY(coins.lockMarkedCoins(true));
        const auto owner = coins.lockOperation(); QVERIFY(owner);
        fixture.finish();
        QVERIFY(owner->result()); QVERIFY(owner->result()->value);
        const auto original_receipt = owner->result();
        QVERIFY(original_receipt && original_receipt->value);
        const auto& outcome = *original_receipt->value;
        QVERIFY(outcome.failed); QVERIFY(!outcome.uncertain);
        QCOMPARE(outcome.requested, size_t{3});
        QCOMPARE(outcome.changed, size_t{2});
        QCOMPARE(outcome.items.size(), size_t{3});
        QCOMPARE(outcome.items[0].state, CoinsListModel::LockItemOutcome::State::Changed);
        QCOMPARE(outcome.items[1].state, CoinsListModel::LockItemOutcome::State::Failed);
        QCOMPARE(outcome.items[2].state, CoinsListModel::LockItemOutcome::State::Changed);
        QCOMPARE(outcome.items[1].outpoint, failed);
        QCOMPARE(coins.markedCount(), 1);
        QVERIFY(outcome.marks->contains(failed));
        QVERIFY(!outcome.marks->contains(COutPoint{Txid{}, 0}));
        QVERIFY(!outcome.marks->contains(COutPoint{Txid{}, 2}));
        QCOMPARE(fixture.wallet->locked.size(), size_t{2});
        QVERIFY(fixture.wallet->locked.contains(COutPoint{Txid{}, 0}));
        QVERIFY(fixture.wallet->locked.contains(COutPoint{Txid{}, 2}));
    }
    void unknownMarkedLockResultRefreshesNativeStateWithoutRetrying()
    {
        ManualCoins fixture{1}; auto& coins = *fixture.model;
        const auto id = coins.data(coins.index(0), CoinsListModel::CoinIdRole).toString();
        QVERIFY(coins.toggleMarkById(id)); fixture.finish();
        const COutPoint outpoint{Txid{}, 0};
        { std::lock_guard lock{fixture.wallet->mutex}; fixture.wallet->throw_after_lock.insert(outpoint); }

        QVERIFY(coins.lockMarkedCoins(true));
        const auto owner = coins.lockOperation(); QVERIFY(owner);
        fixture.finish();
        QVERIFY(owner->result()); QVERIFY(owner->result()->value);
        const auto original_receipt = owner->result();
        QVERIFY(original_receipt && original_receipt->value);
        const auto& outcome = *original_receipt->value;
        QVERIFY(outcome.uncertain);
        QCOMPARE(outcome.items.size(), size_t{1});
        QCOMPARE(outcome.items.front().state, CoinsListModel::LockItemOutcome::State::Unknown);
        QCOMPARE(coins.markedCount(), 1);
        QCOMPARE(fixture.wallet->lockCalls(), std::vector<bool>{true});
        QCOMPARE(coins.data(coins.index(0), CoinsListModel::LockedRole).toBool(), true);
    }
    void cancellingMarksOrdersAfterUnpublishedToggleAndPreservesSpendSelection() {
        ManualCoins fixture{3}; auto& coins = *fixture.model;
        const auto id = coins.data(coins.index(0), CoinsListModel::CoinIdRole).toString();
        QVERIFY(coins.setManualInputSelection(true));
        QVERIFY(coins.toggleCoinSelectionById(id)); fixture.finish();
        const auto selected = coins.selectionSnapshot();
        QVERIFY(coins.toggleMarkById(id)); QVERIFY(coins.clearMarks()); fixture.finish();
        QCOMPARE(coins.markedCount(), 0); QCOMPARE(coins.selectionSnapshot(), selected);
        QVERIFY(!coins.data(coins.index(0), CoinsListModel::MarkedRole).toBool());
        QCOMPARE(coins.selectedCoinsCount(), 1);
    }
    void failedNativeReadCannotEraseSelectionOrSuccessfulRows() {
        ManualCoins fixture{3}; auto& coins = *fixture.model;
        const auto id = coins.data(coins.index(0), CoinsListModel::CoinIdRole).toString();
        QVERIFY(coins.setManualInputSelection(true));
        QVERIFY(coins.toggleCoinSelectionById(id)); fixture.finish();
        const auto selected = coins.selectionSnapshot(); QCOMPARE(selected->size(), size_t{1});
        fixture.wallet->setReadFailure(true); coins.update(); fixture.finish();
        QCOMPARE(coins.rowCount(), 3); QCOMPARE(coins.coinCount(), 3);
        QCOMPARE(coins.selectionSnapshot(), selected); QCOMPARE(coins.selectedCoinsCount(), 1);
        QVERIFY(!coins.readError().isEmpty());
    }
    void acceptedSelectionRetainsOriginalOutcomeAfterObserverLoss() {
        ManualCoins fixture{3};
        const COutPoint original{Txid{}, 0};
        QVERIFY(fixture.model->requestSelectionDelta(original, true));
        const auto owner = fixture.model->selectionOperation(); QVERIFY(owner);
        fixture.model->closeAdmission(); fixture.model.reset(); fixture.opening->closeAdmission();
        fixture.commands.drain();
        QVERIFY(owner->result()); QVERIFY(owner->result()->value);
        QVERIFY(owner->result()->value->selection->contains(original));
        QCOMPARE(owner->result()->value->revision, quint64{1});
    }
    void consumedLockCompletionCannotRetainItsOwnCleanupLease() {
        ManualCoins fixture;
        auto* coins=fixture.model.get();
        const auto id=coins->data(coins->index(0),CoinsListModel::CoinIdRole).toString();
        QVERIFY(coins->setCoinsLocked({id},true));
        std::weak_ptr<const execution::OwnedCommand<CoinsListModel::LockOutcome>> completed;
        {
            const auto owner=coins->lockOperation();QVERIFY(owner);completed=owner;
            fixture.finish();
            const auto result=owner->result();QVERIFY(result);QVERIFY(result->value);
            QCOMPARE(result->value->requested,size_t{1});QCOMPARE(result->value->changed,size_t{1});
            QVERIFY(!result->value->failed);QVERIFY(!result->value->uncertain);
            QCOMPARE(fixture.wallet->lockCalls(),std::vector<bool>{true});
        }
        // The GUI may retain the actual outcome until close. Afterward no
        // persistent completion may keep its own result/selection lease alive.
        QVERIFY(!completed.expired());
        coins->closeAdmission();fixture.model.reset();
        fixture.opening->retire();fixture.commands.scheduler->closeAdmission();
        fixture.finish();fixture.retirement.drain();fixture.commands.drain();
        QVERIFY(completed.expired());
        QCOMPARE(fixture.wallet->lockCalls(),std::vector<bool>{true});
        QCOMPARE(fixture.commands.scheduler->pending(),size_t{0});
        QCOMPARE(fixture.retirement.scheduler->pending(),size_t{0});
    }
    void admittedLockBatchDrainsInBoundedChunksAfterObserverClose() {
        ManualCoins fixture{97}; auto& coins = *fixture.model;
        QStringList ids; for (int i = 0; i < coins.rowCount(); ++i) ids.push_back(coins.data(coins.index(i), CoinsListModel::CoinIdRole).toString());
        QVERIFY(coins.setCoinsLocked(ids, true));
        const auto owner = coins.lockOperation(); QVERIFY(owner);
        QCOMPARE(fixture.wallet->lockCalls().size(), size_t{0});
        QVERIFY(fixture.commands.step(execution::Lane::Domain));
        QVERIFY(fixture.commands.step(execution::Lane::Core));
        QCOMPARE(fixture.wallet->lockCalls().size(), size_t{32});
        fixture.model->closeAdmission(); fixture.model.reset(); fixture.opening->closeAdmission();
        fixture.commands.drain();
        QCOMPARE(fixture.wallet->lockCalls().size(), size_t{97});
        QVERIFY(owner->result()); QVERIFY(owner->result()->value);
        QCOMPARE(owner->result()->value->changed, size_t{97});
        QVERIFY(!owner->result()->value->uncertain);
    }
    void rejectedContinuationRetainsActualPartialLockOutcome() {
        ManualCoins fixture{97}; auto& coins = *fixture.model;
        QStringList ids; for (int i = 0; i < coins.rowCount(); ++i) ids.push_back(coins.data(coins.index(i), CoinsListModel::CoinIdRole).toString());
        fixture.commands.rejectCoreNumber(fixture.commands.coreSubmissions() + 2);
        QVERIFY(coins.setCoinsLocked(ids, true)); const auto owner = coins.lockOperation();
        fixture.finish();
        QVERIFY(owner->result()); QVERIFY(owner->result()->value);
        QCOMPARE(owner->result()->value->changed, size_t{32}); QVERIFY(owner->result()->value->failed); QVERIFY(!owner->result()->value->uncertain);
        QCOMPARE(fixture.wallet->lockCalls().size(), size_t{32}); QVERIFY(!coins.lockError().isEmpty());
    }
    void oppositeLockIntentsAreNotSuppressedByTheCachedRow() {
        auto wallet = std::make_unique<CoinWallet>();
        auto* source = wallet.get();
        wallet->add(0, 1000, 1700000000, 1, "First");
        WalletQmlModel model{std::move(wallet)};
        auto* coins = model.coinsListModel();
        QTRY_COMPARE(coins->coinCount(), 1);
        const auto id = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();
        QVERIFY(!coins->data(coins->index(0), CoinsListModel::LockedRole).toBool());
        // No GUI publication occurs between these commands. Both refer to the
        // same initially unlocked cached row, regardless of native timing.
        QVERIFY(coins->setCoinsLocked({id}, true));
        QVERIFY(coins->setCoinsLocked({id}, false));
        QTRY_VERIFY(!coins->lockPending());
        QVERIFY2(coins->lockError().isEmpty(), qPrintable(coins->lockError()));
        QCOMPARE(source->lockCalls(), std::vector<bool>({true, false}));
        QVERIFY(source->read_thread.load() != QThread::currentThread());
        QVERIFY(source->lock_thread.load() != QThread::currentThread());
        QVERIFY(source->coin_lookup_thread.load());
        QVERIFY(source->coin_lookup_thread.load() != QThread::currentThread());
        QTRY_VERIFY(!coins->data(coins->index(0), CoinsListModel::LockedRole).toBool());
    }
    void stableIdentitySurvivesReorderAndRejectsRemovedCoin() {
        auto wallet = std::make_unique<CoinWallet>();
        auto* source = wallet.get();
        wallet->add(0, 1000, 1700000000, 1, "First");
        wallet->add(1, 2000, 1700000010, 2, "Second");
        WalletQmlModel model{std::move(wallet)};
        auto* coins = model.coinsListModel();
        QTRY_COMPARE(coins->coinCount(), 2);
        coins->setSortBy("amount");
        coins->setSortDescending(false);
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::AmountSatoshiRole).toLongLong(), 1000);
        const QString coin_id = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();

        coins->setSortDescending(true);
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::AmountSatoshiRole).toLongLong(), 2000);
        QVERIFY(coins->setManualInputSelection(true));
        QVERIFY(coins->toggleCoinSelectionById(coin_id));
        QTRY_COMPARE(coins->totalSelectedSatoshi(), 1000);

        source->clearCoins();
        coins->update();
        QTRY_COMPARE(coins->coinCount(), 0);
        QVERIFY(!coins->toggleCoinSelectionById(coin_id));
        QTRY_COMPARE(coins->selectedCoinsCount(), 1);
        QVERIFY(coins->selectionReconciliationRequired());
        QVERIFY(coins->setCoinsLocked({coin_id}, true));
        const auto stale_lock = coins->lockOperation(); QVERIFY(stale_lock);
        QTRY_VERIFY(!coins->lockPending());
        QVERIFY(stale_lock->result()); QVERIFY(stale_lock->result()->value);
        QVERIFY(stale_lock->result()->value->failed);
        QCOMPARE(stale_lock->result()->value->requested, size_t{1});
        QCOMPARE(stale_lock->result()->value->items.size(), size_t{1});
        QCOMPARE(stale_lock->result()->value->items.front().state, CoinsListModel::LockItemOutcome::State::Failed);
        QVERIFY(source->lockCalls().empty());
        QVERIFY(coins->setManualInputSelection(false));
        QTRY_COMPARE(coins->selectedCoinsCount(), 0);
        QVERIFY(!coins->selectionReconciliationRequired());
    }

    void filteringKeepsHiddenSelectionAndCancelRestoresInputs() {
        auto wallet = std::make_unique<CoinWallet>();
        wallet->add(0, 1000, 1700000000, 1, "Rent");
        wallet->add(1, 2000, 1700000010, 2, "Savings");
        WalletQmlModel model{std::move(wallet)};
        auto* coins = model.coinsListModel();
        QTRY_COMPARE(coins->coinCount(), 2);
        coins->setSortDescending(false);
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::AddressRole).toString(), QString::fromStdString(EncodeDestination(CoinDestination(1))));
        QVERIFY(coins->setManualInputSelection(true));
        coins->toggleCoinSelection(0);
        QTRY_COMPARE(coins->totalSelectedSatoshi(), 1000);
        coins->beginSelection();
        coins->setSearchText("SAV");
        QTRY_COMPARE(coins->rowCount(), 1);
        QTRY_COMPARE(coins->totalSelectedSatoshi(), 1000);
        coins->toggleCoinSelection(0);
        QTRY_COMPARE(coins->totalSelectedSatoshi(), 3000);

        coins->setSearchText(QString::fromStdString(EncodeDestination(CoinDestination(1))));
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::AddressRole).toString(), QString::fromStdString(EncodeDestination(CoinDestination(1))));
        QTRY_COMPARE(coins->rowCount(), 1);
        const QString coin_id = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();
        coins->setSearchText(coin_id.left(12));
        QTRY_COMPARE(coins->rowCount(), 2); // The mock outputs share the same transaction id.
        coins->cancelSelection();
        QTRY_COMPARE(coins->totalSelectedSatoshi(), 1000);
        coins->setSearchText("SAV");
        QTRY_VERIFY(!coins->loading());
        QTRY_COMPARE(coins->rowCount(), 1);
        coins->beginSelection();
        coins->toggleCoinSelection(0);
        coins->applySelection();
        coins->cancelSelection();
        QTRY_COMPARE(coins->totalSelectedSatoshi(), 3000);
    }
    void sortingGroupingAndLocksUseStableCoinIdentity() {
        auto wallet = std::make_unique<CoinWallet>();
        auto* source = wallet.get();
        wallet->add(0, 3000, 1700000000, 1, "Zulu");
        wallet->add(1, 1000, 1700000010, 1, "Zulu");
        wallet->add(2, 2000, 1700000020, 2, "Alpha");
        WalletQmlModel model{std::move(wallet)};
        auto* coins = model.coinsListModel();
        QTRY_COMPARE(coins->coinCount(), 3);
        coins->setGroupBy("date");
        coins->setSortBy("amount"); coins->setSortDescending(false);
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::AmountSatoshiRole).toLongLong(), 1000);
        coins->setSortDescending(true);
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::AmountSatoshiRole).toLongLong(), 3000);
        coins->setSortDescending(false);
        coins->setSortBy("label");
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::LabelRole).toString(), QString("Alpha"));
        coins->setGroupBy("address");
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::GroupRole), coins->data(coins->index(0), CoinsListModel::AddressRole));
        QTRY_VERIFY(coins->data(coins->index(0), CoinsListModel::GroupFirstRole).toBool());
        QVERIFY(coins->data(coins->index(0), CoinsListModel::GroupLastRole).toBool());
        QVERIFY(coins->data(coins->index(1), CoinsListModel::GroupFirstRole).toBool());
        QVERIFY(!coins->data(coins->index(1), CoinsListModel::GroupLastRole).toBool());
        QVERIFY(coins->data(coins->index(2), CoinsListModel::GroupLastRole).toBool());
        coins->toggleCoinSelection(0);
        const auto id = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();
        QVERIFY(coins->setCoinsLocked({id}, true));
        QTRY_VERIFY(!coins->lockPending());
        QVERIFY2(coins->lockError().isEmpty(), qPrintable(coins->lockError()));
        QTRY_COMPARE(coins->selectedCoinsCount(), 0);
        coins->setFilter("locked"); QTRY_COMPARE(coins->rowCount(), 1);
        coins->toggleCoinSelection(0); QTRY_COMPARE(coins->selectedCoinsCount(), 1);
        QTRY_COMPARE(coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString(), id);
        QVERIFY(coins->setCoinsLocked({id}, false));
        QTRY_VERIFY(!coins->lockPending());
        QVERIFY2(coins->lockError().isEmpty(), qPrintable(coins->lockError()));
        QTRY_COMPARE(coins->rowCount(), 0);
        coins->setFilter("spendable"); QTRY_COMPARE(coins->rowCount(), 3);
        coins->setMinAmount(1500); QTRY_COMPARE(coins->rowCount(), 2);
        coins->setMaxAmount(2500); QTRY_COMPARE(coins->rowCount(), 1);
        coins->setMinAmount(-1); coins->setMaxAmount(-1); QTRY_COMPARE(coins->rowCount(), 3);
        coins->setGroupBy("date");
        QVERIFY(!coins->data(coins->index(0), CoinsListModel::GroupRole).toString().isEmpty());
        coins->toggleCoinSelection(0);
        source->clearCoins(); coins->update();
        QTRY_COMPARE(coins->coinCount(), 0);
        QTRY_COMPARE(coins->selectedCoinsCount(), 0);
        QTRY_COMPARE(coins->totalSatoshi(), 0);
    }
};
BITCOINQML_REGISTER_QT_TEST(CoinsListModelTests)
#include <test_coinslistmodel.moc>
