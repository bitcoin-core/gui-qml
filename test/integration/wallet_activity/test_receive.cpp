// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <qml/models/activityfilterproxymodel.h>

#include <QSignalSpy>

class ReceiveIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

    QObject* addressRow(const QString& address)
    {
        QList<QQuickItem*> pending{m_app.window()->contentItem()};
        while (!pending.isEmpty()) {
            auto* item = pending.takeLast();
            if (item->objectName() == "addressRow" && item->property("address").toString() == address) return item;
            pending.append(item->childItems());
        }
        return nullptr;
    }

    bool hasTransaction(WalletQmlModel& wallet, const QString& txid)
    {
        auto* activity = wallet.transactionActivityModel();
        for (int row{0}; row < activity->rowCount(); ++row) {
            if (activity->index(row, 0).data(TransactionActivityModel::TxidRole).toString() == txid) return true;
        }
        return false;
    }

public:
    explicit ReceiveIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void privateNotesSyncWithoutChangingPublicPaymentMetadata()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        auto* request = wallet->currentPaymentRequest();
        request->amount()->setSatoshi(10'000);
        request->setLabel("Alice");
        request->setMessage("pizza");
        request->setNoteSelf("Private lunch note");
        QVERIFY(wallet->commitPaymentRequest());
        const auto id = request->id();
        const auto address = request->address();
        const auto uri = request->qrPayload();
        QVERIFY(!id.isEmpty());
        QVERIFY(!address.isEmpty());
        QVERIFY(!uri.contains("Private"));
        QCOMPARE(wallet->receiveRequests()->count(), 1);
        QCOMPARE(wallet->getAddressLabel(address), QStringLiteral("Private lunch note"));
        QSignalSpy changed{wallet->receiveRequests(), &QAbstractItemModel::dataChanged};
        QVERIFY(wallet->addressListModel()->setAddressLabel(address, "Renamed on Addresses page"));
        QTRY_VERIFY(!changed.isEmpty());
        QVERIFY(wallet->loadPaymentRequestDetail(id));
        QCOMPARE(wallet->detailPaymentRequest()->noteSelf(), QStringLiteral("Renamed on Addresses page"));
        QCOMPARE(wallet->detailPaymentRequest()->label(), QStringLiteral("Alice"));
        QCOMPARE(wallet->detailPaymentRequest()->message(), QStringLiteral("pizza"));
        QCOMPARE(wallet->detailPaymentRequest()->qrPayload(), uri);
        QVERIFY(qmltest::Click(m_app, "activityTabButton"));
        QVERIFY(qmltest::SetText(m_app, "activitySearchField", "Renamed on Addresses page"));
        auto* filter = qobject_cast<ActivityFilterProxyModel*>(qmltest::Find(m_app, "activityFilterProxyModel"));
        QVERIFY(filter);
        QTRY_COMPARE(filter->rowCount(), 1);
        QCOMPARE(filter->data(filter->index(0, 0), TransactionActivityModel::LabelRole).toString(), QStringLiteral("Renamed on Addresses page"));
        wallet->reloadReceiveRequests();
        QVERIFY(wallet->loadPaymentRequestDetail(id));
        QCOMPARE(wallet->detailPaymentRequest()->noteSelf(), QStringLiteral("Renamed on Addresses page"));
        QCOMPARE(wallet->detailPaymentRequest()->qrPayload(), uri);
        QVERIFY(wallet->removeReceiveRequest(id));
        QCOMPARE(wallet->receiveRequests()->count(), 0);
        QVERIFY(!wallet->loadPaymentRequestDetail(id));
    }

    void receivingAddressCanBeReusedOrReplaced()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        QVERIFY(wallet->ensureReceivingAddress());
        const auto address = wallet->receivingAddress()->address();
        QVERIFY(!address.isEmpty());
        QVERIFY(wallet->ensureReceivingAddress());
        QCOMPARE(wallet->receivingAddress()->address(), address);
        QVERIFY(wallet->ensureReceivingAddress(true));
        QVERIFY(wallet->receivingAddress()->address() != address);
        const auto types = wallet->availableReceiveAddressTypes();
        QVERIFY(!types.isEmpty());
        QVERIFY(wallet->ensureReceivingAddress(true, "legacy"));
        const auto legacy = wallet->receivingAddress()->address();
        QVERIFY(legacy.startsWith('m') || legacy.startsWith('n'));
    }

    void signingUnlocksOnlyForTheOperation()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name(), "legacy");
        QVERIFY(wallet->encryptWallet("signing password"));
        auto* signer = wallet->signVerifyMessageModel();
        QVERIFY(!signer->signMessage(address, "integration message"));
        QVERIFY(signer->signingNeedsUnlock());
        QVERIFY(!signer->signMessageWithPassphrase(address, "integration message", "incorrect"));
        QVERIFY(signer->signMessageWithPassphrase(address, "integration message", "signing password"));
        const auto signature = signer->signature();
        QVERIFY(!signature.isEmpty());
        QVERIFY(signer->verifyMessage(address, "integration message", signature));
        QVERIFY(!signer->verifyMessage(address, "changed message", signature));
        QVERIFY(wallet->isLocked());
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("unlocked_until").getInt<int64_t>(), 0);
    }

    void addressDetailsCreateAndUpdateTheSamePaymentRequest()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        const auto address = QString::fromStdString(qmltest::Rpc(m_app, "getnewaddress", qmltest::Json("[\"invoice 1024\"]"), wallet->name()).get_str());
        QVERIFY(qmltest::SelectSettings(m_app, "wallet"));
        QVERIFY(qmltest::Click(m_app, "walletAddressesRow"));
        QVERIFY(qmltest::WaitForVisible(m_app, "addressListPage"));
        QTRY_VERIFY(addressRow(address));
        QVERIFY(qmltest::Click(qmltest::Find(addressRow(address), "addressRowDetailsButton")));
        QVERIFY(qmltest::WaitForVisible(m_app, "addressDetailsPopup"));
        QVERIFY(qmltest::Click(m_app, "addressDetailsCreatePaymentRequestButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "requestPaymentPage"));
        QCOMPARE(wallet->receivingAddress()->address(), address);
        QCOMPARE(qmltest::Find(m_app, "requestPaymentNoteRow")->property("value").toString(), QStringLiteral("invoice 1024"));
        QVERIFY(qmltest::SetText(m_app, "requestPaymentAmountInput", "0.25"));
        QVERIFY(QMetaObject::invokeMethod(qmltest::Find(m_app, "requestPaymentAmountInput"), "editingFinished"));
        QVERIFY(qmltest::Click(m_app, "requestPaymentGenerateButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "paymentRequestModal"));
        QCOMPARE(wallet->receiveRequests()->count(), 1);
        QVERIFY(qmltest::Click(m_app, "paymentRequestModalClose"));

        // The Addresses stack is retained, and its action now views the saved request.
        QVERIFY(qmltest::Click(m_app, "desktopWalletSettingsTabButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "addressListPage"));
        QTRY_VERIFY(addressRow(address));
        QVERIFY(qmltest::Click(qmltest::Find(addressRow(address), "addressRowDetailsButton")));
        auto* action = qmltest::WaitForVisible(m_app, "addressDetailsCreatePaymentRequestButton");
        QVERIFY(action);
        QCOMPARE(action->property("text").toString(), QStringLiteral("View payment request"));
        QVERIFY(qmltest::Click(action));
        QVERIFY(qmltest::WaitForVisible(m_app, "paymentRequestModal"));
        QVERIFY(qmltest::SetText(m_app, "requestPaymentAmountInput", "0.5"));
        QVERIFY(QMetaObject::invokeMethod(qmltest::Find(m_app, "requestPaymentAmountInput"), "editingFinished"));
        QCOMPARE(wallet->receiveRequests()->count(), 1);
        QVERIFY(qmltest::Click(m_app, "requestPaymentUpdateButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "requestPaymentCopyButton"));
        QCOMPARE(wallet->receiveRequests()->count(), 1);
        QCOMPARE(qmltest::Find(m_app, "requestPaymentAddressText")->property("address").toString(), address);
        QVERIFY(qmltest::Click(m_app, "paymentRequestModalClose"));
        QVERIFY(qmltest::Click(m_app, "desktopWalletSettingsTabButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "addressListPage"));
    }

    void replacementAndConfirmedPaymentsDoNotDoubleCountReceipts()
    {
        auto* payer = qmltest::CreateWallet(m_app, "payer");
        QVERIFY(payer);
        QVERIFY(qmltest::FundWallet(m_app, *payer));
        auto* receiver = qmltest::CreateWallet(m_app, "receiver");
        QVERIFY(receiver);
        auto* request = receiver->currentPaymentRequest();
        request->amount()->setSatoshi(10'000);
        request->setMessage("pizza");
        request->setNoteSelf("Private lunch note");
        QVERIFY(receiver->commitPaymentRequest());
        const auto id = request->id();
        const auto address = request->address();
        QVERIFY(receiver->loadPaymentRequestDetail(id));
        auto* detail = receiver->detailPaymentRequest();

        const auto original = qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + address.toStdString() + "\",\"amount\":0.00001,\"fee_rate\":1}"), payer->name()).get_str();
        QTRY_VERIFY_WITH_TIMEOUT(detail->paymentReceived(), 30'000);
        QTRY_COMPARE(detail->receivedAmountSatoshi(), 1'000);
        QVERIFY(detail->qrPayload().isEmpty());
        const auto replacement = qmltest::Rpc(m_app, "bumpfee", qmltest::Json("[\"" + original + "\",{\"fee_rate\":20}]"), payer->name());
        const auto replacement_id = QString::fromStdString(replacement.find_value("txid").get_str());
        QVERIFY(replacement_id != QString::fromStdString(original));
        QTRY_VERIFY_WITH_TIMEOUT(hasTransaction(*receiver, replacement_id), 30'000);
        QCOMPARE(detail->receivedAmountSatoshi(), 1'000);

        const auto mining_address = qmltest::NewAddress(m_app, payer->name());
        const auto mine = [&] {
            qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + mining_address.toStdString() + "\"]"));
            // Mining returns before wallet notifications and reconciliation finish.
            qmltest::Rpc(m_app, "syncwithvalidationinterfacequeue");
        };
        mine();
        QTRY_VERIFY_WITH_TIMEOUT(!receiver->receiveRequestReconciliationPending(), 30'000);
        QCOMPARE(detail->receivedAmountSatoshi(), 1'000);
        const auto second = QString::fromStdString(qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + address.toStdString() + "\",\"amount\":0.00002,\"fee_rate\":1}"), payer->name()).get_str());
        QTRY_VERIFY_WITH_TIMEOUT(hasTransaction(*receiver, second), 30'000);
        QTRY_COMPARE(detail->receivedAmountSatoshi(), 3'000);
        mine();
        QTRY_VERIFY_WITH_TIMEOUT(!receiver->receiveRequestReconciliationPending(), 30'000);
        QCOMPARE(detail->receivedAmountSatoshi(), 3'000);
        // Saves can also be refused while a queued transaction notification is
        // pending. Retry that busy result without changing the public metadata.
        QTRY_VERIFY_WITH_TIMEOUT(receiver->updatePaymentRequest(id, 10'000, {}, "pizza", "Partial payment received"), 30'000);
        QVERIFY(receiver->loadPaymentRequestDetail(id));
        QCOMPARE(detail->noteSelf(), QStringLiteral("Partial payment received"));
        QCOMPARE(detail->message(), QStringLiteral("pizza"));
        QCOMPARE(receiver->receiveRequests()->count(), 1);
        QVERIFY(detail->paymentReceived());
    }
};

std::unique_ptr<QObject> CreateReceiveTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<ReceiveIntegrationTests>(app);
}

#include <test_receive.moc>
