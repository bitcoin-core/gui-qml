// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/paymentrequesteditormodel.h>
#include <qml/models/requestimageexportoperation.h>
#include <qml/models/requestsharingmodel.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/models/walletreceivemodel.h>
#include <qml/walletsettings.h>
#include <test/mocks/walletmodelservices.h>
#include <test/mocks/manualnodeobservations.h>
#include <test/mocks/manualwalletdrain.h>
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
    //! Address book labels by address; other owned addresses have none.
    std::map<std::string, std::string> labels{{Address().toStdString(), "private"}};
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
    bool getAddress(const CTxDestination& destination, std::string* label, wallet::AddressPurpose* purpose) override {
        ++address_reads;
        if (label) { const auto found = labels.find(EncodeDestination(destination)); *label = found == labels.end() ? std::string{} : found->second; }
        if (purpose) *purpose = wallet::AddressPurpose::RECEIVE;
        return true;
    }
    bool setAddressBook(const CTxDestination& destination, const std::string& label, const std::optional<wallet::AddressPurpose>&) override {
        if (fail_label) return false;
        labels[EncodeDestination(destination)] = label; return true;
    }
    bool setAddressReceiveRequest(const CTxDestination&, const std::string& id, const std::string& blob) override {
        ++writes; if (blob.empty()) stored.erase(id); else stored[id] = blob; return true;
    }
};
struct Fixture {
    QJsonArray trace;
    ManualNodeObservations env{[this](execution::Lane lane) { trace.append(int(lane)); }};
    std::shared_ptr<Wallet> wallet{std::make_shared<Wallet>()};
    std::unique_ptr<WalletQmlModel> model{new WalletQmlModel{
        WalletWithQueues(wallet, env.executor(), {}, "request-consumer"), nullptr,
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
            if (env.turnsIdle()) return;
        }
        QFAIL("Receive projection did not settle within128 controlled publication turns");
    }
    //! Opens saved request \p id in its own editor, as its detail page does.
    PaymentRequestEditorModel* open(const QString& id)
    {
        QObject owner;
        const QPointer<RequestOpenOperation> operation{model->receive()->openRequest(id, &owner)};
        for (int round{0}; round < 16 && operation && operation->pending(); ++round) settle();
        return operation && operation->state() == RequestOpenOperation::State::Opened ? operation->editor() : nullptr;
    }
    //! The Receive page's draft once its address is reserved.
    PaymentRequestEditorModel* draft()
    {
        auto* receive = model->receive();
        receive->ensureAddress(); settle();
        return receive->reservation()->available() && receive->editor() && receive->editor()->canSave() ? receive->editor() : nullptr;
    }
    ~Fixture()
    {
        const auto native_drained = WatchWalletNativeDrain(*model);
        model->beginShutdown();
        const bool drained = QTest::qWaitFor([&] { settle(); return *native_drained; }, 3000);
        QVERIFY(drained);
        settle();
        QVERIFY(env.turnsIdle());
        const auto directory = qEnvironmentVariable("BITCOIN_QML_MANUAL_TRACE_DIR");
        if (!directory.isEmpty()) {
            QDir{}.mkpath(directory);
            QFile file{directory + QLatin1Char('/') + QString::fromLatin1(QTest::currentTestFunction()) + QStringLiteral(".json")};
            QVERIFY(file.open(QIODevice::WriteOnly));
            const auto bytes = QJsonDocument{QJsonObject{{"commands", trace}, {"loads", wallet->loads},
                {"writes", wallet->writes}, {"failed", QTest::currentTestFailed()}}}.toJson();
            QCOMPARE(file.write(bytes), bytes.size());
        }
    }
};
}

