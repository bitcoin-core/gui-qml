// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletqmlmodel.h>
#include <qml/walletsettings.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>

#include <chainparams.h>
#include <chainparamsbase.h>
#include <key_io.h>
#include <util/chaintype.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

namespace {
QString Address() { return QString::fromStdString(EncodeDestination(PKHash{uint160{}})); }
QmlRecentRequestEntry Entry()
{
    QmlRecentRequestEntry entry;
    entry.id = 1; entry.date = QDateTime::fromSecsSinceEpoch(1'700'000'000);
    entry.recipient.address = Address().toStdString(); entry.recipient.amount = 100;
    entry.recipient.label = "public"; entry.recipient.message = "message"; entry.recipient.noteSelf = "private";
    return entry;
}
struct Wallet : StubWallet {
    std::map<std::string, std::string> stored{{"1", walletrequests::Encode(Entry())}};
    int loads{0}, writes{0}, generated{0}, address_reads{0};
    bool fail_label{false};
    bool canGetAddresses() override { return true; }
    bool taprootEnabled() override { return true; }
    util::Result<CTxDestination> getNewDestination(OutputType, const std::string&) override {
        uint160 key; key.begin()[0] = static_cast<unsigned char>(++generated);
        return CTxDestination{PKHash{key}};
    }
    TransactionChangedFn transaction;
    std::string getWalletName() override { return "request-consumer"; }
    std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn fn) override {
        transaction = std::move(fn); return {};
    }
    std::vector<std::string> getAddressReceiveRequests() override {
        ++loads; std::vector<std::string> result; result.reserve(stored.size());
        for (const auto& [id, blob] : stored) result.push_back(blob);
        return result;
    }
    std::vector<interfaces::WalletAddress> getAddresses() override {
        return {{DecodeDestination(Address().toStdString()), true, wallet::AddressPurpose::RECEIVE, "private"}};
    }
    bool getAddress(const CTxDestination&, std::string* label, wallet::AddressPurpose* purpose) override {
        ++address_reads; if (label) *label = "private";
        if (purpose) *purpose = wallet::AddressPurpose::RECEIVE;
        return true;
    }
    bool setAddressBook(const CTxDestination&, const std::string&, const std::optional<wallet::AddressPurpose>&) override { return !fail_label; }
    bool setAddressReceiveRequest(const CTxDestination&, const std::string& id, const std::string& blob) override {
        ++writes; if (blob.empty()) stored.erase(id); else stored[id] = blob; return true;
    }
};
struct Fixture {
    ManualRoutedExecution env;
    std::shared_ptr<Wallet> wallet{std::make_shared<Wallet>()};
    std::unique_ptr<WalletQmlModel> model{new WalletQmlModel{
        WalletQmlModel::SharedWallet{wallet, env.scheduler, "request-consumer"}, nullptr,
        FeeReadServices{}, psbtfee::Services{}, backup::Services{}}};
    Fixture()
    {
        QSettings settings;
        settings.remove("receiveAddressTypes/request-consumer");
        settings.remove(walletsettings::EntryKey(walletsettings::Area::Reservations, QStringLiteral("request-consumer")));
        settings.sync();
    }
    void settle()
    {
        // A live projection owns idle cleanup reservations. Those count toward
        // physical drain, but cannot make an otherwise idle manual turn runnable.
        for (int round{0}; round < 128; ++round) {
            env.drain();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            if (env.queuedIdle() && env.activeIdle()) return;
        }
        QFAIL("Receive projection did not settle within128 controlled publication turns");
    }
    ~Fixture()
    {
        model->beginShutdown();
        const bool drained = QTest::qWaitFor([&] { settle(); return model->backendExecutor()->isDrained(); }, 3000);
        QVERIFY(drained);
        settle();
        QCOMPARE(env.scheduler->pending(), size_t{0});
        const auto directory = qEnvironmentVariable("BITCOIN_QML_MANUAL_TRACE_DIR");
        if (!directory.isEmpty()) {
            QDir{}.mkpath(directory);
            QJsonArray trace;
            for (const auto& event : env.traceSnapshot()) trace.append(QString::fromStdString(event));
            QFile file{directory + QLatin1Char('/') + QString::fromLatin1(QTest::currentTestFunction()) + QStringLiteral(".json")};
            QVERIFY(file.open(QIODevice::WriteOnly));
            const auto bytes = QJsonDocument{QJsonObject{{"commands", trace}, {"loads", wallet->loads},
                {"writes", wallet->writes}, {"failed", QTest::currentTestFailed()}}}.toJson();
            QCOMPARE(file.write(bytes), bytes.size());
        }
    }
};
}

class ReceiveRequestOperationTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase()
    {
        SelectBaseParams(ChainType::MAIN); SelectParams(ChainType::MAIN);
    }
    void historyLoadsOnlyOnInjectedTurns()
    {
        Fixture f;
        QCOMPARE(f.wallet->loads, 0);
        QCOMPARE(f.model->receiveRequests()->count(), 0);
        f.env.drain();
        QCOMPARE(f.wallet->loads, 1);
        QCOMPARE(f.model->receiveRequests()->count(), 0);
        f.settle();
        QCOMPARE(f.model->receiveRequests()->count(), 1);
        QCOMPARE(f.model->receiveRequests()->firstRequestIdForAddress(Address()), QStringLiteral("1"));
        QVERIFY(!f.model->receiveRequestReconciliationPending());
    }
    void qrExportUsesTheOriginalOpeningAndBackgroundWriter()
    {
        Fixture f; f.settle(); QVERIFY(f.model->loadPaymentRequestDetail("1"));
        // Only the selected request, not every history row, formats its URI.
        f.settle();
        auto* request = f.model->detailPaymentRequest();
        QVERIFY(!request->qrPayload().isEmpty());
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString path = directory.path() + QStringLiteral("/request.png");
        QImage image{32, 32, QImage::Format_ARGB32}; image.fill(Qt::blue);
        std::unique_ptr<ImageExportOperation> operation{f.model->savePaymentRequestImage(request, image, QUrl::fromLocalFile(path))};
        QVERIFY(operation); QVERIFY(operation->pending()); QVERIFY(!QFile::exists(path));
        f.env.drain(); QVERIFY(QFile::exists(path)); QVERIFY(operation->pending());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(operation->saved()); QCOMPARE(QImage{path}.pixel(0, 0), image.pixel(0, 0));
        const QString retired_path = directory.path() + QStringLiteral("/retired.png");
        std::unique_ptr<ImageExportOperation> retired{f.model->savePaymentRequestImage(request, image, QUrl::fromLocalFile(retired_path))};
        QVERIFY(retired); request->clear(); f.settle();
        QVERIFY(!QFile::exists(retired_path)); QVERIFY(!retired->pending()); QVERIFY(!retired->saved());
    }
    void queuedQrExportRejectsNewNativeFactsBeforeGuiNotification()
    {
        Fixture f; f.settle(); QVERIFY(f.model->loadPaymentRequestDetail("1"));
        f.settle();
        QVERIFY(!f.model->detailPaymentRequest()->qrPayload().isEmpty());
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString path = directory.path() + QStringLiteral("/stale.png");
        QImage image{32, 32, QImage::Format_ARGB32}; image.fill(Qt::blue);
        std::unique_ptr<ImageExportOperation> operation{f.model->savePaymentRequestImage(f.model->detailPaymentRequest(), image, QUrl::fromLocalFile(path))};
        QVERIFY(operation); QVERIFY(f.wallet->transaction);
        // The native revision changes immediately, before Domain/GUI consumes
        // the receipt refresh. The old queued file effect is no longer valid.
        f.wallet->transaction(Txid::FromUint256(uint256{1}), CT_NEW);
        QVERIFY(f.model->receiveRequestReconciliationPending());
        f.env.drain(); QVERIFY(!QFile::exists(path));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(!operation->pending()); QVERIFY(!operation->saved());
        QCOMPARE(operation->operation()->result()->error, imageexport::Error::Retired);
    }
    void loadingRejectsARecordReplacedDuringClear_data()
    {
        QTest::addColumn<bool>("detail");
        QTest::newRow("draft") << false;
        QTest::newRow("detail") << true;
    }
    void loadingRejectsARecordReplacedDuringClear()
    {
        QFETCH(bool, detail);
        Fixture f; f.settle();
        auto* target = detail ? f.model->detailPaymentRequest() : f.model->currentPaymentRequest();
        bool replaced{false};
        connect(target, &PaymentRequest::generationChanged, f.model.get(), [&] {
            if (std::exchange(replaced, true)) return;
            auto entry = Entry(); entry.id = 2; entry.recipient.label = "replacement record";
            f.wallet->stored = {{"2", walletrequests::Encode(entry)}};
            f.model->reloadReceiveRequests(); f.settle();
        });
        QVERIFY(!(detail ? f.model->loadPaymentRequestDetail("1") : f.model->loadPaymentRequest("1")));
        QVERIFY(replaced);
        QCOMPARE(f.model->receiveRequests()->count(), 1);
        QCOMPARE(f.model->receiveRequests()->firstRequestIdForAddress(Address()), QStringLiteral("2"));
        QVERIFY(target->id().isEmpty()); QVERIFY(target->label().isEmpty());
        QCOMPARE(target->amount()->satoshi(), CAmount{0});
    }
    void loadCannotContinueAfterItsClearClosesTheOpening()
    {
        Fixture f; f.settle();
        auto* target = f.model->currentPaymentRequest();
        connect(target, &PaymentRequest::generationChanged, f.model.get(), [&] { f.model->beginShutdown(); });
        QVERIFY(!f.model->loadPaymentRequest("1"));
        QVERIFY(!f.model->available()); QVERIFY(target->id().isEmpty());
    }
    void olderLoadCannotOverwriteANewerDraftCreatedDuringClear()
    {
        Fixture f; f.settle(); auto* target = f.model->currentPaymentRequest();
        bool replaced{false};
        connect(target, &PaymentRequest::generationChanged, f.model.get(), [&] {
            if (std::exchange(replaced, true)) return;
            target->clear(); target->setLabel("newer draft");
        });
        QVERIFY(!f.model->loadPaymentRequest("1"));
        QCOMPARE(target->label(), QStringLiteral("newer draft")); QVERIFY(target->id().isEmpty());
    }
    void loadingPreservesAnEditMadeByAPropertyReceiver()
    {
        Fixture f; f.settle(); auto* target = f.model->currentPaymentRequest();
        bool edited{false};
        connect(target, &PaymentRequest::addressChanged, f.model.get(), [&] {
            if (target->address().isEmpty() || std::exchange(edited, true)) return;
            target->setMessage("newer input");
        });
        const bool loaded = f.model->loadPaymentRequest("1");
        QVERIFY(edited); QCOMPARE(target->message(), QStringLiteral("newer input"));
        QVERIFY(!loaded); QVERIFY(target->id().isEmpty());
    }
    void templatePreservesAnEditMadeByAPropertyReceiver()
    {
        Fixture f; f.settle(); auto* target = f.model->currentPaymentRequest();
        bool edited{false};
        connect(target, &PaymentRequest::labelChanged, f.model.get(), [&] {
            if (target->label().isEmpty() || std::exchange(edited, true)) return;
            target->setMessage("newer template input");
        });
        f.model->usePaymentRequestAsTemplate("1");
        QVERIFY(edited); QCOMPARE(target->message(), QStringLiteral("newer template input"));
        QVERIFY(target->id().isEmpty());
    }
    void clearStopsWhenANotificationDestroysTheProjection_data()
    {
        QTest::addColumn<bool>("child");
        QTest::newRow("parent-notification") << false;
        QTest::newRow("amount-notification") << true;
    }
    void clearStopsWhenANotificationDestroysTheProjection()
    {
        QFETCH(bool, child);
        auto* target = new PaymentRequest;
        const QPointer<PaymentRequest> held{target};
        target->amount()->setSatoshi(100);
        if (child) connect(target->amount(), &BitcoinAmount::inputChanged, [target] { delete target; });
        else connect(target, &PaymentRequest::generationChanged, [target] { delete target; });
        target->clear(); QVERIFY(!held);
    }
    void uriWaitsForDomainAndPublishesOnlyTheLatestFields()
    {
        Fixture f; f.settle(); auto* target = f.model->currentPaymentRequest();
        target->setPreparedDestination(DecodeDestination(Address().toStdString()), Address());
        target->setLabel("old label");
        QVERIFY(target->qrPayload().isEmpty());
        f.env.drain(); QVERIFY(target->qrPayload().isEmpty());
        target->setLabel("latest & value");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(target->qrPayload().isEmpty());
        f.env.drain(); QVERIFY(target->qrPayload().isEmpty());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(target->qrPayload(), QStringLiteral("bitcoin:") + Address() + QStringLiteral("?label=latest%20%26%20value"));
        QCOMPARE(f.wallet->loads, 1); QCOMPARE(f.wallet->writes, 0); QCOMPARE(f.wallet->generated, 0);
    }
    void savedPreparedPayloadSurvivesAnEarlierFormatter()
    {
        Fixture f; f.settle(); QVERIFY(f.model->loadPaymentRequest("1"));
        const QString expected = QStringLiteral("bitcoin:") + Address() + QStringLiteral("?amount=0.00000100&label=public&message=message");
        // Selected formatting is asynchronous. The address-only formatter
        // queued before all stored fields cannot publish as the final URI.
        QVERIFY(f.model->currentPaymentRequest()->qrPayload().isEmpty());
        f.env.drain(); QVERIFY(f.model->currentPaymentRequest()->qrPayload().isEmpty());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(f.model->currentPaymentRequest()->qrPayload().isEmpty());
        f.settle(); QCOMPARE(f.model->currentPaymentRequest()->qrPayload(), expected);
        QCOMPARE(f.wallet->loads, 1); QCOMPARE(f.wallet->writes, 0); QCOMPARE(f.wallet->generated, 0);
    }
    void clearAndPaymentRetireEarlierUriResults_data()
    {
        QTest::addColumn<bool>("paid");
        QTest::newRow("paid") << true; QTest::newRow("cleared") << false;
    }
    void clearAndPaymentRetireEarlierUriResults()
    {
        QFETCH(bool, paid);
        Fixture f; f.settle(); auto* target = f.model->currentPaymentRequest();
        target->setPreparedDestination(DecodeDestination(Address().toStdString()), Address());
        f.env.drain();
        if (paid) target->setPaymentReceived(true); else target->clear();
        QVERIFY(target->qrPayload().isEmpty());
        f.settle(); QVERIFY(target->qrPayload().isEmpty());
    }
    void rejectedUriAdmissionCannotKeepAnEarlierPayload()
    {
        Fixture f; f.settle(); auto* target = f.model->currentPaymentRequest();
        target->setPreparedDestination(DecodeDestination(Address().toStdString()), Address()); f.settle();
        QVERIFY(!target->qrPayload().isEmpty());
        f.env.rejectLane(execution::Lane::Domain); target->setLabel("rejected fields");
        QVERIFY(target->qrPayload().isEmpty()); f.settle(); QVERIFY(target->qrPayload().isEmpty());
        target->setLabel("retried fields"); f.settle();
        QCOMPARE(target->qrPayload(), QStringLiteral("bitcoin:") + Address() + QStringLiteral("?label=retried%20fields"));
    }
    void oversizedUriInputDoesNotEnterTheDomainQueue()
    {
        Fixture f; f.settle(); auto* target = f.model->currentPaymentRequest();
        target->setPreparedDestination(DecodeDestination(Address().toStdString()), Address()); f.settle();
        const auto pending = f.env.scheduler->pending();
        target->setLabel(QString(33'000, QLatin1Char{'x'}));
        QVERIFY(f.env.queuedIdle()); QVERIFY(f.env.activeIdle());
        QCOMPARE(f.env.scheduler->pending(), pending); QVERIFY(target->qrPayload().isEmpty());
        f.settle(); QVERIFY(target->qrPayload().isEmpty()); QCOMPARE(f.wallet->writes, 0);
        target->setLabel("bounded"); f.settle();
        QCOMPARE(target->qrPayload(), QStringLiteral("bitcoin:") + Address() + QStringLiteral("?label=bounded"));
    }
    void createAdmissionIsNotPersistenceOrGuiAcknowledgement()
    {
        Fixture f; f.settle();
        auto* draft = f.model->currentPaymentRequest();
        draft->setPreparedDestination(DecodeDestination(Address().toStdString()), Address());
        draft->setLabel("another");
        std::unique_ptr<ReceiveRequestOperation> operation{f.model->commitPaymentRequest()};
        QVERIFY(operation); QVERIFY(operation->pending());
        QCOMPARE(f.wallet->writes, 0); QVERIFY(draft->id().isEmpty());
        while (f.env.step(execution::Lane::Domain)) {}
        QCOMPARE(f.wallet->writes, 0);
        f.env.drain();
        QCOMPARE(f.wallet->writes, 1);
        QVERIFY(operation->pending()); QVERIFY(draft->id().isEmpty());
        f.settle();
        QVERIFY(!operation->pending()); QVERIFY(operation->requestSaved());
        QCOMPARE(operation->requestId(), QStringLiteral("2"));
        QCOMPARE(draft->id(), QStringLiteral("2"));
        QCOMPARE(f.model->receiveRequests()->count(), 2);
    }
    void savedCreateAndNoteFailureRetainTheSavedIdentity()
    {
        Fixture f; f.settle(); f.wallet->fail_label = true;
        auto* draft = f.model->currentPaymentRequest();
        draft->setPreparedDestination(DecodeDestination(Address().toStdString()), Address());
        draft->setNoteSelf("different private note");
        std::unique_ptr<ReceiveRequestOperation> operation{f.model->commitPaymentRequest()};
        QVERIFY(operation); f.settle();
        QVERIFY(operation->requestSaved()); QVERIFY(!operation->labelSaved());
        QCOMPARE(operation->error(), ReceiveRequestOperation::Error::LabelPersistence);
        QCOMPARE(operation->requestId(), QStringLiteral("2"));
        QCOMPARE(draft->id(), operation->requestId());
        QCOMPARE(f.wallet->stored.size(), size_t{2});
    }
    void competingCreatesKeepTheFirstSavedIdentityBeforeGuiCompletion()
    {
        Fixture f; f.settle();
        std::unique_ptr<ReceiveRequestOperation> reserved{f.model->ensureReceivingAddress()};
        QVERIFY(reserved); f.settle(); QVERIFY(reserved->addressReady());
        std::unique_ptr<ReceiveRequestOperation> first{f.model->commitReceivingPaymentRequest()};
        std::unique_ptr<ReceiveRequestOperation> second{f.model->commitReceivingPaymentRequest()};
        QVERIFY(first && second);
        f.env.drain();
        QCOMPARE(f.wallet->writes, 1);
        QVERIFY(first->pending() && second->pending());
        QVERIFY(f.model->currentPaymentRequest()->id().isEmpty());
        f.settle();
        QVERIFY(first->requestSaved()); QVERIFY(!second->requestSaved());
        QCOMPARE(second->error(), ReceiveRequestOperation::Error::AlreadySaved);
        QCOMPARE(first->requestId(), second->requestId());
        QCOMPARE(f.model->currentPaymentRequest()->id(), first->requestId());
        QVERIFY(!second->errorText().isEmpty());
    }
    void changedDraftCannotReceiveAnEarlierCreate()
    {
        Fixture f; f.settle();
        auto* draft = f.model->currentPaymentRequest();
        draft->setPreparedDestination(DecodeDestination(Address().toStdString()), Address());
        draft->setLabel("first draft");
        std::unique_ptr<ReceiveRequestOperation> operation{f.model->commitPaymentRequest()};
        QVERIFY(operation);
        draft->clear(); draft->setLabel("replacement draft");
        f.settle();
        QVERIFY(operation->requestSaved()); QCOMPARE(operation->requestId(), QStringLiteral("2"));
        QVERIFY(draft->id().isEmpty()); QCOMPARE(draft->label(), QStringLiteral("replacement draft"));
    }
    void disappearingProjectionDoesNotLoseTheEdit()
    {
        Fixture f; f.settle();
        auto* operation = f.model->updatePaymentRequest("1", 100, "public", "message", "edited privately");
        QVERIFY(operation); delete operation;
        QCOMPARE(f.wallet->writes, 0);
        f.settle();
        QCOMPARE(f.wallet->writes, 1);
        const auto stored = walletrequests::Decode({f.wallet->stored.at("1")});
        QCOMPARE(stored.entries.front().recipient.noteSelf, std::string{"edited privately"});
        QCOMPARE(f.model->receiveRequests()->entryById("1")->recipient.noteSelf, std::string{"edited privately"});
    }
    void eraseAndEditUseTheSameRevisionGuardedOwner()
    {
        Fixture f; f.settle();
        std::unique_ptr<ReceiveRequestOperation> erased{f.model->removeReceiveRequest("1")};
        std::unique_ptr<ReceiveRequestOperation> edited{f.model->updatePaymentRequest("1", 100, "public", "message", "late")};
        QVERIFY(erased && edited); f.settle();
        QVERIFY(erased->erased());
        QCOMPARE(edited->error(), ReceiveRequestOperation::Error::Missing);
        QCOMPARE(f.wallet->writes, 1); QVERIFY(f.wallet->stored.empty());
        QCOMPARE(f.model->receiveRequests()->count(), 0);
    }
    void backendReconciliationNeedsNoPollAfterGuiDelivery()
    {
        Fixture f; f.settle(); QVERIFY(f.wallet->transaction);
        f.wallet->transaction(Txid::FromUint256(uint256::ONE), CT_UPDATED);
        QVERIFY(f.model->receiveRequestReconciliationPending());
        // Deliver backend work first, then consume GUI publications without a
        // poll. Separate arrival-order tests still need controlled GUI ports.
        f.env.drain(); f.settle();
        QVERIFY(!f.model->receiveRequestReconciliationPending());
    }
    void explicitSelectionWaitsForNativeValidationAndGuiDelivery()
    {
        Fixture f; f.settle();
        uint160 key; key.begin()[0] = 42;
        const auto address = QString::fromStdString(EncodeDestination(PKHash{key}));
        f.model->currentPaymentRequest()->setLabel("existing draft");
        std::unique_ptr<ReceiveRequestOperation> operation{f.model->setCurrentPaymentRequestAddress(address)};
        QVERIFY(operation); QVERIFY(operation->pending());
        QCOMPARE(f.wallet->address_reads, 0); QCOMPARE(f.wallet->generated, 0);
        QCOMPARE(f.model->currentPaymentRequest()->label(), QStringLiteral("existing draft"));
        while (f.env.step(execution::Lane::Domain)) {}
        QCOMPARE(f.wallet->address_reads, 0);
        f.env.drain(); QCOMPARE(f.wallet->address_reads, 1); QCOMPARE(f.wallet->generated, 0);
        QVERIFY(operation->pending());
        QCOMPARE(f.model->currentPaymentRequest()->label(), QStringLiteral("existing draft"));
        f.settle(); QVERIFY(operation->addressReady());
        QCOMPARE(f.model->currentPaymentRequest()->address(), address);
        QCOMPARE(f.model->currentPaymentRequest()->noteSelf(), QStringLiteral("private"));
        QCOMPARE(f.model->receivingAddress()->address(), address);
        QVERIFY(f.model->property("receivingAddressReady").toBool());
        QCOMPARE(f.wallet->writes, 0);
    }
    void selectionCannotReplaceANewerDraft()
    {
        Fixture f; f.settle();
        uint160 key; key.begin()[0] = 42;
        std::unique_ptr<ReceiveRequestOperation> operation{f.model->setCurrentPaymentRequestAddress(QString::fromStdString(EncodeDestination(PKHash{key})))};
        QVERIFY(operation);
        f.model->currentPaymentRequest()->setLabel("newer draft");
        f.settle(); QVERIFY(operation->addressReady());
        QCOMPARE(f.model->currentPaymentRequest()->label(), QStringLiteral("newer draft"));
        QVERIFY(f.model->currentPaymentRequest()->address().isEmpty());
        QVERIFY(!f.model->property("receivingAddressReady").toBool());
    }
    void selectingASavedAddressLoadsTheAuthoritativeRequest()
    {
        Fixture f; f.settle();
        std::unique_ptr<ReceiveRequestOperation> operation{f.model->setCurrentPaymentRequestAddress(Address())};
        QVERIFY(operation); QVERIFY(operation->pending());
        f.env.drain(); QVERIFY(operation->pending());
        QVERIFY(f.model->currentPaymentRequest()->id().isEmpty());
        f.settle(); QVERIFY(operation->requestLoaded()); QVERIFY(!operation->requestSaved());
        QCOMPARE(f.model->currentPaymentRequest()->id(), QStringLiteral("1"));
        QCOMPARE(f.model->currentPaymentRequest()->label(), QStringLiteral("public"));
        QVERIFY(f.model->currentPaymentRequest()->isEditing());
        QCOMPARE(f.wallet->address_reads, 0); QCOMPARE(f.wallet->generated, 0); QCOMPARE(f.wallet->writes, 0);
    }
    void templateResetAndImmediateReserveUseOrderedNativeStorage()
    {
        Fixture f; f.settle();
        std::unique_ptr<ReceiveRequestOperation> first{f.model->ensureReceivingAddress()};
        f.settle(); QVERIFY(first->addressReady());
        const auto previous = f.model->receivingAddress()->address();
        const QString settings_key{"receiveAddressTypes/request-consumer/address"};
        QCOMPARE(QSettings{}.value(settings_key).toString(), previous);
        f.model->usePaymentRequestAsTemplate("1");
        QVERIFY(f.model->receivingAddress()->address().isEmpty());
        QCOMPARE(f.model->currentPaymentRequest()->label(), QStringLiteral("public"));
        QCOMPARE(f.model->currentPaymentRequest()->addressType(), QStringLiteral("legacy"));
        QCOMPARE(QSettings{}.value(settings_key).toString(), previous);
        QCOMPARE(f.wallet->generated, 1);
        std::unique_ptr<ReceiveRequestOperation> next{f.model->ensureReceivingAddress()};
        f.settle(); QVERIFY(next->addressReady()); QCOMPARE(f.wallet->generated, 2);
        QVERIFY(f.model->receivingAddress()->address() != previous);
        QCOMPARE(QSettings{}.value(settings_key).toString(), f.model->receivingAddress()->address());
        QCOMPARE(f.wallet->writes, 0);
    }
};

BITCOINQML_REGISTER_QT_TEST(ReceiveRequestOperationTests)
#include <test_receiverequestoperation.moc>
