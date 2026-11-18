// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletreceipts.h>
#include <qml/backendexecutor.h>
#include <qml/retainedvalue.h>
#include <qml/models/receiptreconciliationmodel.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>

#include <chainparams.h>
#include <key_io.h>
#include <script/solver.h>

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>

namespace {
using namespace walletreceipts;
using execution::Lane;
const QString ADDRESS{QStringLiteral("1BoatSLRHtKNngkdXEeobR76b53LETtpyT")};
const QSet<QString> ADDRESSES{ADDRESS};
interfaces::WalletTx Payment(int tag, CAmount amount)
{
    CMutableTransaction tx;
    tx.nLockTime = tag;
    tx.vout.emplace_back(amount, GetScriptForDestination(DecodeDestination(ADDRESS.toStdString())));
    interfaces::WalletTx result{};
    result.tx = MakeTransactionRef(tx);
    result.txout_is_mine = {true};
    return result;
}
struct Wallet : StubWallet {
    std::map<Txid, interfaces::WalletTx> transactions;
    std::set<Txid> inactive;
    std::set<Txid> confirmed;
    std::vector<Txid> read_ids;
    int reads{0}, scans{0};
    bool fail{false};
    std::optional<int> status_depth;
    bool abandoned{false};
    TransactionChangedFn transaction_changed;
    std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn callback) override {
        transaction_changed = std::move(callback); return {};
    }
    std::string getWalletName() override { return "receipts"; }
    std::set<interfaces::WalletTx> getWalletTxs() override {
        ++scans;
        std::set<interfaces::WalletTx> result;
        for (const auto& [id, tx] : transactions) result.insert(tx);
        return result;
    }
    interfaces::WalletTx getWalletTxDetails(const Txid& id, interfaces::WalletTxStatus& status,
        std::vector<std::string>&, std::vector<std::string>&, bool& mempool, int&) override {
        ++reads;
        read_ids.push_back(id);
        if (fail) throw std::runtime_error("receipt read failure");
        status.depth_in_main_chain = confirmed.contains(id) ? 1 : 0;
        if (status_depth) status.depth_in_main_chain = *status_depth;
        status.is_abandoned = abandoned;
        mempool = !inactive.contains(id);
        const auto tx = transactions.find(id);
        return tx == transactions.end() ? interfaces::WalletTx{} : tx->second;
    }
};
struct Fixture {
    ManualRoutedExecution commands, retirement;
    std::shared_ptr<Wallet> wallet{std::make_shared<Wallet>()};
    std::shared_ptr<walletopening::Session> opening;
    std::shared_ptr<interfaces::Wallet> client;
    std::shared_ptr<Reader> reader;
    std::chrono::steady_clock::time_point now{};
    int notices{0};
    Fixture() {
        opening = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {});
        client = opening->start(wallet).borrower;
        commands.drain();
        reader = std::make_shared<Reader>(opening, [this] { ++notices; }, [this] { return now; });
    }
    ~Fixture() {
        reader->close(); opening->retire(); client.reset();
        commands.drain(); retirement.drain();
        const auto directory = qEnvironmentVariable("QT6_RECEIPT_TRACE_DIR");
        if (!directory.isEmpty()) {
            QJsonArray command_trace, cleanup_trace;
            for (const auto& event : commands.traceSnapshot()) command_trace.append(QString::fromStdString(event));
            for (const auto& event : retirement.traceSnapshot()) cleanup_trace.append(QString::fromStdString(event));
            const QJsonObject trace{{"test", QTest::currentTestFunction()}, {"failed", QTest::currentTestFailed()},
                {"commands", command_trace}, {"retirement", cleanup_trace}, {"reads", wallet->reads}, {"scans", wallet->scans},
                {"virtual_ms", qint64(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count())},
                {"quiescent", commands.quiescent() && retirement.quiescent()}};
            QVERIFY(QDir{}.mkpath(directory));
            QFile file{directory + "/" + QTest::currentTestFunction() + ".json"};
            QVERIFY(file.open(QIODevice::WriteOnly));
            const auto bytes = QJsonDocument{trace}.toJson();
            QCOMPARE(file.write(bytes), qint64(bytes.size()));
        }
    }
    Txid add(int tag, CAmount amount) {
        auto tx = Payment(tag, amount); const auto id = tx.tx->GetHash();
        wallet->transactions[id] = std::move(tx); return id;
    }
    std::optional<Publication> settle() { commands.drain(); return reader->take(); }
    ReceiptReconciliationModel::Ports projectionPorts() {
        ReceiptReconciliationModel::Ports ports;
        ports.opening = opening->identity();
        ports.take = [this] { return reader->take(); };
        ports.retire = [this](auto snapshot) { reader->retire(std::move(snapshot)); };
        ports.current = [this] { return reader->settled(); };
        ports.nativeRevision = [this] { return opening->transactionRevision(); };
        ports.poll = [this] { reader->poll(); };
        ports.retry = [this] { reader->retry(); };
        ports.acknowledge = [this] { opening->acknowledgeTransactions(); };
        return ports;
    }
};
}

class WalletReceiptsTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { SelectParams(ChainType::MAIN); }
    void missingRetirementPortRefusesReceiptConstructionAndFurtherReads()
    {
        Fixture f; auto ports = f.projectionPorts(); int captures{0};
        ports.retire = {};
        ports.take = [&]() -> std::optional<Publication> { ++captures; return {}; };
        ReceiptReconciliationModel model{std::move(ports)}; RequestReceiptModel request;
        request.setReader(&model); request.setSource("17", ADDRESS, false, true);
        model.consume(); model.poll(); model.retry(); f.commands.drain();
        QCOMPARE(model.phase(), ReceiptReconciliationModel::Rejected); QVERIFY(!model.errorText().isEmpty());
        QVERIFY(!request.fresh()); QCOMPARE(request.receiptState(), RequestReceiptModel::Unknown);
        QCOMPARE(captures, 0); QCOMPARE(f.wallet->reads, 0); QCOMPARE(f.wallet->scans, 0);
    }
    void currentCreditProjectionKeepsPositiveUnderpaymentSeparateFromHistoricalUse()
    {
        Fixture f; ReceiptReconciliationModel model{f.projectionPorts()}; RequestReceiptModel request;
        request.setReader(&model); request.setSource("17", ADDRESS, false, true);
        QCOMPARE(request.receiptState(), RequestReceiptModel::Unknown); QVERIFY(!request.fresh());
        f.add(1, 546); f.reader->reload(ADDRESSES); f.commands.drain(); model.consume();
        QVERIFY(request.fresh()); QCOMPARE(request.receivedAmountSatoshi(), qint64{546});
        QCOMPARE(request.receiptState(), RequestReceiptModel::Received); QVERIFY(request.historicallyUsed());
        QCOMPARE(request.requestId(), QStringLiteral("17")); QCOMPARE(f.wallet->scans, 1);
        // The projection reports actual credit, not whether a 1 BTC invoice
        // is fulfilled; positive underpayment intentionally remains observed.
        QCOMPARE(model.amount(ADDRESS), CAmount{546});
    }
    void cancelledPaymentReturnsToAwaitingWithoutErasingHistoricalUse()
    {
        Fixture f; ReceiptReconciliationModel model{f.projectionPorts()}; RequestReceiptModel request;
        request.setReader(&model); request.setSource("17", ADDRESS, false, true);
        const auto id = f.add(1, 1000); f.reader->reload(ADDRESSES); f.commands.drain(); model.consume();
        QVERIFY(request.fresh()); QCOMPARE(request.receivedAmountSatoshi(), qint64{1000});
        f.wallet->inactive.insert(id); model.poll(); f.commands.drain(); model.consume();
        QVERIFY(request.fresh()); QCOMPARE(request.receivedAmountSatoshi(), qint64{0});
        QCOMPARE(request.receiptState(), RequestReceiptModel::Awaiting); QVERIFY(request.historicallyUsed());
        QCOMPARE(request.requestId(), QStringLiteral("17")); QCOMPARE(f.wallet->scans, 1);
    }
    void changedNoticeReconcilesActualLiveStateEvenWhenLastNoticeSaysDeleted()
    {
        Fixture f; QStringList notified; auto ports = f.projectionPorts();
        ports.transactions = [&](const auto& changes) {
            for (const auto& [id, change] : changes) { Q_UNUSED(change); notified.push_back(QString::fromStdString(id.ToString())); }
        };
        ReceiptReconciliationModel model{std::move(ports)}; RequestReceiptModel request;
        request.setReader(&model); request.setSource("17", ADDRESS, false, true);
        const auto id = f.add(1, 1000); f.reader->reload(ADDRESSES); f.commands.drain(); model.consume();
        f.reader->changed({{id, CT_UPDATED}}, ADDRESSES);
        f.reader->changed({{id, CT_DELETED}}, ADDRESSES);
        f.commands.drain(); model.consume();
        QVERIFY(request.fresh()); QCOMPARE(request.receivedAmountSatoshi(), qint64{1000});
        QCOMPARE(request.receiptState(), RequestReceiptModel::Received); QCOMPARE(f.wallet->scans, 1);
        QCOMPARE(notified, QStringList{QString::fromStdString(id.ToString())});
    }
    void deletedNoticeRemovesCreditOnlyWhenOriginalWalletReadConfirmsAbsence()
    {
        Fixture f; ReceiptReconciliationModel model{f.projectionPorts()}; RequestReceiptModel request;
        request.setReader(&model); request.setSource("17", ADDRESS, false, true);
        const auto id = f.add(1, 1000); f.reader->reload(ADDRESSES); f.commands.drain(); model.consume();
        QCOMPARE(request.receivedAmountSatoshi(), qint64{1000}); QVERIFY(request.historicallyUsed());
        f.wallet->transactions.erase(id); const int before = f.wallet->reads;
        f.reader->changed({{id, CT_DELETED}}, ADDRESSES); f.commands.drain(); model.consume();
        QVERIFY(f.wallet->reads > before); QVERIFY(request.fresh());
        QCOMPARE(request.receivedAmountSatoshi(), qint64{0}); QCOMPARE(request.receiptState(), RequestReceiptModel::Awaiting);
        QVERIFY(request.historicallyUsed()); QCOMPARE(f.wallet->scans, 1);
    }
    void pendingObservationPreservesCreditWithoutClaimingCurrentReadiness()
    {
        Fixture f; ReceiptReconciliationModel model{f.projectionPorts()}; RequestReceiptModel request;
        request.setReader(&model); request.setSource("17", ADDRESS, false, true);
        const auto id = f.add(1, 100); f.reader->reload(ADDRESSES); f.commands.drain(); model.consume();
        f.reader->changed({{id, CT_UPDATED}}, ADDRESSES); model.invalidate();
        QVERIFY(model.pending()); QVERIFY(!request.fresh()); QCOMPARE(request.receiptState(), RequestReceiptModel::Unknown);
        QCOMPARE(request.receivedAmountSatoshi(), qint64{100}); QVERIFY(request.historicallyUsed());
        f.commands.drain(); model.consume(); QVERIFY(request.fresh());
        QCOMPARE(request.receivedAmountSatoshi(), qint64{100});
    }
    void failedReceiptReadRetainsTheLastCreditAndRecoversWithManualClock()
    {
        Fixture f; ReceiptReconciliationModel model{f.projectionPorts()}; RequestReceiptModel request;
        request.setReader(&model); request.setSource("17", ADDRESS, false, true);
        f.add(1, 100); f.reader->reload(ADDRESSES); f.commands.drain(); model.consume();
        f.wallet->fail = true; model.poll(); f.commands.drain(); model.consume();
        QCOMPARE(model.phase(), ReceiptReconciliationModel::Failed); QVERIFY(!request.fresh());
        QCOMPARE(request.receivedAmountSatoshi(), qint64{100}); QVERIFY(!request.errorText().isEmpty());
        const auto failed_reads = f.wallet->reads;
        request.retry(); f.commands.drain(); model.consume(); QCOMPARE(f.wallet->reads, failed_reads);
        f.now += std::chrono::seconds{1}; f.wallet->fail = false;
        request.retry(); f.commands.drain(); model.consume();
        QVERIFY(request.fresh()); QVERIFY(request.errorText().isEmpty());
        QCOMPARE(request.receivedAmountSatoshi(), qint64{100}); QCOMPARE(f.wallet->scans, 2);
    }
    void requestProjectionReplacementNeverCarriesThePriorAddressCredit()
    {
        Fixture f; ReceiptReconciliationModel model{f.projectionPorts()}; RequestReceiptModel request;
        request.setReader(&model); request.setSource("17", ADDRESS, false, true);
        f.add(1, 100); f.reader->reload(ADDRESSES); f.commands.drain(); model.consume();
        const auto reads = f.wallet->reads;
        request.setSource("29", "another-original-address", false, true);
        QCOMPARE(request.requestId(), QStringLiteral("29")); QCOMPARE(request.address(), QStringLiteral("another-original-address"));
        QCOMPARE(request.receivedAmountSatoshi(), qint64{0}); QVERIFY(!request.historicallyUsed());
        QCOMPARE(f.wallet->reads, reads); // No action-level wallet rescan or snapshot copy.
        request.setSource("17", ADDRESS, true, true);
        QCOMPARE(request.receivedAmountSatoshi(), qint64{100}); QVERIFY(request.historicallyUsed());
    }
    void retiredProjectionCannotPollOrRetryTheOriginalOpening()
    {
        Fixture f; ReceiptReconciliationModel model{f.projectionPorts()}; RequestReceiptModel request;
        request.setReader(&model); request.setSource("17", ADDRESS, true, true);
        f.add(1, 100); f.reader->reload(ADDRESSES); f.commands.drain(); model.consume();
        const auto reads = f.wallet->reads; model.close();
        QCOMPARE(request.phase(), ReceiptReconciliationModel::Closed); QVERIFY(!request.fresh());
        QVERIFY(request.historicallyUsed()); QCOMPARE(request.receiptState(), RequestReceiptModel::Unknown);
        model.poll(); request.retry(); model.consume(); f.commands.drain(); QCOMPARE(f.wallet->reads, reads);
    }
    void projectionCloseDuringChangedStopsOriginalTransactionPublication()
    {
        Fixture f; int transactions{0}; auto ports = f.projectionPorts();
        ports.transactions = [&](const auto& changes) { transactions += int(changes.size()); };
        ReceiptReconciliationModel model{std::move(ports)};
        const auto id = f.add(1, 100); f.reader->changed({{id, CT_NEW}}, ADDRESSES); f.commands.drain();
        connect(&model, &ReceiptReconciliationModel::changed, &model, [&] { model.close(); });
        model.consume(); QCOMPARE(model.phase(), ReceiptReconciliationModel::Closed);
        QCOMPARE(transactions, 0); QVERIFY(!model.applying()); f.commands.drain();
    }
    void destroyedReceiptOwnerLeavesTheOriginalRequestExplicitlyStale()
    {
        Fixture f; RequestReceiptModel request;
        auto model = std::make_unique<ReceiptReconciliationModel>(f.projectionPorts());
        request.setReader(model.get()); request.setSource("17", ADDRESS, true, true);
        f.add(1, 100); f.reader->reload(ADDRESSES); f.commands.drain(); model->consume();
        QVERIFY(request.fresh()); model.reset(); QVERIFY(!request.fresh());
        QCOMPARE(request.phase(), ReceiptReconciliationModel::Closed);
        QCOMPARE(request.receiptState(), RequestReceiptModel::Unknown); QVERIFY(request.historicallyUsed());
        QCOMPARE(request.requestId(), QStringLiteral("17")); f.commands.drain();
    }
    void sameAddressInAnotherOpeningCannotRetargetAnOriginalReceiptFacade()
    {
        Fixture first, successor;
        ReceiptReconciliationModel original{first.projectionPorts()}, newer{successor.projectionPorts()};
        RequestReceiptModel request;
        request.setReader(&original); request.setSource("17", ADDRESS, false, true);
        first.add(1, 100); successor.add(2, 999);
        first.reader->reload(ADDRESSES); successor.reader->reload(ADDRESSES);
        first.commands.drain(); successor.commands.drain(); original.consume(); newer.consume();
        QVERIFY(request.fresh()); QCOMPARE(request.receivedAmountSatoshi(), qint64{100});
        request.setReader(&newer); QCOMPARE(request.receivedAmountSatoshi(), qint64{100});
        original.close(); request.setReader(&newer); QVERIFY(!request.fresh());
        QCOMPARE(request.phase(), ReceiptReconciliationModel::Closed); QCOMPARE(request.requestId(), QStringLiteral("17"));
        QVERIFY(newer.fresh()); QCOMPARE(newer.amount(ADDRESS), CAmount{999});
    }
    void realWorkersReconcileAndDrainIndependentlyOfAClosedReceiptView()
    {
        struct State {
            QSemaphore entered, release;
            std::atomic_bool hold{false};
            std::atomic_int metadata{0}, notices{0};
            std::atomic<QThread*> read{nullptr}, outcome{nullptr}, destroyed{nullptr};
        };
        struct Native final : Wallet {
            explicit Native(std::shared_ptr<State> state) : state(std::move(state)) {}
            ~Native() override { state->destroyed = QThread::currentThread(); }
            interfaces::WalletTx getWalletTxDetails(const Txid& id, interfaces::WalletTxStatus& status,
                std::vector<std::string>& order, std::vector<std::string>& forms, bool& mempool, int& blocks) override {
                state->read = QThread::currentThread();
                if (state->hold.exchange(false)) { state->entered.release(); state->release.acquire(); }
                return Wallet::getWalletTxDetails(id, status, order, forms, mempool, blocks);
            }
            const std::shared_ptr<State> state;
        };
        const auto state = std::make_shared<State>();
        BackendExecutor commands, retirement;
        const auto unblock = qScopeGuard([state] { state->release.release(); });
        auto native = std::make_shared<Native>(state);
        auto payment = Payment(1, 1000); const auto id = payment.tx->GetHash(); native->transactions.emplace(id, std::move(payment));
        auto opening = std::make_shared<walletopening::Session>(commands.scheduler(), retirement.scheduler(),
            std::make_shared<std::atomic_bool>(false), [state] { ++state->metadata; });
        auto client = opening->start(native).borrower;
        QTRY_VERIFY(state->metadata.load() > 0);
        auto reader = std::make_shared<Reader>(opening, [state] { ++state->notices; });
        auto disposal = execution::RetainValues<std::shared_ptr<Reader>>(retirement.scheduler(), [&] {
            return std::make_unique<std::shared_ptr<Reader>>(reader);
        }); QVERIFY(disposal);
        QVERIFY(reader->onSnapshot([state](auto) { state->outcome = QThread::currentThread(); }));
        const std::weak_ptr<Reader> weak = reader;
        ReceiptReconciliationModel::Ports ports;
        ports.opening = opening->identity();
        ports.take = [weak] { const auto live = weak.lock(); return live ? live->take() : std::optional<Publication>{}; };
        ports.current = [weak] { const auto live = weak.lock(); return live && live->settled(); };
        ports.nativeRevision = [weak_opening = std::weak_ptr<walletopening::Session>{opening}] {
            const auto live = weak_opening.lock(); return live ? live->transactionRevision() : uint64_t{0};
        };
        ports.retire = [weak](auto value) { if (const auto live = weak.lock()) live->retire(std::move(value)); };
        ports.poll = [weak] { if (const auto live = weak.lock()) live->poll(); };
        ReceiptReconciliationModel model{std::move(ports)}; RequestReceiptModel request;
        request.setReader(&model); request.setSource("17", ADDRESS, false, true);
        reader->reload(ADDRESSES); QTRY_VERIFY(reader->settled());
        QVERIFY(!request.fresh()); QCOMPARE(request.receivedAmountSatoshi(), qint64{0});
        model.consume(); QVERIFY(request.fresh()); QCOMPARE(request.receivedAmountSatoshi(), qint64{1000});
        QVERIFY(state->read.load() != qApp->thread()); QVERIFY(state->outcome.load() && state->outcome.load() != qApp->thread());
        state->hold = true; model.poll();
        const bool entered = state->entered.tryAcquire(1, 5000); if (!entered) state->release.release(); QVERIFY(entered);
        bool heartbeat{false}; QMetaObject::invokeMethod(qApp, [&] { heartbeat = true; }, Qt::QueuedConnection);
        QTRY_VERIFY(heartbeat); QVERIFY(!request.fresh()); QCOMPARE(request.receivedAmountSatoshi(), qint64{1000});
        model.close(); reader->close(); opening->retire(); client.reset(); native.reset();
        reader.reset(); disposal.reset(); state->release.release();
        commands.shutdown(); retirement.shutdown(); QTRY_VERIFY(commands.isDrained() && retirement.isDrained());
        QCOMPARE(request.receiptState(), RequestReceiptModel::Unknown); QVERIFY(request.historicallyUsed());
        QVERIFY(state->destroyed.load() && state->destroyed.load() != qApp->thread());
    }
    void largeReceiptProjectionRetiresItsSharedInventoryWithoutAnotherCoreRead()
    {
        Fixture f;
        auto ports = f.projectionPorts();
        auto source = std::make_shared<Snapshot>(); source->opening = f.opening->identity(); source->addresses = ADDRESSES;
        for (int index{0}; index < 10'000; ++index) source->totals.emplace(QString::number(index), index);
        source->totals.emplace(ADDRESS, 546);
        std::optional<Publication> pending{Publication{source, {}, Status::Ready, false, false, 0}};
        const std::weak_ptr<const Snapshot> original = source; source.reset();
        ports.take = [&] { return std::exchange(pending, {}); };
        ports.current = [] { return true; };
        ReceiptReconciliationModel model{std::move(ports)}; model.consume();
        QVERIFY(model.fresh()); QCOMPARE(model.amount(ADDRESS), CAmount{546});
        for (int index{0}; index < 100; ++index) {
            RequestReceiptModel request; request.setReader(&model); request.setSource(QString::number(index + 1), ADDRESS, false, true);
            QCOMPARE(request.receivedAmountSatoshi(), qint64{546}); QVERIFY(request.fresh());
        }
        QCOMPARE(f.wallet->reads, 0); QCOMPARE(f.wallet->scans, 0); QVERIFY(!original.expired());
        model.close(); QVERIFY(!original.expired());
        while (f.commands.step(Lane::Domain)) {}
        QVERIFY(original.expired()); QCOMPARE(f.wallet->reads, 0); QCOMPARE(f.wallet->scans, 0);
    }
    void foreignOpeningReceiptIsRetiredWithoutPublishingIntoTheOriginalRequest()
    {
        Fixture f; auto ports = f.projectionPorts();
        auto source = std::make_shared<Snapshot>(); source->opening = std::make_shared<const walletopening::Identity>();
        source->addresses = ADDRESSES; source->totals.emplace(ADDRESS, 999);
        std::optional<Publication> pending{Publication{source, {}, Status::Ready, false, false, 0}};
        const std::weak_ptr<const Snapshot> foreign = source; source.reset();
        ports.take = [&] { return std::exchange(pending, {}); };
        ports.current = [] { return true; };
        ReceiptReconciliationModel model{std::move(ports)}; RequestReceiptModel request;
        request.setReader(&model); request.setSource("17", ADDRESS, false, true);
        QSignalSpy notices{&model, &ReceiptReconciliationModel::changed}; model.consume();
        QCOMPARE(notices.count(), 0); QCOMPARE(request.receivedAmountSatoshi(), qint64{0}); QVERIFY(!request.fresh());
        while (f.commands.step(Lane::Domain)) {}
        QVERIFY(foreign.expired()); QCOMPARE(f.wallet->reads, 0); QCOMPARE(f.wallet->scans, 0);
    }
    void committedReceiptNotificationEndsTheLegacyPublicationGuard()
    {
        Fixture f; bool applying_on_values{false}, applying_on_commit{true}; int committed{0};
        ReceiptReconciliationModel model{f.projectionPorts()};
        connect(&model, &ReceiptReconciliationModel::changed, &model, [&] { applying_on_values = model.applying(); model.consume(); });
        connect(&model, &ReceiptReconciliationModel::committed, &model, [&] { applying_on_commit = model.applying(); ++committed; });
        f.add(1, 546); f.reader->reload(ADDRESSES); f.commands.drain(); model.consume();
        QVERIFY(applying_on_values); QVERIFY(!applying_on_commit); QCOMPARE(committed, 1);
        QVERIFY(model.fresh()); QVERIFY(!model.pending()); QCOMPARE(model.amount(ADDRESS), CAmount{546});
    }
    void idlePresentationRetirementNeedsNoReceiptRead()
    {
        Fixture f;
        auto snapshot = std::make_shared<const Snapshot>();
        const std::weak_ptr<const Snapshot> old = snapshot;
        f.reader->retire(std::move(snapshot));
        QVERIFY(!old.expired());
        while (f.commands.step(Lane::Domain)) {}
        QVERIFY(old.expired());
        QCOMPARE(f.wallet->scans, 0); QCOMPARE(f.wallet->reads, 0);
        QCOMPARE(f.notices, 0);
    }
    void ledgerReplacesPaymentsWithoutReusingObservedAddresses()
    {
        Ledger ledger;
        const auto a = Txid::FromUint256(uint256{uint8_t{1}});
        ledger.apply({a, {{ADDRESS, 100}}, true, true}, ADDRESSES);
        ledger.apply({a, {{ADDRESS, 150}}, true, true}, ADDRESSES);
        QCOMPARE(ledger.totals().at(ADDRESS), CAmount{150});
        QCOMPARE(ledger.unconfirmed().size(), size_t{1});
        QVERIFY(ledger.unconfirmed().contains(a));
        ledger.apply({a, {{ADDRESS, 150}}, false, false}, ADDRESSES);
        QVERIFY(ledger.totals().empty()); QVERIFY(ledger.unconfirmed().empty());
        QVERIFY(ledger.observed().contains(ADDRESS));
        ledger.clearPayments(); QVERIFY(ledger.observed().contains(ADDRESS));
    }
    void receiptIdSnapshotsKeepOriginalOrder_data()
    {
        QTest::addColumn<bool>("permuted");
        QTest::newRow("ascending") << false;
        QTest::newRow("permuted") << true;
    }
    void transferredPaymentsKeepIndependentOwners_data()
    {
        QTest::addColumn<bool>("populated");
        QTest::newRow("empty") << false; QTest::newRow("two-payments") << true;
    }
    void transferredPaymentsKeepIndependentOwners()
    {
        QFETCH(bool,populated);
        const QString other{QStringLiteral("3J98t1WpEZ73CNmQviecrnyiWrnqRhWNLy")};
        const QSet<QString> watched{ADDRESS,other};
        const auto first=Txid::FromUint256(uint256{uint8_t{1}});
        const auto second=Txid::FromUint256(uint256{uint8_t{2}});
        Ledger current;
        if(populated){current.apply({first,{{ADDRESS,100}},true,true},watched);current.apply({second,{{other,50}},true,false},watched);}
        auto transferred=current.takePayments(); QVERIFY(transferred);
        QVERIFY(current.emptyPayments()); QVERIFY(current.totals().empty());
        QCOMPARE(current.observed().contains(ADDRESS),populated); QCOMPARE(current.observed().contains(other),populated);
        QVERIFY(transferred->observed().empty());
        if(populated){QCOMPARE(transferred->totals().at(ADDRESS),CAmount{100});QCOMPARE(transferred->totals().at(other),CAmount{50});}
        else QVERIFY(transferred->emptyPayments());
        current.apply({first,{{ADDRESS,40}},true,false},watched);
        QCOMPARE(current.totals().at(ADDRESS),CAmount{40}); QVERIFY(current.unconfirmed().empty());
        const auto original_ids=transferred->unconfirmed();
        transferred->erase(first); transferred->erase(Txid::FromUint256(uint256{uint8_t{99}}));
        QCOMPARE(current.totals().at(ADDRESS),CAmount{40});
        QCOMPARE(original_ids.contains(first),populated);
        QVERIFY(!transferred->unconfirmed().contains(first));
        if(populated)QCOMPARE(transferred->totals().at(other),CAmount{50});
        transferred.reset(); current.clearPayments();
        QVERIFY(current.emptyPayments()); QVERIFY(current.observed().contains(ADDRESS));
        QCOMPARE(current.observed().contains(other),populated);
    }
    void publicReceiptReadKeepsStatusAndOutputFactsSeparate_data()
    {
        QTest::addColumn<int>("mode");
        const std::array names{"mempool", "confirmed", "conflicted", "abandoned", "not-in-mempool",
            "replaced", "confirmed-after-replacement", "unowned", "missing-ownership", "zero-output",
            "negative-output", "non-address-output", "same-address-outputs", "missing-transaction", "read-error",
            // #1072: a reorg returns a replaced original to the mempool.
            "replaced-original-back-in-mempool"};
        for(int mode=0;mode<int(names.size());++mode)QTest::newRow(names[mode])<<mode;
    }
    void publicReceiptReadKeepsStatusAndOutputFactsSeparate()
    {
        QFETCH(int,mode);
        Wallet wallet;
        auto record=Payment(100+mode,mode==9?0:mode==10?-1:100);
        if(mode==7)record.txout_is_mine={false};
        if(mode==8)record.txout_is_mine.clear();
        if(mode==11){CMutableTransaction tx{*record.tx};tx.vout[0].scriptPubKey=CScript{}<<OP_TRUE;record.tx=MakeTransactionRef(tx);}
        if(mode==12){CMutableTransaction tx{*record.tx};tx.vout.emplace_back(25,tx.vout[0].scriptPubKey);record.tx=MakeTransactionRef(tx);record.txout_is_mine={true,true};}
        if(mode==5||mode==6||mode==15)record.replaced_by_txid=Txid::FromUint256(uint256{uint8_t{77}});
        const auto id=record.tx->GetHash();
        if(mode!=13)wallet.transactions.emplace(id,record);
        if(mode==1)wallet.status_depth=1;
        if(mode==2)wallet.status_depth=-1;
        if(mode==6)wallet.status_depth=2;
        wallet.abandoned=mode==3;
        if(mode==1||mode==4||mode==5||mode==6)wallet.inactive.insert(id);
        wallet.fail=mode==14;
        if(mode==14){
            bool threw{false};
            try{Read(wallet,id);}catch(const std::runtime_error& error){threw=true;QCOMPARE(std::string{error.what()},std::string{"receipt read failure"});}
            QVERIFY(threw);QCOMPARE(wallet.reads,1);return;
        }
        const auto receipt=Read(wallet,id);
        QCOMPARE(receipt.id,id); QCOMPARE(wallet.reads,1); QCOMPARE(wallet.read_ids,(std::vector<Txid>{id}));
        const bool has_output=mode<=6||mode==12||mode==15;
        QCOMPARE(receipt.outputs.contains(ADDRESS),has_output);
        if(has_output)QCOMPARE(receipt.outputs.at(ADDRESS),mode==12?CAmount{125}:CAmount{100});
        else QVERIFY(receipt.outputs.empty());
        QCOMPARE(receipt.active,mode==0||mode==1||mode==6||mode==12||mode==15);
        QCOMPARE(receipt.unconfirmed,mode==0||mode==12||mode==15);
    }
    void detachedBackendInterestDoesNotRetainOrRewriteItsSnapshot()
    {
        std::vector<std::shared_ptr<const Snapshot>> snapshots;
        Fixture f;
        f.add(1,100);f.reader->reload(ADDRESSES);const auto initial=f.settle();QVERIFY(initial&&initial->status==Status::Ready);
        QVERIFY(f.reader->onSnapshot([&](auto snapshot){snapshots.push_back(std::move(snapshot));}));f.commands.drain();
        QCOMPARE(snapshots.size(),size_t{1});QCOMPARE(snapshots[0]->totals.at(ADDRESS),CAmount{100});
        const auto second=f.add(2,50);f.reader->changed({{second,CT_NEW}},ADDRESSES);const auto update=f.settle();QVERIFY(update&&update->status==Status::Ready);
        QCOMPARE(snapshots.size(),size_t{2});QCOMPARE(snapshots[1]->totals.at(ADDRESS),CAmount{150});
        QVERIFY(f.reader->onSnapshot({}));f.commands.drain();
        const auto third=f.add(3,25);f.reader->changed({{third,CT_NEW}},ADDRESSES);const auto latest=f.settle();
        QVERIFY(latest&&latest->status==Status::Ready);QCOMPARE(latest->snapshot->totals.at(ADDRESS),CAmount{175});
        QCOMPARE(snapshots.size(),size_t{2});QCOMPARE(snapshots[0]->totals.at(ADDRESS),CAmount{100});
        QCOMPARE(snapshots[1]->totals.at(ADDRESS),CAmount{150});
        const auto reads=f.wallet->reads;
        QVERIFY(f.reader->onSnapshot([&](auto snapshot){snapshots.push_back(std::move(snapshot));}));f.commands.drain();
        QCOMPARE(snapshots.size(),size_t{3});QCOMPARE(snapshots[2]->totals.at(ADDRESS),CAmount{175});QCOMPARE(f.wallet->reads,reads);
    }
    void repeatedReadFailuresRespectTheManualBackoffCap()
    {
        Fixture f;const auto id=f.add(1,100);f.wallet->fail=true;
        f.reader->changed({{id,CT_NEW}},ADDRESSES);const auto initial=f.settle();QVERIFY(initial&&initial->status==Status::Failed);
        int expected_reads{1};
        for(const int delay:{1,2,4,8,16,30,30}){
            f.now+=std::chrono::seconds{delay}-std::chrono::milliseconds{1};f.reader->retry();f.commands.drain();
            QCOMPARE(f.wallet->reads,expected_reads);QVERIFY(!f.reader->settled());
            f.now+=std::chrono::milliseconds{1};f.reader->retry();const auto failed=f.settle();
            QVERIFY(failed&&failed->status==Status::Failed);QCOMPARE(f.wallet->reads,++expected_reads);
        }
        f.wallet->fail=false;f.now+=std::chrono::seconds{30};f.reader->retry();const auto recovered=f.settle();
        QVERIFY(recovered&&recovered->status==Status::Ready);QCOMPARE(recovered->snapshot->totals.at(ADDRESS),CAmount{100});
        QCOMPARE(recovered->transactions.at(id),CT_NEW);
        // Recovery reads the full current history and then the retained notice.
        QCOMPARE(f.wallet->reads,expected_reads+2);QCOMPARE(f.wallet->scans,8);
    }
    void receiptIdSnapshotsKeepOriginalOrder()
    {
        QFETCH(bool, permuted);
        constexpr int COUNT{1025};
        std::vector<Txid> expected;
        expected.reserve(COUNT);
        for (int i{0}; i < COUNT; ++i) {
            uint256 bytes;
            bytes.data()[0] = uint8_t(i);
            bytes.data()[1] = uint8_t(i >> 8);
            expected.push_back(Txid::FromUint256(bytes));
        }
        std::sort(expected.begin(), expected.end());
        ReceiptIds current;
        for (int i{0}; i < COUNT; ++i) current.insert(expected[permuted ? (i * 73) % COUNT : i]);
        QCOMPARE(current.size(), size_t{COUNT});
        const auto original = current; // O(1) independent interest in this revision.
        for (int i{COUNT - 1}; i >= 0; i -= 2) {
            current.erase(expected[i]);
            QVERIFY(!current.contains(expected[i]));
            QVERIFY(original.contains(expected[i]));
        }
        const auto collect = [](const ReceiptIds& ids) {
            std::vector<Txid> result;
            result.reserve(ids.size());
            std::optional<Txid> after;
            while (auto next = ids.next(after)) {
                result.push_back(*next);
                after = next;
            }
            return result;
        };
        QCOMPARE(collect(original), expected);
        std::vector<Txid> remaining;
        remaining.reserve(COUNT / 2);
        for (int i{1}; i < COUNT; i += 2) remaining.push_back(expected[i]);
        QCOMPARE(current.size(), remaining.size());
        QCOMPARE(collect(current), remaining);
        current.clear();
        QVERIFY(current.empty());
        QCOMPARE(collect(original), expected);
    }
    void nativeIngressDoesNotDependOnGuiDelivery()
    {
        Fixture f; const auto id = f.add(1, 100);
        f.reader->reload(ADDRESSES); f.settle();
        f.opening->onTransactions([weak = std::weak_ptr<Reader>{f.reader}](uint64_t revision, std::map<Txid, ChangeType> changes, bool rescan) {
            if (const auto owner = weak.lock()) owner->notice(revision, std::move(changes), rescan);
        });
        f.wallet->transaction_changed(id, CT_UPDATED);
        // No Session::take() or event-loop delivery: producer -> Domain -> Core.
        const auto result = f.settle();
        QVERIFY(result && result->status == Status::Ready);
        QCOMPARE(result->snapshot->totals.at(ADDRESS), CAmount{100});
        QCOMPARE(result->transactions.at(id), CT_UPDATED);
        QVERIFY(f.opening->notificationsPending()); // Presentation still unacknowledged.
    }
    void backendConsumerAndReadinessDoNotWaitForGui()
    {
        Fixture f; const auto id = f.add(1, 100);
        int deliveries{0}; std::shared_ptr<const Snapshot> observed;
        QVERIFY(f.reader->onSnapshot([&](auto snapshot) { ++deliveries; observed = std::move(snapshot); }));
        f.opening->onTransactions([weak = std::weak_ptr<Reader>{f.reader}](uint64_t revision, std::map<Txid, ChangeType> changes, bool rescan) {
            if (const auto owner = weak.lock()) owner->notice(revision, std::move(changes), rescan);
        });
        f.reader->reload(ADDRESSES); QVERIFY(!f.reader->settled()); f.commands.drain();
        QVERIFY(f.reader->settled()); QCOMPARE(deliveries, 1); QVERIFY(observed && observed->observed.contains(ADDRESS));
        QCOMPARE(observed->native_revision, uint64_t{0});
        f.wallet->transaction_changed(id, CT_UPDATED);
        // Session ingress invalidates readiness before Domain can deliver its
        // notification to the reader. No GUI mailbox has been consumed.
        QVERIFY(!f.reader->settled()); QCOMPARE(deliveries, 1);
        f.commands.drain(); QVERIFY(f.reader->settled()); QCOMPARE(deliveries, 2);
        QCOMPARE(observed->native_revision, f.opening->transactionRevision());
        QVERIFY(f.opening->notificationsPending()); f.reader->close(); QVERIFY(!f.reader->settled());
    }
    void backendConsumerAttachmentReplaysOnlyCurrentReadySnapshot()
    {
        Fixture f; f.add(1, 100); f.reader->reload(ADDRESSES); f.commands.drain();
        int deliveries{0}; QVERIFY(f.reader->onSnapshot([&](auto) { ++deliveries; }));
        QCOMPARE(deliveries, 0); f.commands.drain(); QCOMPARE(deliveries, 1);
        f.wallet->fail = true; f.reader->poll(); QVERIFY(!f.reader->settled()); f.commands.drain();
        QVERIFY(!f.reader->settled()); QCOMPARE(deliveries, 1);
        QVERIFY(f.reader->onSnapshot([&](auto) { ++deliveries; })); f.commands.drain(); QCOMPARE(deliveries, 1);
    }
    void newerNativeNoticePreventsReadyBeforeReaderIngress()
    {
        Fixture f; const auto id = f.add(1, 100); f.reader->reload(ADDRESSES); f.settle();
        f.opening->onTransactions([weak = std::weak_ptr<Reader>{f.reader}](uint64_t revision, auto changes, bool rescan) {
            if (const auto owner = weak.lock()) owner->notice(revision, std::move(changes), rescan);
        });
        f.commands.drain();
        f.wallet->transaction_changed(id, CT_UPDATED);
        QVERIFY(f.commands.step(Lane::Core));
        while (f.commands.step(Lane::Domain)) {}
        QVERIFY(f.commands.step(Lane::Core)); // receipt captured for notice 1
        f.wallet->transaction_changed(id, CT_UPDATED); // notice 2 awaits Session capture
        while (f.commands.step(Lane::Domain)) {}
        const auto stale = f.reader->take(); QVERIFY(stale);
        QCOMPARE(stale->status, Status::Pending); QVERIFY(!f.reader->settled());
        f.commands.drain(); const auto current = f.reader->take();
        QVERIFY(current && current->status == Status::Ready);
        QCOMPARE(current->snapshot->native_revision, f.opening->transactionRevision());
    }
    void attachingAfterTheNativeNoticeSeedsTheCurrentOpening()
    {
        Fixture f; const auto id = f.add(1, 100);
        f.wallet->transaction_changed(id, CT_NEW); f.commands.drain();
        f.reader->reload(ADDRESSES); f.settle();
        f.opening->onTransactions([weak = std::weak_ptr<Reader>{f.reader}](uint64_t revision, std::map<Txid, ChangeType> changes, bool rescan) {
            if (const auto owner = weak.lock()) owner->notice(revision, std::move(changes), rescan);
        });
        const auto result = f.settle();
        QVERIFY(result && result->status == Status::Ready && result->rescan);
        QCOMPARE(result->snapshot->totals.at(ADDRESS), CAmount{100});
        QCOMPARE(result->transactions.at(id), CT_NEW);
    }
    void admissionReadDomainAndPresentationAreSeparate()
    {
        Fixture f; const auto id = f.add(1, 100);
        f.reader->changed({{id, CT_NEW}}, ADDRESSES);
        QCOMPARE(f.wallet->reads, 0);
        QVERIFY(f.commands.step(Lane::Domain)); // plan on Domain
        QCOMPARE(f.wallet->reads, 0);
        QVERIFY(f.commands.step(Lane::Core));
        QCOMPARE(f.wallet->reads, 1);
        auto pending = f.reader->take();
        QVERIFY(pending && pending->status == Status::Pending && !pending->snapshot);
        auto result = f.settle();
        QVERIFY(result && result->snapshot && result->status == Status::Ready);
        QCOMPARE(result->snapshot->totals.at(ADDRESS), CAmount{100});
        QVERIFY(result->snapshot->opening == f.opening->identity());
        QCOMPARE(result->transactions.at(id), CT_NEW);
    }
    void replacementRechecksTheReceiversUnnotifiedOriginal()
    {
        Fixture f; const auto original = f.add(1, 100);
        f.reader->changed({{original, CT_NEW}}, ADDRESSES); f.settle();
        const auto replacement = f.add(2, 100); f.wallet->inactive.insert(original);
        f.reader->changed({{replacement, CT_NEW}}, ADDRESSES);
        const auto result = f.settle();
        QVERIFY(result && result->status == Status::Ready);
        QCOMPARE(result->snapshot->totals.at(ADDRESS), CAmount{100});
        QCOMPARE(f.wallet->reads, 3); // original, replacement, unnotified original
        QVERIFY(result->snapshot->observed.contains(ADDRESS));
    }
    void replacementNeverPublishesAnAmountBeforeItsOriginalIsRechecked()
    {
        Fixture f; const auto original = f.add(1, 100);
        f.reader->changed({{original, CT_NEW}}, ADDRESSES); f.settle();
        const auto replacement = f.add(2, 100); f.wallet->inactive.insert(original);
        f.reader->changed({{replacement, CT_NEW}}, ADDRESSES);
        QVERIFY(f.commands.step(Lane::Domain)); QVERIFY(f.commands.step(Lane::Core));
        while (f.commands.step(Lane::Domain)) {} // Hold the original's next Core status read.
        QCOMPARE(f.wallet->reads, 2);
        const auto pending = f.reader->take();
        QVERIFY(pending && pending->status == Status::Pending);
        QVERIFY(pending->transactions.empty());
        // Pending is not permission to display a temporarily doubled payment.
        QVERIFY(!pending->snapshot || pending->snapshot->totals.at(ADDRESS) == CAmount{100});
        const auto result = f.settle();
        QVERIFY(result && result->status == Status::Ready);
        QCOMPARE(result->snapshot->totals.at(ADDRESS), CAmount{100});
        QCOMPARE(result->transactions.at(replacement), CT_NEW);
        QCOMPARE(f.wallet->reads, 3);
    }
    void replacementToAnotherRequestRechecksTheUnnotifiedOriginal()
    {
        Fixture f; const auto original = f.add(1, 100);
        const auto other = QString::fromStdString(EncodeDestination(PKHash{uint160{}}));
        const QSet<QString> addresses{ADDRESS, other};
        f.reader->changed({{original, CT_NEW}}, addresses); f.settle();
        CMutableTransaction tx;
        tx.nLockTime = 2;
        tx.vout.emplace_back(100, GetScriptForDestination(DecodeDestination(other.toStdString())));
        interfaces::WalletTx replacement{};
        replacement.tx = MakeTransactionRef(tx); replacement.txout_is_mine = {true};
        const auto id = replacement.tx->GetHash();
        f.wallet->transactions[id] = std::move(replacement); f.wallet->inactive.insert(original);
        f.reader->changed({{id, CT_NEW}}, addresses);
        QVERIFY(f.commands.step(Lane::Domain)); QVERIFY(f.commands.step(Lane::Core));
        while (f.commands.step(Lane::Domain)) {} // Hold the original's recheck.
        QCOMPARE(f.wallet->reads, 2);
        const auto pending = f.reader->take();
        QVERIFY(pending && pending->status == Status::Pending);
        QVERIFY(!pending->snapshot || (pending->snapshot->totals.at(ADDRESS) == CAmount{100} && !pending->snapshot->totals.contains(other)));
        const auto result = f.settle();
        QVERIFY(result && result->status == Status::Ready);
        QVERIFY(!result->snapshot->totals.contains(ADDRESS));
        QCOMPARE(result->snapshot->totals.at(other), CAmount{100});
        QCOMPARE(result->transactions.at(id), CT_NEW);
        QCOMPARE(f.wallet->reads, 3);
    }
    void pollRemovesInactivePaymentAndKeepsItsLock()
    {
        Fixture f; const auto id = f.add(1, 100);
        f.reader->changed({{id, CT_NEW}}, ADDRESSES); f.settle();
        f.wallet->inactive.insert(id); f.reader->poll();
        const auto result = f.settle();
        QVERIFY(result && result->snapshot->totals.empty());
        QVERIFY(result->snapshot->observed.contains(ADDRESS));
    }
    void noticesDuringReadSurviveTheOlderSnapshot()
    {
        Fixture f; const auto old = f.add(1, 100);
        f.reader->reload(ADDRESSES);
        QVERIFY(f.commands.step(Lane::Domain)); QVERIFY(f.commands.step(Lane::Core));
        const auto next = f.add(2, 200);
        f.reader->changed({{next, CT_NEW}}, ADDRESSES);
        const auto result = f.settle();
        QVERIFY(result && result->status == Status::Ready);
        QCOMPARE(result->snapshot->totals.at(ADDRESS), CAmount{300});
        QVERIFY(result->snapshot->observed.contains(ADDRESS));
        QCOMPARE(result->transactions.at(next), CT_NEW);
        Q_UNUSED(old);
    }
    void failuresRetainWorkAndUseTheInjectedBackoff()
    {
        Fixture f; const auto id = f.add(1, 100); f.wallet->fail = true;
        f.reader->changed({{id, CT_NEW}}, ADDRESSES);
        auto failed = f.settle(); QVERIFY(failed && failed->status == Status::Failed);
        QCOMPARE(f.wallet->reads, 1);
        f.now += std::chrono::milliseconds{999}; f.reader->retry(); f.commands.drain();
        QCOMPARE(f.wallet->reads, 1);
        f.now += std::chrono::milliseconds{1}; f.wallet->fail = false; f.reader->retry();
        const auto recovered = f.settle();
        QVERIFY(recovered && recovered->status == Status::Ready);
        QCOMPARE(recovered->snapshot->totals.at(ADDRESS), CAmount{100});
        QCOMPARE(recovered->transactions.at(id), CT_NEW);
    }
    void oldReadyCannotSettleAReloadQueuedBeforePresentation()
    {
        Fixture f; const auto id = f.add(1, 100);
        f.reader->changed({{id, CT_NEW}}, ADDRESSES); f.commands.drain();
        f.reader->reload(ADDRESSES); // Leave its planner held.
        const auto previous = f.reader->take();
        QVERIFY(previous && previous->snapshot && previous->status == Status::Pending);
        const auto current = f.settle();
        QVERIFY(current && current->status == Status::Ready);
        QCOMPARE(current->snapshot->totals.at(ADDRESS), CAmount{100});
    }
    void rejectedPlanningRetainsTheNoticeForAClockedRetry()
    {
        Fixture f; const auto id = f.add(1, 100);
        f.commands.rejectLane(Lane::Domain);
        f.reader->changed({{id, CT_NEW}}, ADDRESSES);
        auto rejected = f.reader->take();
        QVERIFY(rejected && rejected->status == Status::Rejected);
        QCOMPARE(f.wallet->reads, 0);
        f.commands.drain(); QCOMPARE(f.wallet->reads, 0);
        f.now += std::chrono::seconds{1}; f.reader->retry();
        auto result = f.settle();
        QVERIFY(result && result->status == Status::Ready);
        QCOMPARE(result->snapshot->totals.at(ADDRESS), CAmount{100});
        QCOMPARE(result->transactions.at(id), CT_NEW);
    }
    void aBurstUsesBoundedNoticesAndReconcilesTheOverflow()
    {
        Fixture f;
        std::map<Txid, ChangeType> changes;
        for (int i{0}; i < 300; ++i) changes[f.add(i, 1)] = CT_NEW;
        f.reader->changed(std::move(changes), ADDRESSES);
        const auto result = f.settle();
        QVERIFY(result && result->status == Status::Ready && result->rescan);
        QCOMPARE(result->snapshot->totals.at(ADDRESS), CAmount{300});
        QCOMPARE(result->transactions.size(), size_t{256});
        QCOMPARE(f.wallet->scans, 1);
    }
    void fullHistoryYieldsBetweenStatusBatches()
    {
        Fixture f;
        for (int i{0}; i < 300; ++i) f.add(i, 1);
        f.reader->reload(ADDRESSES);
        QVERIFY(f.commands.step(Lane::Domain));
        QVERIFY(f.commands.step(Lane::Core));
        QCOMPARE(f.wallet->scans, 1);
        QCOMPARE(f.wallet->reads, 0); // Bulk capture does not loop over status reads.
        while (f.commands.step(Lane::Domain)) {} // Consume the planner acknowledgement and capture outcome.
        QVERIFY(f.commands.step(Lane::Core));
        QCOMPARE(f.wallet->reads, 128);
        const auto peer_read = std::make_shared<bool>(false);
        QVERIFY(f.opening->read([peer_read](interfaces::Wallet&) { *peer_read = true; return 0; }, [](int) {}));
        while (f.commands.step(Lane::Domain)) {}
        QVERIFY(f.commands.step(Lane::Core));
        QVERIFY(*peer_read); // A same-opening observation progresses before the next batch.
        QCOMPARE(f.wallet->reads, 128);
        const auto pending = f.reader->take();
        QVERIFY(pending && pending->status == Status::Pending && !pending->snapshot);
        const auto result = f.settle();
        QVERIFY(result && result->status == Status::Ready);
        QCOMPARE(result->snapshot->totals.at(ADDRESS), CAmount{300});
        QCOMPARE(f.wallet->reads, 300);
        QCOMPARE(f.wallet->scans, 1);
    }
    void largeUnchangedPollYieldsAndReadsEveryOriginalExactlyOnce()
    {
        Fixture f;
        constexpr int COUNT{1025};
        for (int i{0}; i < COUNT; ++i) f.add(i, 1);
        f.reader->reload(ADDRESSES);
        const auto initial = f.settle();
        QVERIFY(initial && initial->status == Status::Ready && initial->needs_poll);
        QCOMPARE(initial->snapshot->totals.at(ADDRESS), CAmount{COUNT});
        QCOMPARE(f.wallet->scans, 1);
        f.wallet->read_ids.clear();
        f.reader->poll();
        QVERIFY(f.commands.step(Lane::Domain));
        QVERIFY(f.commands.step(Lane::Core));
        QCOMPARE(f.wallet->read_ids.size(), size_t{128});
        const auto peer_ran = std::make_shared<bool>(false);
        QVERIFY(f.opening->read([peer_ran](interfaces::Wallet&) { *peer_ran = true; return 0; }, [](int) {}));
        while (f.commands.step(Lane::Domain)) {}
        QVERIFY(f.commands.step(Lane::Core));
        QVERIFY(*peer_ran);
        QCOMPARE(f.wallet->read_ids.size(), size_t{128});
        const auto pending = f.reader->take();
        QVERIFY(pending && pending->status == Status::Pending && !f.reader->settled());
        const auto result = f.settle();
        QVERIFY(result && result->status == Status::Ready && result->needs_poll);
        QCOMPARE(result->snapshot->totals.at(ADDRESS), CAmount{COUNT});
        QCOMPARE(result->snapshot->observed, initial->snapshot->observed);
        QCOMPARE(f.wallet->scans, 1);
        QCOMPARE(f.wallet->read_ids.size(), size_t{COUNT});
        const std::set<Txid> unique{f.wallet->read_ids.begin(), f.wallet->read_ids.end()};
        QCOMPARE(unique.size(), size_t{COUNT});
        QVERIFY(std::is_sorted(f.wallet->read_ids.begin(), f.wallet->read_ids.end()));
        // A second unchanged cycle neither loses nor duplicates a pending ID.
        f.wallet->read_ids.clear();
        f.reader->poll();
        const auto repeated = f.settle();
        QVERIFY(repeated && repeated->status == Status::Ready);
        QCOMPARE(repeated->snapshot->totals, result->snapshot->totals);
        QCOMPARE(f.wallet->read_ids.size(), size_t{COUNT});
        QCOMPARE(f.wallet->reads, COUNT * 3);
    }
    void noticeDuringLargePollRechecksAnAlreadyConsumedOriginal()
    {
        Fixture f;
        constexpr int COUNT{513};
        for (int i{0}; i < COUNT; ++i) f.add(i, 1);
        f.reader->reload(ADDRESSES);
        const auto initial = f.settle();
        QVERIFY(initial && initial->status == Status::Ready);
        f.wallet->read_ids.clear();
        f.reader->poll();
        QVERIFY(f.commands.step(Lane::Domain));
        QVERIFY(f.commands.step(Lane::Core));
        QCOMPARE(f.wallet->read_ids.size(), size_t{128});
        const auto original = f.wallet->read_ids.front();
        while (f.commands.step(Lane::Domain)) {} // Admit the next original batch.
        // The old ID was already consumed, but this new payment replaces it
        // without an original-ID notification. It must be reconsidered.
        f.wallet->inactive.insert(original);
        const auto replacement = f.add(COUNT + 1, 7);
        f.reader->changed({{replacement, CT_NEW}}, ADDRESSES);
        const auto pending = f.reader->take();
        QVERIFY(pending && pending->status == Status::Pending);
        QVERIFY(!pending->snapshot || pending->snapshot->totals.at(ADDRESS) == CAmount{COUNT});
        const auto result = f.settle();
        QVERIFY(result && result->status == Status::Ready);
        QCOMPARE(result->snapshot->totals.at(ADDRESS), CAmount{COUNT - 1 + 7});
        QCOMPARE(result->transactions.at(replacement), CT_NEW);
        QCOMPARE(std::count(f.wallet->read_ids.begin(), f.wallet->read_ids.end(), original), 2);
        QCOMPARE(std::count(f.wallet->read_ids.begin(), f.wallet->read_ids.end(), replacement), 1);
        QCOMPARE(f.wallet->scans, 1);
        QVERIFY(result->snapshot->observed.contains(ADDRESS));
    }
    void largePollAppliesConfirmationAndConflictWithoutReusingAddresses()
    {
        Fixture f;
        constexpr int COUNT{513};
        for (int i{0}; i < COUNT; ++i) f.add(i, 1);
        f.reader->reload(ADDRESSES);
        const auto initial = f.settle();
        QVERIFY(initial && initial->status == Status::Ready && initial->needs_poll);
        const auto conflicted = f.wallet->transactions.begin()->first;
        f.wallet->inactive.insert(conflicted);
        for (const auto& [id, tx] : f.wallet->transactions) {
            if (id != conflicted) f.wallet->confirmed.insert(id);
        }
        f.wallet->read_ids.clear();
        f.reader->poll();
        // Hold actual Cleanup while all bounded Domain/Core status batches
        // finish. Releasing the old all-unconfirmed root must not block them.
        while (f.commands.step(Lane::Core) || f.commands.step(Lane::Domain)) {}
        const auto result = f.reader->take();
        QVERIFY(result && result->status == Status::Ready && !result->needs_poll);
        QCOMPARE(result->snapshot->totals.at(ADDRESS), CAmount{COUNT - 1});
        QVERIFY(result->snapshot->observed.contains(ADDRESS));
        QCOMPARE(f.wallet->read_ids.size(), size_t{COUNT});
        QVERIFY(f.commands.queuedCountIf([](const auto& turn) { return turn.lane == Lane::Cleanup; }) > 0);
        f.commands.drain();
        QCOMPARE(f.commands.scheduler->pending(), size_t{0});
        f.reader->poll();
        f.commands.drain();
        QCOMPARE(f.wallet->read_ids.size(), size_t{COUNT});
        QVERIFY(f.reader->settled());
        QCOMPARE(f.wallet->scans, 1);
    }
    void closingDuringLargePollCancelsTheUnconsumedSnapshot()
    {
        Fixture f;
        for (int i{0}; i < 513; ++i) f.add(i, 1);
        f.reader->reload(ADDRESSES);
        const auto initial = f.settle();
        QVERIFY(initial && initial->status == Status::Ready);
        f.wallet->read_ids.clear();
        f.reader->poll();
        QVERIFY(f.commands.step(Lane::Domain));
        QVERIFY(f.commands.step(Lane::Core));
        QCOMPARE(f.wallet->read_ids.size(), size_t{128});
        f.reader->close();
        f.opening->retire();
        f.commands.drain();
        QCOMPARE(f.wallet->read_ids.size(), size_t{128});
        QVERIFY(!f.reader->settled());
        const auto pending = f.reader->take();
        QVERIFY(!pending || pending->status != Status::Ready);
    }
    void heldCleanupBoundsReceiptInventoriesAndRetryRecovers()
    {
        Fixture f;
        constexpr int COUNT{1025};
        for (int i{0}; i < COUNT; ++i) f.add(i, 1);
        f.reader->reload(ADDRESSES);
        const auto initial = f.settle();
        QVERIFY(initial && initial->status == Status::Ready);
        const auto original_opening = initial->snapshot->opening;
        const auto run_without_cleanup = [&] {
            while (true) {
                while (f.commands.step(Lane::Domain)) {}
                const auto before = f.wallet->read_ids.size();
                if (!f.commands.step(Lane::Core)) break;
                if (f.wallet->read_ids.size() - before > size_t{128}) return false;
            }
            return true;
        };
        for (int poll{0}; poll < 2; ++poll) {
            f.wallet->read_ids.clear();
            f.reader->poll();
            QVERIFY(run_without_cleanup());
            const auto ready = f.reader->take();
            QVERIFY(ready && ready->status == Status::Ready);
            QCOMPARE(ready->snapshot->opening, original_opening);
            QCOMPARE(ready->snapshot->totals.at(ADDRESS), CAmount{COUNT});
            QCOMPARE(f.wallet->read_ids.size(), size_t{COUNT});
            const std::set<Txid> ids{f.wallet->read_ids.begin(), f.wallet->read_ids.end()};
            QCOMPARE(ids.size(), size_t{COUNT});
        }
        QVERIFY(f.commands.queuedCountIf([](const auto& turn) { return turn.lane == Lane::Cleanup; }) > 0);
        f.wallet->read_ids.clear();
        f.reader->poll();
        QVERIFY(run_without_cleanup());
        const auto rejected = f.reader->take();
        QVERIFY(rejected && rejected->status == Status::Rejected);
        QVERIFY(!f.reader->settled());
        QVERIFY(f.wallet->read_ids.empty());
        QCOMPARE(f.wallet->scans, 1);
        // Only actual queued Native payload retirement returns inventory.
        while (f.commands.step(Lane::Cleanup)) {}
        f.reader->retry();
        f.commands.drain();
        QVERIFY(f.wallet->read_ids.empty());
        f.now += std::chrono::seconds{1};
        f.reader->retry();
        QVERIFY(run_without_cleanup());
        const auto recovered = f.reader->take();
        QVERIFY(recovered && recovered->status == Status::Ready);
        QCOMPARE(recovered->snapshot->opening, original_opening);
        QCOMPARE(recovered->snapshot->totals.at(ADDRESS), CAmount{COUNT});
        QCOMPARE(recovered->snapshot->observed, initial->snapshot->observed);
        QCOMPARE(f.wallet->scans, 2);
        QCOMPARE(f.wallet->read_ids.size(), size_t{COUNT});
        const std::set<Txid> ids{f.wallet->read_ids.begin(), f.wallet->read_ids.end()};
        QCOMPARE(ids.size(), size_t{COUNT});
        f.reader->close();
        f.opening->retire();
        f.client.reset();
        f.commands.drain();
        f.retirement.drain();
        QVERIFY(f.commands.quiescent());
        QVERIFY(f.retirement.quiescent());
    }
    void rejectedLargePollRetriesOriginalOpeningWithoutLosingPayments()
    {
        Fixture f;
        constexpr int COUNT{1025};
        for (int i{0}; i < COUNT; ++i) f.add(i, 1);
        f.reader->reload(ADDRESSES);
        const auto initial = f.settle();
        QVERIFY(initial && initial->status == Status::Ready);
        QCOMPARE(initial->snapshot->totals.at(ADDRESS), CAmount{COUNT});
        const auto original_opening = initial->snapshot->opening;
        f.wallet->read_ids.clear();
        f.reader->poll();
        QVERIFY(f.commands.step(Lane::Domain));
        QVERIFY(f.commands.step(Lane::Core));
        QCOMPARE(f.wallet->read_ids.size(), size_t{128});
        f.commands.rejectLane(Lane::Core);
        while (f.commands.step(Lane::Domain)) {}
        const auto rejected = f.reader->take();
        QVERIFY(rejected && rejected->status == Status::Rejected);
        QVERIFY(!f.reader->settled());
        QCOMPARE(f.wallet->read_ids.size(), size_t{128});
        QCOMPARE(f.wallet->scans, 1);
        // Rejection cannot silently consume the remaining original interest.
        // Before its exact retry deadline, no replacement capture/read runs.
        f.reader->retry();
        f.commands.drain();
        QCOMPARE(f.wallet->read_ids.size(), size_t{128});
        QCOMPARE(f.wallet->scans, 1);
        f.wallet->read_ids.clear();
        f.now += std::chrono::seconds{1};
        f.reader->retry();
        while (true) {
            while (f.commands.step(Lane::Domain)) {}
            const auto before = f.wallet->read_ids.size();
            if (!f.commands.step(Lane::Core)) break;
            QVERIFY(f.wallet->read_ids.size() - before <= size_t{128});
        }
        f.commands.drain();
        const auto ready = f.reader->take();
        QVERIFY(ready && ready->status == Status::Ready && ready->snapshot);
        QCOMPARE(ready->snapshot->opening, original_opening);
        QCOMPARE(ready->snapshot->totals.at(ADDRESS), CAmount{COUNT});
        QCOMPARE(ready->snapshot->observed, initial->snapshot->observed);
        QCOMPARE(f.wallet->scans, 2);
        const std::set<Txid> actual{f.wallet->read_ids.begin(), f.wallet->read_ids.end()};
        QCOMPARE(actual.size(), size_t{COUNT});
        for (const auto& [id, transaction] : f.wallet->transactions) {
            QVERIFY(actual.contains(id));
        }
        // Original snapshots and accepted reads must not retain an idle root
        // lease after explicit close of the still-live Reader.
        f.reader->close();
        f.opening->retire();
        f.client.reset();
        f.commands.drain();
        f.retirement.drain();
        QVERIFY(f.commands.quiescent());
        QVERIFY(f.retirement.quiescent());
    }
    void closingBetweenHistoryBatchesCancelsTheRemainder()
    {
        Fixture f;
        for (int i{0}; i < 300; ++i) f.add(i, 1);
        f.reader->reload(ADDRESSES);
        QVERIFY(f.commands.step(Lane::Domain)); QVERIFY(f.commands.step(Lane::Core));
        while (f.commands.step(Lane::Domain)) {}
        QVERIFY(f.commands.step(Lane::Core));
        while (f.commands.step(Lane::Domain)) {} // First reduction queues the second batch.
        QCOMPARE(f.wallet->reads, 128);
        f.reader->close(); f.opening->retire(); f.commands.drain();
        QCOMPARE(f.wallet->reads, 128);
        const auto result = f.reader->take();
        QVERIFY(!result || !result->snapshot);
    }
    void rejectedHistoryContinuationRebuildsWithoutDoubleCounting()
    {
        Fixture f;
        for (int i{0}; i < 300; ++i) f.add(i, 1);
        f.reader->reload(ADDRESSES);
        QVERIFY(f.commands.step(Lane::Domain)); QVERIFY(f.commands.step(Lane::Core));
        while (f.commands.step(Lane::Domain)) {}
        QVERIFY(f.commands.step(Lane::Core));
        f.commands.rejectLane(Lane::Core);
        QVERIFY(f.commands.step(Lane::Domain));
        const auto rejected = f.reader->take();
        QVERIFY(rejected && rejected->status == Status::Rejected);
        QCOMPARE(f.wallet->reads, 128);
        f.now += std::chrono::seconds{1}; f.reader->retry();
        const auto result = f.settle();
        QVERIFY(result && result->status == Status::Ready);
        QCOMPARE(result->snapshot->totals.at(ADDRESS), CAmount{300});
        QCOMPARE(f.wallet->scans, 2);
        QCOMPARE(f.wallet->reads, 428);
    }
    void admittedResultRetainsNativeBorrowerThroughDomainConsumption()
    {
        Fixture f; const auto id = f.add(1, 100);
        f.reader->changed({{id, CT_NEW}}, ADDRESSES);
        QVERIFY(f.commands.step(Lane::Domain)); QVERIFY(f.commands.step(Lane::Core));
        f.reader->close(); f.opening->retire(); f.client.reset();
        QVERIFY(f.retirement.queuedIdle());
        QCOMPARE(f.retirement.scheduler->pending(), size_t{2}); // cleanup reservation + actual borrower
        f.commands.drain();
        QCOMPARE(f.retirement.scheduler->pending(), size_t{1});
        QVERIFY(f.retirement.step(Lane::Cleanup));
        f.retirement.drain();
        QVERIFY(f.retirement.quiescent());
    }
    void closingCancelsUnstartedObservationWithoutRetargeting()
    {
        Fixture f; const auto id = f.add(1, 100);
        f.reader->changed({{id, CT_NEW}}, ADDRESSES);
        QVERIFY(f.commands.step(Lane::Domain));
        f.reader->close(); f.opening->retire();
        f.commands.drain();
        QCOMPARE(f.wallet->reads, 0);
        const auto result = f.reader->take();
        QVERIFY(!result || !result->snapshot);
    }
};

#include <test_walletreceipts.moc>
BITCOINQML_REGISTER_QT_TEST(WalletReceiptsTests)