// Typed receive coverage for behaviors these tests had for the removed
// PaymentRequest projection: load and template re-entrancy and replacement
// (WalletReceiveModelTests: keepEditingCancelsTheCapturedReplacement,
// dirtyAddressNavigationCapturesOneOriginalTarget,
// delayedDeleteOfOriginalRequestDoesNotCloseANewerEditor,
// failedOpenDoesNotPublishAPhantomEditor), payment URI preparation
// (RequestSharingModelTests: freshSavedPayloadCopiesOnlyAfterTheNativeGuiHandoff,
// firstInputRevocationPreventsCopyBeforeTheEditedProjectionArrives,
// discardedInputCannotRearmAnOlderQueuedClipboardGrant,
// nativeUseBeforeGuiPublicationFencesTheOriginalPayload,
// paidSourceBeforeNativeInstallNeverWritesTheTarget;
// PaymentRequestEditorModelTests: sourceOversizeFailsClosedWithoutTruncatingSavedData),
// a changed draft keeping its own input
// (saveCapturesEachOriginalFieldAndRetainsSuccessorInput), and an edit
// outliving its projection (disappearingProjectionDoesNotDiscardTheNativeWrite).
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
    void qrExportWritesTheSavedRequestAndStopsWhenTheViewHides()
    {
        Fixture f; f.settle(); QObject view;
        auto* sharing = f.model->receive()->sharingRequest(QStringLiteral("1"), &view); QVERIFY(sharing);
        sharing->setActive(true); f.settle();
        QVERIFY(sharing->canExportQr());
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString path = directory.path() + QStringLiteral("/request.png");
        const QPointer<RequestImageExportOperation> operation{sharing->beginQrExport()}; QVERIFY(operation);
        f.settle(); QVERIFY(operation->choosing());
        QVERIFY(operation->choose(QUrl::fromLocalFile(path)));
        QVERIFY(!QFile::exists(path));
        QVERIFY(QTest::qWaitFor([&] { f.settle(); return operation->result().outcomeKnown && !operation->busy(); }, 5000));
        QVERIFY(operation->result().written); QCOMPARE(operation->result().requestId, QStringLiteral("1"));
        QVERIFY(!QImage{path}.isNull());
        // Hiding the view cancels an export that has not written yet.
        const QString hidden_path = directory.path() + QStringLiteral("/hidden.png");
        const QPointer<RequestImageExportOperation> hidden{sharing->beginQrExport()}; QVERIFY(hidden);
        f.settle(); QVERIFY(hidden->choosing());
        sharing->setActive(false); f.settle();
        QVERIFY(!hidden || !hidden->choose(QUrl::fromLocalFile(hidden_path)));
        f.settle();
        QVERIFY(!QFile::exists(hidden_path)); QVERIFY(!hidden || !hidden->result().written);
        QCOMPARE(f.wallet->writes, 0);
    }
    void createAdmissionIsNotPersistenceOrGuiAcknowledgement()
    {
        Fixture f; f.settle();
        auto* draft = f.draft(); QVERIFY(draft);
        draft->publicLabel()->edit("another"); f.settle();
        draft->save();
        QVERIFY(draft->busy()); QCOMPARE(f.wallet->writes, 0); QVERIFY(draft->requestId().isEmpty());
        while (f.env.step(execution::Lane::Utility)) {}
        QCOMPARE(f.wallet->writes, 0);
        f.env.drain();
        QCOMPARE(f.wallet->writes, 1);
        QVERIFY(!draft->saveResult().requestSaved); QVERIFY(draft->requestId().isEmpty());
        f.settle();
        QVERIFY(draft->saveResult().requestSaved);
        QCOMPARE(draft->saveResult().requestId, QStringLiteral("2"));
        QCOMPARE(f.model->receiveRequests()->count(), 2);
    }
    void savedCreateAndNoteFailureRetainTheSavedIdentity()
    {
        Fixture f; f.settle(); f.wallet->fail_label = true;
        auto* draft = f.draft(); QVERIFY(draft);
        draft->privateNote()->edit("different private note"); f.settle();
        draft->save(); f.settle();
        QVERIFY(draft->saveResult().requestSaved); QVERIFY(!draft->saveResult().labelSaved);
        QCOMPARE(draft->saveResult().error, ReceiveError::Code::LabelPersistence);
        QCOMPARE(draft->saveResult().requestId, QStringLiteral("2"));
        QCOMPARE(f.wallet->stored.size(), size_t{2});
    }
    void aSecondSaveWhileTheFirstRunsIsRefused()
    {
        Fixture f; f.settle();
        auto* draft = f.draft(); QVERIFY(draft);
        draft->publicLabel()->edit("first"); f.settle();
        draft->save(); draft->save();
        QCOMPARE(draft->error().code, TextEditError::Refused);
        f.settle();
        QVERIFY(draft->saveResult().requestSaved);
        QCOMPARE(f.wallet->writes, 1); QCOMPARE(f.model->receiveRequests()->count(), 2);
    }
    void eraseAndEditUseTheSameRevisionGuardedOwner()
    {
        Fixture f; f.settle();
        QPointer<PaymentRequestEditorModel> editor{f.open(QStringLiteral("1"))}; QVERIFY(editor);
        editor->privateNote()->edit("late"); f.settle(); QVERIFY(editor->canSave());
        std::unique_ptr<ReceiveRequestOperation> erased{f.model->removeReceiveRequest(QStringLiteral("1"))};
        QVERIFY(erased);
        editor->save(); f.settle();
        QVERIFY(erased->erased());
        QVERIFY(!editor || !editor->saveResult().requestSaved);
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
        f.wallet->labels[address.toStdString()] = "private";
        auto* receive = f.model->receive();
        QVERIFY(receive->useAddress(address));
        QCOMPARE(f.wallet->address_reads, 0); QCOMPARE(f.wallet->generated, 0);
        while (f.env.step(execution::Lane::Utility)) {}
        QCOMPARE(f.wallet->address_reads, 0);
        f.settle();
        QVERIFY(f.wallet->address_reads > 0); QCOMPARE(f.wallet->generated, 0);
        QVERIFY(receive->reservation()->available());
        QCOMPARE(receive->reservation()->address(), address);
        QCOMPARE(receive->editor()->address(), address);
        QCOMPARE(receive->editor()->privateNote()->text(), QStringLiteral("private"));
        QCOMPARE(f.wallet->writes, 0);
    }
    void selectingASavedAddressLoadsTheAuthoritativeRequest()
    {
        Fixture f; f.settle();
        auto* receive = f.model->receive();
        QVERIFY(receive->useAddress(Address()));
        f.env.drain(); QVERIFY(receive->reservation()->selectedRequestId().isEmpty());
        f.settle();
        QCOMPARE(receive->reservation()->selectedRequestId(), QStringLiteral("1"));
        QPointer<PaymentRequestEditorModel> request{f.open(QStringLiteral("1"))}; QVERIFY(request);
        QCOMPARE(request->publicLabel()->text(), QStringLiteral("public"));
        QCOMPARE(request->mutationKind(), PaymentRequestEditorModel::Update);
        QCOMPARE(f.wallet->address_reads, 0); QCOMPARE(f.wallet->generated, 0); QCOMPARE(f.wallet->writes, 0);
    }
    void templateResetAndImmediateReserveUseOrderedNativeStorage()
    {
        Fixture f; f.settle();
        auto* receive = f.model->receive();
        QVERIFY(f.draft());
        const auto previous = receive->reservation()->address();
        const QString settings_key{"receiveAddressTypes/request-consumer/address"};
        QCOMPARE(QSettings{}.value(settings_key).toString(), previous);
        QCOMPARE(f.wallet->generated, 1);
        // The template's legacy type gets its own fresh address; the saved
        // reservation is replaced only once that address is stored.
        QVERIFY(receive->useTemplate(QStringLiteral("1"))); f.settle();
        QCOMPARE(receive->editor()->publicLabel()->text(), QStringLiteral("public"));
        QCOMPARE(receive->reservation()->addressType(), QStringLiteral("legacy"));
        QVERIFY(receive->reservation()->available());
        QCOMPARE(f.wallet->generated, 2);
        QVERIFY(receive->reservation()->address() != previous);
        QCOMPARE(QSettings{}.value(settings_key).toString(), receive->reservation()->address());
        QCOMPARE(f.wallet->writes, 0);
    }
};

BITCOINQML_REGISTER_QT_TEST(ReceiveRequestOperationTests)
#include <test_receiverequestoperation.moc>
