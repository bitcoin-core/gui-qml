// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/activitydetailmodel.h>
#include <qml/models/transactionactivitymodel.h>
#include <qml/models/walletqmlmodel.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/mockwallet.h>
#include <core_io.h>
#include <key_io.h>
#include <QClipboard>
#include <QGuiApplication>
#include <QScopeGuard>
#include <QtTest/QtTest>
#include <atomic>
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <vector>
#include <utility>

namespace {
interfaces::WalletTx Payment(uint8_t marker)
{
    interfaces::WalletTx result{};
    CMutableTransaction tx;
    tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256{marker}),0});
    const CTxDestination destination{PKHash{uint160{std::vector<unsigned char>(20,marker)}}};
    tx.vout.emplace_back(1000+marker,GetScriptForDestination(destination));
    result.tx=MakeTransactionRef(tx);
    result.time=1'789'200'000;
    result.txin_is_mine={false};
    result.txout_is_mine={true};
    result.txout_is_change={false};
    result.txout_address={destination};
    result.txout_address_is_mine={true};
    result.credit=1000+marker;
    return result;
}
QString Id(const interfaces::WalletTx& value) {return QString::fromStdString(value.tx->GetHash().ToString());}
QString RawRows(ActivityDetailModel& view)
{
    QString result;
    auto* rows = view.rawTransactionLines();
    for (int row = 0; row < rows->rowCount(); ++row) result += rows->data(rows->index(row,0),Qt::UserRole).toString();
    return result;
}
QString EntryField(ActivityDetailModel& view,const QString& field)
{
    QString value;
    auto* rows=view.entryDocument();
    for (int row=0;row<rows->rowCount();++row) {
        const auto entry=rows->data(rows->index(row,0),Qt::UserRole).toMap();
        if (entry.value("field").toString()==field) value+=entry.value("content").toString();
    }
    return value;
}
struct Native final : StubWallet {
    const interfaces::WalletTx first{Payment(1)},second{Payment(2)};
    std::vector<interfaces::WalletTx> other;
    explicit Native(int count=2) {other.reserve(std::max(0,count-2));
        for(int i=3;i<=count;++i)other.push_back(Payment(uint8_t(i)));}
    std::atomic<int> depth{2},histories{0},detail_reads{0},subscriptions{0};
    std::atomic<bool> read_on_gui{false};
    std::atomic<bool> first_missing{false};
    std::string getWalletName() override {return "same-opening";}
    void read() {if(QThread::currentThread()==qApp->thread())read_on_gui=true;}
    std::set<interfaces::WalletTx> getWalletTxs() override {read();
        ++histories;
        std::set<interfaces::WalletTx> result{first,second};
        result.insert(other.begin(),other.end());
        return result;}
    interfaces::WalletTx getWalletTx(const Txid& id) override {read();
        ++detail_reads;
        if (id==first.tx->GetHash() && first_missing.load()) return {};
        return id==first.tx->GetHash()?first:id==second.tx->GetHash()?second:interfaces::WalletTx{};}
    CTransactionRef getTx(const Txid&) override {read();
        return {};}
    bool tryGetTxStatus(const Txid&,interfaces::WalletTxStatus& status,int& height,int64_t& time) override {
        read();
        status={};
        status.depth_in_main_chain=depth.load();
        status.is_in_main_chain=true;
        height=100;
        time=1'789'200'000;
        return true;
    }
    bool tryGetBalances(interfaces::WalletBalances& balances,uint256& tip) override {balances={};
        tip=uint256{1};
        return true;}
    std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn) override {++subscriptions;
        return {};}
};
struct Fixture {
    ManualExecutionEnvironment env;
    std::shared_ptr<Native> native;
    std::unique_ptr<WalletQmlModel> wallet;
    std::unique_ptr<TransactionActivityModel> source;
    std::shared_ptr<BackendExecutor> backend;
    std::unique_ptr<ActivityDetailModel> view(QObject& owner) {
        std::unique_ptr<ActivityDetailModel> result{source->createDetailView(&owner)};
        if (result) result->setActive(true);
        return result;
    }
    explicit Fixture(int count=2) : native(std::make_shared<Native>(count)) {
        FeeReadServices fees;
        fees.scheduler=env.scheduler;
        fees.post=[this](auto turn){env.post(std::move(turn));};
        fees.timer=std::shared_ptr<FeeReadTimer>{&env,[](auto*){}};
        wallet=std::make_unique<WalletQmlModel>(WalletQmlModel::SharedWallet{native,env.scheduler},nullptr,std::move(fees),psbtfee::Services{},backup::Services{});
        backend=wallet->backendExecutor();
        if(!QTest::qWaitFor([this]{settle();
            return wallet->capabilitiesKnown()&&wallet->property("receiveRequestsLoaded").toBool()&&!wallet->receiveRequestReconciliationPending();},5000))QTest::qFail("Opening did not become ready",__FILE__,__LINE__);
        TransactionActivityModel::Services services;
        services.post=[this](auto turn){env.post(std::move(turn));};
        services.schedule_poll=[](auto,auto){};
        services.cancel_poll=[]{};
        source=std::make_unique<TransactionActivityModel>(wallet.get(),std::move(services));
        settle();
    }
    void settle() {
        for(int round=0;round<64;++round){env.drain();
            env.publishAll();
            QCoreApplication::processEvents();
            if(env.turnsIdle()&&env.publicationsIdle()&&(!source||!source->loading()))return;}
    }
    ~Fixture() {
        source.reset();
        wallet->beginShutdown();
        if(!QTest::qWaitFor([this]{settle();
            return backend->isDrained()&&env.turnsIdle()&&env.publicationsIdle();},10000))QTest::qFail("Shared opening failed physical retirement",__FILE__,__LINE__);
        wallet.reset();
        settle();
    }
};
}

class ActivityDetailModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void cachedPreviewAndFullResultKeepIndependentOwners_data()
    {
        QTest::addColumn<bool>("full_gui_first");
        QTest::newRow("preview-before-full") << false;
        QTest::newRow("full-before-late-preview") << true;
    }
    void cachedPreviewAndFullResultKeepIndependentOwners()
    {
        QFETCH(bool,full_gui_first);
        Fixture f;
        QObject owner;
        auto view=f.view(owner);
        const auto reads=f.native->detail_reads.load();
        view->setTransactionId(Id(f.native->first));
        QVERIFY(f.env.queuedTurnCount()>=2);
        auto held_core=f.env.takeTurnFront();
        QVERIFY(held_core);
        auto core=std::move(*held_core); // The original public read stays physically held.
        const auto release_core=qScopeGuard([&] {
            if (core.turn) {
                auto turn=std::exchange(core.turn,{});
                turn();
            }
        });
        f.env.drain();
        QCOMPARE(f.native->detail_reads.load(),reads);
        QCOMPARE(view->detailOutputs()->rowCount(),0); // GUI preview also held.
        if (!full_gui_first) {
            f.env.publishAll();
            QCOMPARE(view->state(),ActivityDetailModel::Loading);
            QCOMPARE(view->detailOutputs()->rowCount(),1);
            QCOMPARE(view->detailOutputs()->data(view->detailOutputs()->index(0,0),Qt::UserRole).toMap().value("amountSat").toLongLong(),qint64{1001});
            QVERIFY(!view->selectedTransactionDetails().value("flow").toMap().value("inputs").toList().front().toMap().value("amountKnown").toBool());
            QVERIFY(!view->selectedTransactionDetails().value("canBump").toBool());
            QVERIFY(view->requestEntryDetails("outputs",0));
            f.env.drain();
            f.env.publishAll();
            QCOMPARE(EntryField(*view,"amountSat"),QStringLiteral("1001"));
            QCOMPARE(EntryField(*view,"index"),QStringLiteral("0"));
            QCOMPARE(f.native->detail_reads.load(),reads); // Entry observation never unblocks/reads Core.
        }
        auto finish_core=std::exchange(core.turn,{});
        finish_core();
        f.env.drain();
        QCOMPARE(f.native->detail_reads.load(),reads+1);
        if (full_gui_first) {
            QCOMPARE(f.env.publicationCount(),size_t{2});
            auto full=f.env.takePublicationBack();
            QVERIFY(full);
            full();
            QCOMPARE(view->state(),ActivityDetailModel::Ready);
        }
        f.env.publishAll();
        QCOMPARE(view->state(),ActivityDetailModel::Ready);
        QCOMPARE(RawRows(*view),QString::fromStdString(EncodeHexTx(*f.native->first.tx)));
        QCOMPARE(view->detailOutputs()->data(view->detailOutputs()->index(0,0),Qt::UserRole).toMap().value("amountSat").toLongLong(),qint64{1001});
        view.reset();
        f.settle();
    }
    void detailUnitComesFromTheCoherentPublishedDomainFrame()
    {
        Fixture f;
        QObject owner;
        const auto id=Id(f.native->first);
        const int row=f.source->activityRowForId(QStringLiteral("tx:")+id);
        QVERIFY(row>=0);
        const auto original_amount=f.source->index(row,0).data(TransactionActivityModel::AmountRole).toString();
        f.source->setDisplayUnit(3); // Desired SAT unit; its Domain projection stays held.
        auto view=f.view(owner);
        view->setTransactionId(id);
        f.env.drain();
        f.env.publishAll(); // Admits source projection but does not run it.
        QCOMPARE(view->state(),ActivityDetailModel::Ready);
        QCOMPARE(view->selectedTransactionDetails().value("amount").toString(),original_amount);
        QVERIFY(view->selectedTransactionDetails().value("flow").toMap().value("outputs").toList().front().toMap().value("amount").toString().contains("BTC"));
        f.settle();
        const auto current_amount=f.source->index(f.source->activityRowForId(QStringLiteral("tx:")+id),0).data(TransactionActivityModel::AmountRole).toString();
        QVERIFY(current_amount!=original_amount);
        QCOMPARE(view->selectedTransactionDetails().value("amount").toString(),current_amount);
        QVERIFY(view->selectedTransactionDetails().value("flow").toMap().value("outputs").toList().front().toMap().value("amount").toString().contains("sat",Qt::CaseInsensitive));
        QCOMPARE(RawRows(*view),QString::fromStdString(EncodeHexTx(*f.native->first.tx)));
        view.reset();
        f.settle();
    }
    void twoViewsShareDomainUpdatesAndKeepIndependentInterest()
    {
        Fixture f{130};
        QObject owner;
        std::unique_ptr<ActivityDetailModel> first{f.view(owner)},second{f.view(owner)};
        QVERIFY(first&&second);
        QVERIFY(first.get()!=second.get());
        const auto histories=f.native->histories.load(),subscriptions=f.native->subscriptions.load();
        QCOMPARE(f.source->transactionCount(),130);
        std::weak_ptr<const activityview::Hold> original_history=f.source->preparedSnapshot();
        const auto reads=f.native->detail_reads.load();
        first->setTransactionId(Id(f.native->first));
        second->setTransactionId(Id(f.native->second));
        QVERIFY(first->pending());
        QVERIFY(second->pending());
        QCOMPARE(first->detailOutputs()->rowCount(),0);
        f.env.drain();
        QCOMPARE(first->detailOutputs()->rowCount(),0);
        QCOMPARE(second->detailOutputs()->rowCount(),0);
        QCOMPARE(f.native->detail_reads.load(),reads+2);
        f.env.publishAll();
        f.settle();
        QCOMPARE(first->selectedTransactionDetails().value("txid").toString(),Id(f.native->first));
        QCOMPARE(second->selectedTransactionDetails().value("txid").toString(),Id(f.native->second));
        QCOMPARE(first->detailOutputs()->rowCount(),1);
        QCOMPARE(second->detailOutputs()->rowCount(),1);
        QVERIFY(first->rawTransactionLines()->rowCount()>0);
        QVERIFY(second->rawTransactionLines()->rowCount()>0);
        QCOMPARE(first->detailOutputs()->data(first->detailOutputs()->index(0,0),Qt::UserRole).toMap().value("amountSat").toLongLong(),qint64{1001});
        QCOMPARE(second->detailOutputs()->data(second->detailOutputs()->index(0,0),Qt::UserRole).toMap().value("amountSat").toLongLong(),qint64{1002});
        const auto first_address = QString::fromStdString(EncodeDestination(f.native->first.txout_address.front()));
        const auto second_address = QString::fromStdString(EncodeDestination(f.native->second.txout_address.front()));
        QCOMPARE(first->detailOutputs()->data(first->detailOutputs()->index(0,0),Qt::UserRole).toMap().value("address").toString(),first_address);
        QCOMPARE(second->detailOutputs()->data(second->detailOutputs()->index(0,0),Qt::UserRole).toMap().value("address").toString(),second_address);
        QCOMPARE(RawRows(*first),QString::fromStdString(EncodeHexTx(*f.native->first.tx)));
        QCOMPARE(RawRows(*second),QString::fromStdString(EncodeHexTx(*f.native->second.tx)));
        QCOMPARE(f.native->histories.load(),histories);
        QCOMPARE(f.native->subscriptions.load(),subscriptions);
        // One authoritative source update reaches both independently selected
        // observers. Neither starts another history reader/subscription.
        f.native->depth=8;
        f.source->refreshStatuses();
        f.settle();
        QCOMPARE(first->selectedTransactionDetails().value("depth").toInt(),8);
        QCOMPARE(second->selectedTransactionDetails().value("depth").toInt(),8);
        QVERIFY(original_history.expired());
        // Selected views do not pin the old 130-row history.
        first->setActive(false);
        f.settle();
        QCOMPARE(first->detailOutputs()->rowCount(),0);
        QCOMPARE(second->detailOutputs()->rowCount(),1);
        QCOMPARE(second->transactionId(),Id(f.native->second));
        QCOMPARE(second->detailOutputs()->data(second->detailOutputs()->index(0,0),Qt::UserRole).toMap().value("amountSat").toLongLong(),qint64{1002});
        QCOMPARE(RawRows(*second),QString::fromStdString(EncodeHexTx(*f.native->second.tx)));
        first->setActive(true);
        f.env.drain();
        first.reset();
        f.env.publishAll();
        f.native->depth=9;
        f.source->refreshStatuses();
        f.settle();
        QCOMPARE(second->selectedTransactionDetails().value("depth").toInt(),9);
        QCOMPARE(f.native->histories.load(),histories);
        QCOMPARE(f.native->subscriptions.load(),subscriptions);
        second.reset();
        f.settle();
    }
    void heldOriginalOutcomeCannotReplaceNewInterestOrReopenedView()
    {
        Fixture f;
        QObject owner;
        std::unique_ptr<ActivityDetailModel> old{f.view(owner)};
        old->setTransactionId(Id(f.native->first));
        f.env.drain();
        old->setTransactionId(Id(f.native->second));
        f.env.publishAll();
        QVERIFY(old->pending());
        QCOMPARE(old->detailOutputs()->rowCount(),0);
        f.settle();
        QCOMPARE(old->selectedTransactionDetails().value("txid").toString(),Id(f.native->second));
        old->reload();
        f.env.drain();
        f.wallet->beginShutdown();
        f.source->closeAdmission();
        const auto publications=f.env.publicationCount();
        QVERIFY(publications>0);
        Fixture reopened;
        QObject reopened_owner;
        QVERIFY(f.wallet->opening()->identity()!=reopened.wallet->opening()->identity());
        std::unique_ptr<ActivityDetailModel> current{reopened.view(reopened_owner)};
        current->setTransactionId(Id(reopened.native->first));
        reopened.settle();
        QSignalSpy changed{current.get(),&ActivityDetailModel::transactionDetailsChanged};
        f.env.publishAll();
        f.settle();
        QCOMPARE(changed.count(),0);
        QCOMPARE(current->transactionId(),Id(reopened.native->first));
        QCOMPARE(current->detailOutputs()->rowCount(),1);
        QCOMPARE(old->detailOutputs()->rowCount(),0);
        QVERIFY(!old->active());
        old.reset();
        current.reset();
        f.settle();
        reopened.settle();
    }
    void originalOpeningCloseFencesBothViewsBeforeHeldGuiDelivery()
    {
        Fixture f;
        QObject owner;
        std::unique_ptr<ActivityDetailModel> first{f.view(owner)},second{f.view(owner)};
        first->setTransactionId(Id(f.native->first));
        second->setTransactionId(Id(f.native->second));
        f.env.drain();
        QVERIFY(!f.env.publicationsIdle());
        QSignalSpy first_changed{first.get(),&ActivityDetailModel::transactionDetailsChanged};
        QSignalSpy second_changed{second.get(),&ActivityDetailModel::transactionDetailsChanged};
        f.wallet->beginShutdown();
        QVERIFY(!first->active());
        QVERIFY(!second->active());
        QCOMPARE(first->state(),ActivityDetailModel::Closed);
        QCOMPARE(second->state(),ActivityDetailModel::Closed);
        QCOMPARE(first->detailOutputs()->rowCount(),0);
        QCOMPARE(second->detailOutputs()->rowCount(),0);
        QCOMPARE(first->requestCopyRawTransaction(Id(f.native->first)),quint64{0});
        QCOMPARE(second->requestEntryDetails("outputs",0),quint64{0});
        const auto first_count=first_changed.count(),second_count=second_changed.count();
        f.source->closeAdmission();
        f.env.publishAll();
        f.settle();
        QCOMPARE(first_changed.count(),first_count);
        QCOMPARE(second_changed.count(),second_count);
        first.reset();
        second.reset();
        f.source.reset();
        QVERIFY(QTest::qWaitFor([&]{f.settle();
            return f.backend->isDrained();},5000));
        QCOMPARE(f.env.scheduler->pending(),size_t{0});
    }
    void identitySignalCannotExposeOrAdmitThePreviousTransaction()
    {
        Fixture f;
        QObject owner;
        auto view=f.view(owner);
        view->setTransactionId(Id(f.native->first));
        f.settle();
        QCOMPARE(RawRows(*view),QString::fromStdString(EncodeHexTx(*f.native->first.tx)));
        int observed{0};
        connect(view.get(),&ActivityDetailModel::transactionIdChanged,&owner,[&] {
            ++observed;
            QCOMPARE(view->transactionId(),Id(f.native->second));
            QCOMPARE(view->state(),ActivityDetailModel::Loading);
            const auto data=view->selectedTransactionDetails();
            QCOMPARE(data.value("txid").toString(),Id(f.native->second));
            QVERIFY(!data.contains("netAmountSat"));
            QVERIFY(data.value("flow").toMap().isEmpty());
            QVERIFY(!data.value("canBump").toBool());
            QCOMPARE(view->detailOutputs()->rowCount(),0);
            QCOMPARE(view->requestEntryDetails("outputs",0),quint64{0});
            QCOMPARE(view->requestCopyRawTransaction(Id(f.native->second)),quint64{0});
        });
        view->setTransactionId(Id(f.native->second));
        QCOMPARE(observed,1);
        f.settle();
        QCOMPARE(view->detailOutputs()->data(view->detailOutputs()->index(0,0),Qt::UserRole).toMap().value("amountSat").toLongLong(),qint64{1002});
        QCOMPARE(RawRows(*view),QString::fromStdString(EncodeHexTx(*f.native->second.tx)));
        view.reset();
        f.settle();
    }
    void acceptedCopyPinsNativeRetirementUntilOriginalGuiHandoff()
    {
        Fixture f;
        QObject owner;
        auto view=f.view(owner);
        view->setTransactionId(Id(f.native->first));
        f.settle();
        QVERIFY(QGuiApplication::clipboard());
        QGuiApplication::clipboard()->setText("before original handoff");
        const auto expected=QString::fromStdString(EncodeHexTx(*f.native->first.tx));
        QSignalSpy copied{view.get(),&ActivityDetailModel::copyFinished};
        QSignalSpy shutdown{f.wallet.get(),&WalletQmlModel::shutdownFinished};
        QVERIFY(view->requestCopyRawTransaction(Id(f.native->first)));
        // Hold the accepted Domain turn, then close its original opening.
        f.wallet->beginShutdown();
        f.source->closeAdmission();
        QCOMPARE(view->state(),ActivityDetailModel::Closed);
        QVERIFY(f.env.scheduler->pending()>0);
        QCOMPARE(QGuiApplication::clipboard()->text(),QStringLiteral("before original handoff"));
        f.env.drain();
        // Domain completed, but the necessary GUI clipboard handoff still
        // owns a Native cleanup lease; the actual ledger cannot drain yet.
        QCOMPARE(QGuiApplication::clipboard()->text(),QStringLiteral("before original handoff"));
        QVERIFY(f.env.scheduler->pending()>0);
        QCOMPARE(shutdown.count(),0);
        QCoreApplication::processEvents();
        QCOMPARE(QGuiApplication::clipboard()->text(),expected);
        QCOMPARE(copied.count(),0); // Quiet observer, actual accepted effect.
        view.reset();
        f.source.reset();
        QVERIFY(QTest::qWaitFor([&] {f.settle();return shutdown.count()==1&&f.backend->isDrained()&&f.env.scheduler->pending()==0;},5000));
        QCOMPARE(f.env.scheduler->pending(),size_t{0});
    }
    void acceptedCopyFinishesAfterItsViewIsDestroyed()
    {
        Fixture f;
        QObject owner;
        std::unique_ptr<ActivityDetailModel> view{f.view(owner)};
        view->setTransactionId(Id(f.native->first));
        f.settle();
        QVERIFY(QGuiApplication::clipboard());
        QGuiApplication::clipboard()->clear();
        const auto expected=QString::fromStdString(EncodeHexTx(*f.native->first.tx));
        QVERIFY(view->requestCopyRawTransaction(Id(f.native->first)));
        view.reset();
        f.env.drain();
        QCoreApplication::processEvents();
        QCOMPARE(QGuiApplication::clipboard()->text(),expected);
        f.settle();
    }
    void reentrantInterestLossCannotInstallTheHeldProjection()
    {
        Fixture f;
        QObject owner;
        std::unique_ptr<ActivityDetailModel> first{f.view(owner)},second{f.view(owner)};
        first->setTransactionId(Id(f.native->first));
        second->setTransactionId(Id(f.native->second));
        f.settle();
        first->reload();
        f.env.drain();
        bool revoked{false};
        int reset_observations{0};
        connect(first->detailInputs(),&QAbstractItemModel::modelAboutToBeReset,&owner,[&]{if(!std::exchange(revoked,true))first->setActive(false);});
        connect(first->detailInputs(),&QAbstractItemModel::modelReset,&owner,[&]{++reset_observations;
            if(!first->active())QCOMPARE(first->detailInputs()->rowCount(),0);});
        f.env.publishAll();
        f.settle();
        QVERIFY(revoked);
        QVERIFY(reset_observations>0);
        QVERIFY(!first->active());
        QCOMPARE(first->detailOutputs()->rowCount(),0);
        QCOMPARE(second->detailOutputs()->rowCount(),1);
        QCOMPARE(second->transactionId(),Id(f.native->second));
        first.reset();
        second.reset();
        f.settle();
    }
    void rejectedReadIsExplicitAndSameViewCanRetry()
    {
        Fixture f;
        QObject owner;
        std::unique_ptr<ActivityDetailModel> view{f.view(owner)};
        const auto reads=f.native->detail_reads.load();
        f.env.rejectNext(true);
        view->setTransactionId(Id(f.native->first));
        f.settle();
        QVERIFY(!view->pending());
        QVERIFY(!view->error().isEmpty());
        QCOMPARE(view->state(),ActivityDetailModel::Failed);
        QCOMPARE(view->detailOutputs()->rowCount(),0);
        QCOMPARE(f.native->detail_reads.load(),reads);
        view->reload();
        f.settle();
        QVERIFY(!view->pending());
        QVERIFY(view->error().isEmpty());
        QCOMPARE(view->state(),ActivityDetailModel::Ready);
        QCOMPARE(view->detailOutputs()->rowCount(),1);
        view.reset();
        f.settle();
    }
    void missingNativeTransactionCannotTurnItsCachedPreviewIntoReady()
    {
        Fixture f;
        QObject owner;
        auto view=f.view(owner);
        view->setTransactionId(Id(f.native->first));
        f.settle();
        f.native->first_missing=true;
        view->reload();
        f.settle();
        QCOMPARE(view->state(),ActivityDetailModel::Failed);
        QVERIFY(!view->error().isEmpty());
        QVERIFY(!view->selectedTransactionDetails().value("canBump").toBool());
        QCOMPARE(view->requestEntryDetails("outputs",0),quint64{0});
        QCOMPARE(view->requestCopyRawTransaction(Id(f.native->first)),quint64{0});
        f.native->first_missing=false;
        view->reload();
        f.settle();
        QCOMPARE(view->state(),ActivityDetailModel::Ready);
        QCOMPARE(RawRows(*view),QString::fromStdString(EncodeHexTx(*f.native->first.tx)));
        view.reset();
        f.settle();
    }
    void realWorkerReadsStayOffGuiAndShareOpeningHistory()
    {
        auto owned=std::make_unique<Native>();
        auto* native=owned.get();
        auto wallet=std::make_unique<WalletQmlModel>(std::move(owned),nullptr);
        QTRY_VERIFY(wallet->capabilitiesKnown());
        auto* source=wallet->transactionActivityModel();
        QTRY_VERIFY(!source->loading());
        QObject owner;
        std::unique_ptr<ActivityDetailModel> first{source->createDetailView(&owner)},second{source->createDetailView(&owner)};
        const auto histories=native->histories.load();
        first->setActive(true);
        second->setActive(true);
        first->setTransactionId(Id(native->first));
        second->setTransactionId(Id(native->second));
        QTRY_VERIFY(!first->pending()&&!second->pending());
        QVERIFY(first->error().isEmpty());
        QVERIFY(second->error().isEmpty());
        QCOMPARE(first->detailOutputs()->rowCount(),1);
        QCOMPARE(second->detailOutputs()->rowCount(),1);
        QVERIFY(!native->read_on_gui.load());
        QCOMPARE(native->histories.load(),histories);
        first.reset();
        second.reset();
        const auto backend=wallet->backendExecutor();
        wallet->beginShutdown();
        QTRY_VERIFY(backend->isDrained());
        wallet.reset();
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(ActivityDetailModelTests)
#else
QTEST_MAIN(ActivityDetailModelTests)
#endif
#include <test_activitydetailmodel.moc>
