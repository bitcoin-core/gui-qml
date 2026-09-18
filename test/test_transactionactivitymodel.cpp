// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/activityfilterproxymodel.h>
#include <qml/models/transactionactivitymodel.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/walletqmlmodel.h>
#include <test/mocks/mockwallet.h>

#include <core_io.h>
#include <key_io.h>
#include <script/solver.h>

#include <QAbstractItemModelTester>
#include <QFile>
#include <QPersistentModelIndex>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <thread>

namespace {
using Model = TransactionActivityModel;
using Proxy = ActivityFilterProxyModel;

struct Output { CAmount amount; bool mine; bool change{false}; unsigned char key{1}; };

interfaces::WalletTx MakeTx(const std::vector<std::pair<CAmount, bool>>& inputs,
                          const std::vector<Output>& outputs, qint64 time = 1'789'200'000)
{
    interfaces::WalletTx wtx{};
    CMutableTransaction tx;
    for (const auto& [amount, mine] : inputs) {
        tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256{uint8_t(tx.vin.size() + 1)}), 0});
        wtx.txin_is_mine.push_back(mine);
        if (mine) wtx.debit += amount;
    }
    for (const auto& output : outputs) {
        uint160 key;
        key.begin()[0] = output.key;
        const CTxDestination address{PKHash{key}};
        tx.vout.emplace_back(output.amount, GetScriptForDestination(address));
        wtx.txout_address.push_back(address);
        wtx.txout_address_is_mine.push_back(output.mine);
        wtx.txout_is_mine.push_back(output.mine);
        wtx.txout_is_change.push_back(output.change);
        if (output.mine) wtx.credit += output.amount;
        if (output.change) wtx.change += output.amount;
    }
    wtx.tx = MakeTransactionRef(tx);
    wtx.time = time;
    return wtx;
}

QString Id(const interfaces::WalletTx& tx) { return QString::fromStdString(tx.tx->GetHash().ToString()); }
QString Address(const interfaces::WalletTx& tx, int output = 0) { return QString::fromStdString(EncodeDestination(tx.txout_address[output])); }
qint64 Time(int month, int day, int hour = 12) { return QDateTime(QDate(2026, month, day), QTime(hour, 3)).toSecsSinceEpoch(); }

QmlRecentRequestEntry Request(int id, const QString& address, const QString& label = "Rent", CAmount amount = 50'000)
{
    QmlRecentRequestEntry entry;
    entry.id = id;
    entry.date = QDateTime::fromSecsSinceEpoch(Time(9, 11, 9));
    entry.recipient.address = address.toStdString();
    entry.recipient.label = label.toStdString();
    entry.recipient.amount = amount;
    return entry;
}

class ActivityWallet : public StubWallet
{
public:
    std::map<Txid, interfaces::WalletTx> transactions;
    std::map<Txid, interfaces::WalletTxStatus> statuses;
    std::map<std::string, std::string> labels;
    std::vector<TransactionChangedFn> callbacks;
    std::map<std::string, wallet::AddressPurpose> purposes;
    std::optional<interfaces::WalletTx> send_draft;
    CTransactionRef previous_tx;
    std::map<Txid, CTransactionRef> flow_parents;
    int parent_reads{0};
    bool external_signer{false};
    int address_writes{0};
    int commits{0};
    bool busy{false};
    bool snapshot_on_worker{false};
    uint256 tip{1};
    int status_reads{0};
    int label_reads{0};
    int snapshots{0};

    void put(const interfaces::WalletTx& tx, int depth = 6)
    {
        transactions[tx.tx->GetHash()] = tx;
        auto& status = statuses[tx.tx->GetHash()];
        status = {};
        status.depth_in_main_chain = depth;
        status.is_in_main_chain = depth > 0;
    }
    void notify(const interfaces::WalletTx& tx, ChangeType change)
    {
        for (const auto& callback : callbacks) callback(tx.tx->GetHash(), change);
    }
    std::set<interfaces::WalletTx> getWalletTxs() override
    {
        std::set<interfaces::WalletTx> result;
        for (const auto& [id, tx] : transactions) result.insert(tx);
        return result;
    }
    interfaces::WalletTx getWalletTx(const Txid& id) override
    {
        ++snapshots;
        snapshot_on_worker |= QThread::currentThread() != qApp->thread();
        const auto it = transactions.find(id);
        return it == transactions.end() ? interfaces::WalletTx{} : it->second;
    }
    CTransactionRef getTx(const Txid& id) override
    {
        ++parent_reads;
        const auto it = flow_parents.find(id);
        return it == flow_parents.end() ? CTransactionRef{} : it->second;
    }
    bool tryGetTxStatus(const Txid& id, interfaces::WalletTxStatus& status, int& height, int64_t& time) override
    {
        ++status_reads;
        if (busy || !statuses.count(id)) return false;
        status = statuses.at(id);
        height = 100;
        time = 1'789'200'000;
        return true;
    }
    bool tryGetBalances(interfaces::WalletBalances&, uint256& hash) override
    {
        if (busy) return false;
        hash = tip;
        return true;
    }
    bool getAddress(const CTxDestination& address, std::string* label, wallet::AddressPurpose* purpose) override
    {
        ++label_reads;
        if (!labels.count(EncodeDestination(address))) return false;
        if (label) *label = labels.at(EncodeDestination(address));
        if (purpose) *purpose = purposes[EncodeDestination(address)];
        return true;
    }
    bool setAddressBook(const CTxDestination& address, const std::string& label,
                        const std::optional<wallet::AddressPurpose>& purpose) override
    {
        ++address_writes;
        labels[EncodeDestination(address)] = label;
        if (purpose) purposes[EncodeDestination(address)] = *purpose;
        return true;
    }
    CAmount getAvailableBalance(const wallet::CCoinControl&) override { return 10 * COIN; }
    CAmount getBalance() override { return 10 * COIN; }
    bool privateKeysDisabled() override { return external_signer; }
    bool hasExternalSigner() override { return external_signer; }
    util::Result<wallet::CreatedTransactionResult> createTransaction(const std::vector<wallet::CRecipient>&,
        const wallet::CCoinControl&, bool, std::optional<unsigned int>) override
    {
        if (!send_draft) return util::Error{Untranslated("No send draft")};
        return wallet::CreatedTransactionResult{send_draft->tx, 1'000, 3, FeeCalculation{}};
    }
    std::optional<common::PSBTError> fillPSBT(const common::PSBTFillOptions& options, size_t*,
                                            PartiallySignedTransaction& psbt, bool& complete) override
    {
        complete = options.sign;
        for (auto& input : psbt.inputs) {
            input.non_witness_utxo = previous_tx;
            if (complete) input.final_script_sig = CScript{} << std::vector<unsigned char>{};
        }
        return std::nullopt;
    }
    void commitTransaction(CTransactionRef tx, interfaces::WalletValueMap, interfaces::WalletOrderForm) override
    {
        ++commits;
        send_draft->tx = std::move(tx);
        put(*send_draft, 0);
        notify(*send_draft, CT_NEW);
    }
    bool transactionCanBeBumped(const Txid&) override { return true; }
    std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn callback) override
    {
        callbacks.push_back(std::move(callback));
        return {};
    }
};

struct Fixture {
    ActivityWallet* state;
    std::unique_ptr<WalletQmlModel> wallet;
    Fixture()
    {
        auto backend = std::make_unique<ActivityWallet>();
        state = backend.get();
        wallet = std::make_unique<WalletQmlModel>(std::move(backend));
    }
    Model* model() { return wallet->transactionActivityModel(); }
    void request(const QmlRecentRequestEntry& request) { wallet->receiveRequests()->prependOrReplace(request); }
};

QModelIndex Find(const QAbstractItemModel& model, const QString& txid)
{
    for (int i = 0; i < model.rowCount(); ++i) {
        const auto index = model.index(i, 0);
        if (index.data(Model::TxidRole).toString() == txid) return index;
    }
    return {};
}
} // namespace

class TransactionActivityModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void copiesRawTransactionAndPublicPaymentRequest();
    void exposesParentsAndActionsWithoutAllocatingFees();
    void savesBatchRecipientNotesWhenSending_data();
    void savesBatchRecipientNotesWhenSending();
    void filtersWholeTransactionsAndExportsParentImpact();
    void exportsBatchActionsWithoutDuplicatingFees();
    void groupsPendingMonthsAndDaysAndCountsParents();
    void groupsTransactionsInPendingUntilFirstConfirmation();
    void totalsPendingWalletImpactWithoutRequests();
    void movesReplacedBatchToHistoryAndKeepsReplacementPending();
    void associatesRequestsPerOutputAndUpdatesLive();
    void usesPrivateRequestNotesWithoutChangingStoredData();
    void restoresRequestsOnConflictAbandonmentReplacementAndDeletion();
    void sharesAddressAssociationsWithoutDuplicatingParents();
    void preservesStatusWhenBusyAndRetries();
    void refreshesOnWalletTipAndSameHeightReorg();
    void queuesWalletNotificationsAndRetainsStableIndexes();
    void handlesInternalAndMinedTypes();
    void recognizesSelfSendToPaymentRequest_data();
    void recognizesSelfSendToPaymentRequest();
    void loadsFlowLazilyAndRefreshesOutputAssociations();
};

void TransactionActivityModelTests::copiesRawTransactionAndPublicPaymentRequest()
{
    Fixture f;
    const auto tx = MakeTx({{100'000, true}}, {{99'000, false}});
    f.state->put(tx);
    auto request = Request(1, Address(tx), "Public label", 10'000);
    request.recipient.message = "Public message";
    request.recipient.noteSelf = "Private note";
    f.request(request);
    const auto* model = f.model();
    QCOMPARE(model->rawTransaction(Id(tx)), QString::fromStdString(EncodeHexTx(*tx.tx)));
    QVERIFY(model->rawTransaction("missing").isEmpty());
    const auto uri = model->paymentRequestUri("1");
    QCOMPARE(uri, ReceiveRequestHistoryModel::BuildBitcoinUri(Address(tx), 10'000, "Public label", "Public message"));
    QVERIFY(!uri.contains("Private"));
    QVERIFY(model->paymentRequestUri("missing").isEmpty());
}

void TransactionActivityModelTests::loadsFlowLazilyAndRefreshesOutputAssociations()
{
    Fixture f;
    const auto tx = MakeTx({{100'000, true}}, {{70'000, false}, {29'000, true, true, 2}});
    CMutableTransaction parent;
    parent.vout.emplace_back(100'000, tx.tx->vout[1].scriptPubKey);
    f.state->flow_parents[tx.tx->vin[0].prevout.hash] = MakeTransactionRef(parent);
    const QString input_address = Address(tx, 1);
    f.state->labels[input_address.toStdString()] = "Savings";
    f.state->put(tx, 2);
    f.state->statuses[tx.tx->GetHash()].block_height = 100;
    auto* model = f.model();
    QVERIFY(!model->transactionDetails(Id(tx)).contains("flow"));
    QCOMPARE(f.state->parent_reads, 0);
    auto details = model->transactionDetails(Id(tx), true);
    QCOMPARE(f.state->parent_reads, 1);
    QCOMPARE(details.value("rawTransaction").toString(), QString::fromStdString(EncodeHexTx(*tx.tx)));
    QCOMPARE(model->transactionDetails(Id(tx), true).value("rawTransaction"), details.value("rawTransaction"));
    QVERIFY(!model->transactionDetails(Id(tx)).contains("rawTransaction"));
    QCOMPARE(details.value("blockHeight").toInt(), 100);
    QCOMPARE(details.value("feeSat").toLongLong(), 1'000);
    QVERIFY(details.value("feeRateSatPerVb").toDouble() > 0);
    QCOMPARE(details.value("size").toLongLong(), qint64(GetSerializeSize(TX_WITH_WITNESS(*tx.tx))));
    QCOMPARE(details.value("version").toLongLong(), qint64(tx.tx->version));
    QCOMPARE(details.value("lockTime").toLongLong(), qint64(tx.tx->nLockTime));
    QCOMPARE(details.value("weight"), details.value("flow").toMap().value("weight"));
    QVERIFY(details.value("weight").toLongLong() > 0);
    QCOMPARE(details.value("flow").toMap().value("outputs").toList().size(), 2);
    const auto input = details.value("flow").toMap().value("inputs").toList()[0].toMap();
    QCOMPARE(input.value("address").toString(), input_address);
    QCOMPARE(input.value("label").toString(), QString("Savings"));
    QVERIFY(input.value("paymentRequests").toList().isEmpty());
    QCOMPARE(details.value("actions").toList().size(), 1);
    model->refreshStatuses();
    model->transactionDetails(Id(tx), true);
    QCOMPARE(f.state->parent_reads, 1);

    // Address edits refresh the input title without reloading transaction data.
    QSignalSpy input_changed(model, &Model::transactionDetailsChanged);
    f.state->labels[input_address.toStdString()] = "Long-term savings";
    Q_EMIT f.wallet->addressListChanged();
    QVERIFY(!input_changed.isEmpty());
    details = model->transactionDetails(Id(tx), true);
    QCOMPARE(details.value("flow").toMap().value("inputs").toList()[0].toMap().value("label").toString(),
        QString("Long-term savings"));
    QCOMPARE(f.state->parent_reads, 1);
    f.state->labels[input_address.toStdString()].clear();
    Q_EMIT f.wallet->addressListChanged();
    QVERIFY(model->transactionDetails(Id(tx), true).value("flow").toMap().value("inputs").toList()[0].toMap().value("label").toString().isEmpty());

    auto request = Request(91, Address(tx, 1), "Public name");
    request.recipient.noteSelf = "Private note";
    QSignalSpy changed(model, &Model::transactionDetailsChanged);
    f.request(request);
    QVERIFY(!changed.isEmpty());
    details = model->transactionDetails(Id(tx), true);
    auto output = details.value("flow").toMap().value("outputs").toList()[1].toMap();
    QCOMPARE(output.value("label").toString(), QString("Private note"));
    QCOMPARE(output.value("paymentRequests").toList().size(), 1);
    request.recipient.noteSelf.clear();
    f.request(request);
    f.wallet->setDisplayUnit(3);
    output = model->transactionDetails(Id(tx), true).value("flow").toMap().value("outputs").toList()[1].toMap();
    QVERIFY(output.value("label").toString().isEmpty());
    QVERIFY(output.value("amount").toString().endsWith(" sat"));
    QCOMPARE(f.state->parent_reads, 1);
    QCOMPARE(f.state->address_writes, 0);
    f.state->notify(tx, CT_UPDATED);
    QTRY_VERIFY(changed.size() > 2);
    QCoreApplication::processEvents();
    model->transactionDetails(Id(tx), true);
    QCOMPARE(f.state->parent_reads, 2);
    QVERIFY(model->transactionDetails("not a txid", true).isEmpty());
}

void TransactionActivityModelTests::exposesParentsAndActionsWithoutAllocatingFees()
{
    Fixture f;
    const auto batch = MakeTx({{120'000, true}}, {{60'000, false}, {40'000, false, false, 2}, {19'000, true, true, 3}});
    f.state->put(batch);
    f.state->labels[Address(batch).toStdString()] = "Robert";
    f.state->labels[Address(batch, 1).toStdString()] = "Elisabeth";
    auto* source = f.model();
    QAbstractItemModelTester tester(source, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(source->rowCount(), 1);
    QCOMPARE(source->transactionCount(), 1);
    QCOMPARE(source->requestCount(), 0);
    const auto row = source->index(0);
    QCOMPARE(row.data(Model::ActivityTypeRole).toInt(), int(Model::Multiple));
    QCOMPARE(row.data(Model::NetAmountSatRole).toLongLong(), -101'000);
    QCOMPARE(row.data(Model::FeeSatRole).toLongLong(), 1'000);
    QVERIFY(row.data(Model::FeeKnownRole).toBool());
    QCOMPARE(row.data(Model::AmountRole).toString(), QString("0.00101000 BTC"));
    QVERIFY(row.data(Model::AddressRole).toString().isEmpty());
    const auto actions = row.data(Model::ActionsRole).toList();
    QCOMPARE(actions.size(), 2);
    QCOMPARE(actions[0].toMap().value("label").toString(), QString("Robert"));
    QCOMPARE(actions[0].toMap().value("amountSat").toLongLong(), 60'000);
    QCOMPARE(actions[1].toMap().value("amountSat").toLongLong(), 40'000);
    QCOMPARE(actions[1].toMap().value("outputIndex").toInt(), 1);
    QVERIFY(actions[0].toMap().value("actionId") != actions[1].toMap().value("actionId"));

    const int reads = f.state->status_reads + f.state->label_reads + f.state->snapshots;
    for (auto role : source->roleNames().keys()) source->data(row, role);
    QCOMPARE(source->transactionDetails(Id(batch)).value("actions").toList(), actions);
    QCOMPARE(f.state->status_reads + f.state->label_reads + f.state->snapshots, reads);
    f.wallet->setDisplayUnit(3);
    QVERIFY(row.data(Model::AmountRole).toString().endsWith(" sat"));
    QVERIFY(row.data(Model::ActionsRole).toList()[0].toMap().value("amount").toString().endsWith(" sat"));
    QCOMPARE(row.data(Model::NetAmountSatRole).toLongLong(), -101'000);
}

void TransactionActivityModelTests::savesBatchRecipientNotesWhenSending_data()
{
    QTest::addColumn<bool>("externalSigner");
    QTest::newRow("local-signing") << false;
    QTest::newRow("external-signing") << true;
}

void TransactionActivityModelTests::savesBatchRecipientNotesWhenSending()
{
    QFETCH(bool, externalSigner);
    Fixture f;
    auto batch = MakeTx({{130'000, true}},
        {{60'000, false}, {40'000, false, false, 2}, {10'000, false, false, 3}, {19'000, true, true, 4}});
    CMutableTransaction previous;
    previous.vout.emplace_back(130'000, CScript{} << OP_DROP << OP_TRUE);
    f.state->previous_tx = MakeTransactionRef(previous);
    CMutableTransaction spending{*batch.tx};
    spending.vin[0].prevout = COutPoint{f.state->previous_tx->GetHash(), 0};
    batch.tx = MakeTransactionRef(spending);
    f.state->send_draft = batch;
    f.state->external_signer = externalSigner;
    const auto second_address = Address(batch, 1).toStdString();
    f.state->labels[second_address] = "Previous label";
    f.state->purposes[second_address] = wallet::AddressPurpose::RECEIVE;
    auto request = Request(1, Address(batch, 1), "Public request name");
    request.recipient.noteSelf = "Private request note";
    f.request(request);

    auto* source = f.model();
    auto* recipients = f.wallet->sendRecipientList();
    const QStringList notes{QStringLiteral("Robert · September"), QStringLiteral("Elisabeth · September"), QString{}};
    for (int i = 0; i < notes.size(); ++i) {
        if (i > 0) recipients->add();
        auto* recipient = recipients->currentRecipient();
        recipient->setAddress(Address(batch, i));
        recipient->setLabel(notes[i]);
        recipient->amount()->setSatoshi(batch.tx->vout[i].nValue);
    }
    QVERIFY2(f.wallet->prepareTransaction(), qPrintable(f.wallet->transactionError()));
    QCOMPARE(f.state->address_writes, 0); // Reviewing/cancelling must not save notes.
    if (externalSigner) {
        QSignalSpy approved(f.wallet.get(), &WalletQmlModel::externalSignerApprovalSucceeded);
        f.wallet->approveExternalSignerTransaction();
        QCOMPARE(approved.count(), 1);
    }
    // The committed notes belong to the reviewed transaction, not a new draft.
    recipients->clear();
    QVERIFY(f.wallet->sendTransaction());
    QCOMPARE(f.state->commits, 1);
    QCOMPARE(f.state->address_writes, 2);
    QCOMPARE(f.state->labels.at(Address(batch).toStdString()), notes[0].toStdString());
    QCOMPARE(f.state->labels.at(second_address), notes[1].toStdString());
    QCOMPARE(f.state->purposes.at(Address(batch).toStdString()), wallet::AddressPurpose::SEND);
    QCOMPARE(f.state->purposes.at(second_address), wallet::AddressPurpose::RECEIVE);
    QCOMPARE(f.wallet->receiveRequests()->matchingEntriesForAddress(Address(batch, 1)).first().toMap().value("noteSelf").toString(),
             QStringLiteral("Private request note"));

    const QString txid = Id(*f.state->send_draft);
    QTRY_VERIFY(Find(*source, txid).isValid());
    auto actions = Find(*source, txid).data(Model::ActionsRole).toList();
    QCOMPARE(actions.size(), 3);
    for (int i = 0; i < notes.size(); ++i) QCOMPARE(actions[i].toMap().value("label").toString(), notes[i]);
    source->reload();
    QCOMPARE(Find(*source, txid).data(Model::ActionsRole).toList(), actions);
}

void TransactionActivityModelTests::filtersWholeTransactionsAndExportsParentImpact()
{
    Fixture f;
    const auto mixed = MakeTx({{100'000, true}, {50'000, false}}, {{120'000, false}, {29'000, true, true, 2}}, Time(8, 12, 22));
    f.state->put(mixed);
    f.state->labels[Address(mixed, 1).toStdString()] = "Savings";
    Proxy proxy;
    proxy.setSourceModel(f.model());
    QAbstractItemModelTester tester(&proxy, QAbstractItemModelTester::FailureReportingMode::QtTest);
    proxy.setSearchText("savings");
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(Model::ActionCountRole).toInt(), 2);
    QVERIFY(!proxy.index(0, 0).data(Model::FeeKnownRole).toBool());
    QVERIFY(!proxy.index(0, 0).data(Model::FeeSatRole).isValid());
    const auto actions = proxy.index(0, 0).data(Model::ActionsRole).toList();
    QCOMPARE(actions[0].toMap().value("source").toInt(), int(Model::WalletInputs));
    QCOMPARE(actions[0].toMap().value("inputs").toList().size(), 1);
    QCOMPARE(actions[1].toMap().value("amount").toString(), QString("+0.00029000 BTC"));
    for (auto type : {Proxy::Sent, Proxy::Received, Proxy::Multiple}) {
        proxy.setTypeFilter(type);
        QCOMPARE(proxy.rowCount(), 1);
        QCOMPARE(proxy.index(0, 0).data(Model::ActionsRole).toList(), actions);
    }
    proxy.setTypeFilters({Proxy::Sent, Proxy::Received, Proxy::Multiple});
    QCOMPARE(proxy.rowCount(), 1); // Matching several selected types never duplicates a parent.
    QCOMPARE(proxy.index(0, 0).data(Model::ActionsRole).toList(), actions);
    QVERIFY(proxy.setAmountRange(71'000, 71'000));
    QCOMPARE(proxy.rowCount(), 1); // Use the net parent amount, not a child output.
    QCOMPARE(proxy.index(0, 0).data(Model::ActionsRole).toList(), actions);
    proxy.setMaxAmount(-1);
    proxy.setMinAmount(71'001);
    QCOMPARE(proxy.rowCount(), 0);
    proxy.setMinAmount(71'000);
    QCOMPARE(proxy.rowCount(), 1);
    QVERIFY(proxy.applyCustomRange("2026-08-12", "2026-08-12"));
    QCOMPARE(proxy.rowCount(), 1);
    QTemporaryDir dir;
    const auto path = dir.filePath("activity.csv");
    QVERIFY(proxy.exportCsv(path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto csv = file.readAll();
    QCOMPARE(csv.count('\n'), 4); // Header, parent wallet impact, and both actions.
    QVERIFY(csv.contains("Action amount (BTC)"));
    QVERIFY(csv.contains("-0.00100000"));
    QVERIFY(csv.contains("0.00029000"));
    QCOMPARE(csv.count("-0.00071000"), 1);
    QVERIFY(csv.contains(Address(mixed, 1).toUtf8()));
    QVERIFY(csv.contains("Multiple actions"));
    QVERIFY(csv.contains("-0.00071000"));
    proxy.setTypeFilter(Proxy::Mined);
    QCOMPARE(proxy.rowCount(), 0);
}

void TransactionActivityModelTests::exportsBatchActionsWithoutDuplicatingFees()
{
    Fixture f;
    const auto batch = MakeTx({{100'000, true}}, {{40'000, false}, {59'000, false, false, 2}}, Time(8, 12));
    f.state->put(batch);
    f.state->labels[Address(batch, 0).toStdString()] = "=Bob";
    f.state->labels[Address(batch, 1).toStdString()] = "Alice, savings";
    Proxy proxy;
    proxy.setSourceModel(f.model());
    proxy.setDisplayUnit(3);
    proxy.setSearchText("Alice"); // Export every action of the matching transaction.
    QTemporaryDir dir;
    const auto path = dir.filePath("batch.csv");
    QVERIFY(proxy.exportCsv(path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto csv = file.readAll();
    QCOMPARE(csv.count('\n'), 4);
    QVERIFY(csv.contains("Action amount (sat)"));
    QVERIFY(csv.contains("\"'=Bob\""));
    QVERIFY(csv.contains("\"Alice, savings\""));
    QVERIFY(csv.contains(Address(batch, 0).toUtf8()));
    QVERIFY(csv.contains(Address(batch, 1).toUtf8()));
    const auto lines = csv.split('\n');
    // The parent carries the 1,000 sat fee once. Child Amount cells are empty;
    // their separate Action amount cells contain only the recipient amounts.
    QVERIFY(lines[1].contains("\"-100000\""));
    QVERIFY(lines[2].endsWith("\"-40000\""));
    QVERIFY(lines[3].endsWith("\"-59000\""));
    const QByteArray empty_amount_and_id = "\",\"\",\"" + Id(batch).toUtf8();
    QVERIFY(lines[2].contains(empty_amount_and_id));
    QVERIFY(lines[3].contains(empty_amount_and_id));
}

void TransactionActivityModelTests::groupsPendingMonthsAndDaysAndCountsParents()
{
    Fixture f;
    const auto incoming = MakeTx({{50'000, false}}, {{49'000, true}}, Time(8, 12));
    const auto august = MakeTx({{90'000, true}}, {{40'000, false}, {49'000, false, false, 2}}, Time(8, 13));
    const auto september = MakeTx({{30'000, true}}, {{29'000, false}}, Time(9, 14));
    f.state->put(incoming, 0);
    f.state->put(august);
    f.state->put(september, 0);
    f.request(Request(1, "unpaid-address"));
    Proxy proxy;
    proxy.setSourceModel(f.model());
    QCOMPARE(proxy.count(), 4);
    QCOMPARE(proxy.transactionCount(), 3);
    QCOMPARE(proxy.requestCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(Proxy::SectionKeyRole).toString(), QString("pending"));
    QCOMPARE(proxy.index(1, 0).data(Proxy::SectionKeyRole).toString(), QString("pending"));
    QCOMPARE(proxy.index(2, 0).data(Proxy::SectionKeyRole).toString(), QString("pending"));
    QCOMPARE(proxy.index(0, 0).data(Model::TxidRole).toString(), Id(september));
    QCOMPARE(proxy.index(3, 0).data(Proxy::SectionKeyRole).toString(), QString("2026-08"));
    proxy.setGroupBy(Proxy::Day);
    QCOMPARE(Find(proxy, Id(august)).data(Proxy::SectionKeyRole).toString(), QString("2026-08-13"));
    QCOMPARE(Find(proxy, Id(august)).data(Proxy::DateTimeLabelRole).toString(), QLocale().toString(QDateTime::fromSecsSinceEpoch(august.time), "h:mm AP"));
    QCOMPARE(Find(proxy, Id(incoming)).data(Proxy::SectionKeyRole).toString(), QString("pending"));
    QVERIFY(Find(proxy, Id(incoming)).data(Proxy::DateTimeLabelRole).toString().contains("12"));
    f.state->statuses[incoming.tx->GetHash()].depth_in_main_chain = 1;
    f.model()->refreshStatuses();
    QCOMPARE(Find(proxy, Id(incoming)).data(Proxy::SectionKeyRole).toString(), QString("2026-08-12"));
    QCOMPARE(proxy.index(0, 0).data(Model::TxidRole).toString(), Id(september));
    proxy.setTypeFilter(Proxy::Sent);
    QCOMPARE(proxy.transactionCount(), 2);
    QCOMPARE(proxy.requestCount(), 0);
    QCOMPARE(proxy.count(), 2); // The batch's two children count as one.
}

void TransactionActivityModelTests::groupsTransactionsInPendingUntilFirstConfirmation()
{
    Fixture f;
    const std::vector<interfaces::WalletTx> transactions{
        MakeTx({{100'000, false}}, {{99'000, true, false, 5}}, Time(9, 15)), // Receive.
        MakeTx({{100'000, true}}, {{99'000, false}}, Time(9, 15)), // Single send.
        MakeTx({{120'000, true}}, {{60'000, false}, {40'000, false, false, 2}, {19'000, true, true, 3}}, Time(9, 15)), // Batch.
        MakeTx({{50'000, true}, {50'000, true}}, {{99'000, true}}, Time(9, 15)), // Consolidation.
        MakeTx({{100'000, true}}, {{49'000, true}, {50'000, true, false, 2}}, Time(9, 15)), // Split.
        MakeTx({{100'000, true}}, {{99'000, true, false, 4}}, Time(9, 15)), // Internal transfer.
    };
    for (const auto& tx : transactions) f.state->put(tx, 0);
    auto* source = f.model();
    Proxy proxy;
    proxy.setSourceModel(source);
    QCOMPARE(proxy.transactionCount(), int(transactions.size()));

    for (const int depth : {0, 1, 2, 5, 6, 0}) {
        for (const auto& tx : transactions) f.state->statuses[tx.tx->GetHash()].depth_in_main_chain = depth;
        source->refreshStatuses();
        for (const auto& tx : transactions) {
            const auto row = Find(proxy, Id(tx));
            QCOMPARE(row.data(Model::IsPendingRole).toBool(), depth == 0);
            QCOMPARE(row.data(Proxy::SectionKeyRole).toString(), depth == 0 ? QString("pending") : QString("2026-09"));
        }
    }
}

void TransactionActivityModelTests::totalsPendingWalletImpactWithoutRequests()
{
    Fixture f;
    const auto receive = MakeTx({{50'000, false}}, {{49'000, true}}, Time(9, 15));
    const auto batch = MakeTx({{120'000, true}},
        {{60'000, false}, {40'000, false, false, 2}, {19'000, true, true, 3}}, Time(9, 15));
    const auto confirmed = MakeTx({{20'000, false}}, {{19'000, true, false, 4}}, Time(9, 15));
    f.state->put(receive, 0);
    f.state->put(batch, 0);
    f.state->put(confirmed, 1);
    f.request(Request(1, "unpaid-address", "Invoice", 1'000'000));
    auto* source = f.model();
    Proxy proxy;
    proxy.setSourceModel(source);
    QSignalSpy changed(&proxy, &Proxy::pendingBalanceChanged);
    QCOMPARE(proxy.pendingBalanceSat(), -52'000); // Includes the batch fee, once.
    proxy.setDisplayUnit(3);
    QCOMPARE(proxy.pendingBalance(), QLocale().toString(qint64{-52'000}) + " sats");
    QVERIFY(!changed.empty());

    proxy.setTypeFilter(Proxy::Received);
    QCOMPARE(proxy.pendingBalanceSat(), 49'000);
    QCOMPARE(proxy.pendingBalance(), QLocale().positiveSign() + QLocale().toString(qint64{49'000}) + " sats");
    proxy.setTypeFilter(Proxy::PaymentRequest);
    QCOMPARE(proxy.pendingBalanceSat(), 0);
    proxy.setTypeFilter(Proxy::TypeAll);
    QCOMPARE(proxy.pendingBalanceSat(), -52'000);

    changed.clear();
    f.state->statuses[batch.tx->GetHash()].depth_in_main_chain = 1;
    source->refreshStatuses();
    QCOMPARE(proxy.pendingBalanceSat(), 49'000);
    QVERIFY(!changed.empty());
    f.state->statuses[receive.tx->GetHash()].depth_in_main_chain = 1;
    source->refreshStatuses();
    QCOMPARE(proxy.pendingBalanceSat(), 0);

    // A same-height reorg returns the outgoing impact to pending.
    f.state->statuses[batch.tx->GetHash()].depth_in_main_chain = 0;
    source->refreshStatuses();
    QCOMPARE(proxy.pendingBalanceSat(), -101'000);
    proxy.setSourceModel(nullptr);
    QCOMPARE(proxy.pendingBalanceSat(), 0);
}

void TransactionActivityModelTests::movesReplacedBatchToHistoryAndKeepsReplacementPending()
{
    Fixture f;
    auto original = MakeTx({{120'000, true}},
        {{60'000, false}, {40'000, false, false, 2}, {19'000, true, true, 3}}, Time(9, 15));
    auto replacement = MakeTx({{120'000, true}},
        {{60'000, false}, {40'000, false, false, 2}, {18'000, true, true, 3}}, Time(9, 15, 13));
    f.state->put(original, 0);
    auto* source = f.model();
    Proxy proxy;
    proxy.setSourceModel(source);
    QCOMPARE(Find(proxy, Id(original)).data(Proxy::SectionKeyRole).toString(), QString("pending"));

    original.value_map["replaced_by_txid"] = Id(replacement).toStdString();
    replacement.value_map["replaces_txid"] = Id(original).toStdString();
    f.state->put(original, 0);
    f.state->notify(original, CT_UPDATED);
    f.state->put(replacement, 0);
    f.state->notify(replacement, CT_NEW);
    QTRY_COMPARE(proxy.transactionCount(), 2);
    QVERIFY(Find(proxy, Id(original)).data(Model::IsInactiveRole).toBool());
    QVERIFY(!Find(proxy, Id(original)).data(Model::IsPendingRole).toBool());
    QCOMPARE(Find(proxy, Id(original)).data(Proxy::SectionKeyRole).toString(), QString("2026-09"));
    QCOMPARE(Find(proxy, Id(replacement)).data(Proxy::SectionKeyRole).toString(), QString("pending"));
    QCOMPARE(proxy.index(0, 0).data(Model::TxidRole).toString(), Id(replacement));
    QCOMPARE(Find(proxy, Id(replacement)).data(Model::ActionCountRole).toInt(), 2);
    proxy.setGroupBy(Proxy::Day);
    QCOMPARE(Find(proxy, Id(original)).data(Proxy::SectionKeyRole).toString(), QString("2026-09-15"));
    QCOMPARE(Find(proxy, Id(replacement)).data(Proxy::SectionKeyRole).toString(), QString("pending"));

    QTemporaryDir dir;
    const auto path = dir.filePath("replaced.csv");
    proxy.setDisplayUnit(3);
    QVERIFY(proxy.exportCsv(path));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto csv = file.readAll();
    QVERIFY(csv.contains("\"Status\""));
    const QByteArray original_parent = "\"0\",\"" + Id(original).toUtf8() + "\",\"Transaction\",\"\",\"Replaced\"";
    const QByteArray replacement_parent = "\"-102000\",\"" + Id(replacement).toUtf8() + "\",\"Transaction\",\"\",\"Unconfirmed\"";
    QVERIFY(csv.contains(original_parent));
    QVERIFY(csv.contains(replacement_parent));
    QCOMPARE(csv.count("\"Replaced\""), 3); // Parent and both historical actions.
    QVERIFY(csv.contains("\"Replaced\",\"-60000\""));
    QVERIFY(!csv.contains("\"-101000\""));
    file.close();

    f.state->statuses[replacement.tx->GetHash()].depth_in_main_chain = 1;
    source->refreshStatuses();
    QCOMPARE(Find(proxy, Id(replacement)).data(Proxy::SectionKeyRole).toString(), QString("2026-09-15"));

    // An original that confirms must not be zeroed by stale replacement metadata.
    f.state->statuses[original.tx->GetHash()].depth_in_main_chain = 6;
    source->refreshStatuses();
    QVERIFY(proxy.exportCsv(path));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto confirmed_csv = file.readAll();
    const QByteArray confirmed_parent = "\"-101000\",\"" + Id(original).toUtf8() + "\",\"Transaction\",\"\",\"Confirmed\"";
    QVERIFY(confirmed_csv.contains(confirmed_parent));
    QVERIFY(!confirmed_csv.contains("\"Replaced\""));
}

void TransactionActivityModelTests::associatesRequestsPerOutputAndUpdatesLive()
{
    Fixture f;
    const auto tx = MakeTx({{120'000, false}}, {{50'000, true}, {69'000, true, false, 2}});
    auto request = Request(1, Address(tx), "Public name");
    request.recipient.noteSelf = "Rent";
    f.request(request);
    auto* source = f.model();
    QAbstractItemModelTester tester(source, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(source->requestCount(), 1);
    f.state->put(tx, 0);
    f.state->notify(tx, CT_NEW);
    QTRY_COMPARE(source->transactionCount(), 1);
    QCOMPARE(source->requestCount(), 0);
    auto actions = Find(*source, Id(tx)).data(Model::ActionsRole).toList();
    QCOMPARE(actions[0].toMap().value("paymentRequests").toList().size(), 1);
    QVERIFY(actions[1].toMap().value("paymentRequests").toList().isEmpty());
    QCOMPARE(actions[0].toMap().value("label").toString(), QString("Rent"));
    QCOMPARE(source->transactionDetails(Id(tx)).value("paymentRequests").toList().size(), 1);
    Proxy proxy;
    proxy.setSourceModel(source);
    proxy.setTypeFilter(Proxy::PaymentRequest);
    QCOMPARE(proxy.rowCount(), 1); // Paid request is represented by its transaction.
    request.recipient.noteSelf = "September rent";
    f.request(request);
    proxy.setSearchText("September rent");
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(Find(*source, Id(tx)).data(Model::ActionsRole).toList()[0].toMap().value("label").toString(), QString("September rent"));
    QVERIFY(f.wallet->receiveRequests()->removeByRequestId("1"));
    QCOMPARE(proxy.rowCount(), 0);
    QVERIFY(!Find(*source, Id(tx)).data(Model::HasPaymentRequestRole).toBool());
    QCOMPARE(source->rowCount(), 1);
}

void TransactionActivityModelTests::usesPrivateRequestNotesWithoutChangingStoredData()
{
    Fixture f;
    const auto tx = MakeTx({{50'000, false}}, {{49'000, true}});
    f.state->labels[Address(tx).toStdString()] = "Address book name";
    auto request = Request(1, Address(tx), "Public request name");
    f.request(request);
    auto* source = f.model();
    QVERIFY(source->index(0).data(Model::LabelRole).toString().isEmpty());

    request.recipient.noteSelf = "Private September rent";
    f.request(request);
    QCOMPARE(source->index(0).data(Model::LabelRole).toString(), QString("Private September rent"));
    Proxy proxy;
    proxy.setSourceModel(source);
    proxy.setSearchText("Private September");
    QCOMPARE(proxy.rowCount(), 1);

    f.state->put(tx);
    source->reload();
    QCOMPARE(source->requestCount(), 0);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(source->transactionDetails(Id(tx)).value("label").toString(), QString("Private September rent"));

    // Clearing a note must not revive the public name or address-book label.
    request.recipient.noteSelf.clear();
    f.request(request);
    QCOMPARE(proxy.rowCount(), 0);
    const auto details = source->transactionDetails(Id(tx));
    QVERIFY(details.value("label").toString().isEmpty());
    QVERIFY(details.value("actions").toList()[0].toMap().value("label").toString().isEmpty());
    source->refreshStatuses();
    source->reload();
    const auto stored = f.wallet->receiveRequests()->entryById("1");
    QVERIFY(stored.has_value());
    QVERIFY(stored->recipient.noteSelf.empty());
    QCOMPARE(stored->recipient.label, std::string("Public request name"));
    QCOMPARE(f.state->labels.at(Address(tx).toStdString()), std::string("Address book name"));
    proxy.setSearchText("Public request name");
    QCOMPARE(proxy.rowCount(), 1); // Public metadata remains searchable.

    Fixture unnamed;
    unnamed.state->put(tx);
    unnamed.state->labels[Address(tx).toStdString()] = "Public address name";
    QVERIFY(unnamed.model()->transactionDetails(Id(tx)).value("label").toString().isEmpty());
}

void TransactionActivityModelTests::restoresRequestsOnConflictAbandonmentReplacementAndDeletion()
{
    Fixture f;
    auto tx = MakeTx({{50'000, false}}, {{49'000, true}});
    f.state->put(tx, 0);
    f.request(Request(1, Address(tx)));
    auto* source = f.model();
    QCOMPARE(source->requestCount(), 0);
    auto& status = f.state->statuses[tx.tx->GetHash()];
    status.depth_in_main_chain = -1;
    source->refreshStatuses();
    QCOMPARE(source->requestCount(), 1);
    QVERIFY(Find(*source, Id(tx)).data(Model::IsInactiveRole).toBool());
    QVERIFY(!Find(*source, Id(tx)).data(Model::IsPendingRole).toBool());
    status.depth_in_main_chain = 0;
    status.is_abandoned = true;
    source->refreshStatuses();
    QCOMPARE(source->requestCount(), 1);
    QCOMPARE(Find(*source, Id(tx)).data(Model::StatusRole).toInt(), int(Transaction::Abandoned));
    status.is_abandoned = false;
    source->refreshStatuses();
    QCOMPARE(source->requestCount(), 0);

    auto replacement = MakeTx({{50'000, false}}, {{48'000, true}});
    tx.value_map["replaced_by_txid"] = Id(replacement).toStdString();
    f.state->put(tx, 0);
    f.state->notify(tx, CT_UPDATED);
    QTRY_COMPARE(source->requestCount(), 1);
    QCOMPARE(Find(*source, Id(tx)).data(Model::ReplacedByTxidRole).toString(), Id(replacement));
    replacement.value_map["replaces_txid"] = Id(tx).toStdString();
    f.state->put(replacement, 0);
    f.state->notify(replacement, CT_NEW);
    QTRY_COMPARE(source->transactionCount(), 2);
    QCOMPARE(source->requestCount(), 0);
    QCOMPARE(Find(*source, Id(replacement)).data(Model::ReplacesTxidRole).toString(), Id(tx));
    f.state->transactions.erase(replacement.tx->GetHash());
    f.state->statuses.erase(replacement.tx->GetHash());
    f.state->notify(replacement, CT_DELETED);
    QTRY_COMPARE(source->transactionCount(), 1);
    QCOMPARE(source->requestCount(), 1);
    source->reload();
    QCOMPARE(source->requestCount(), 1);
    // The original can win a race (or confirm after a reorg). Historical RBF
    // metadata must not override its current confirmed state.
    f.state->statuses[tx.tx->GetHash()].depth_in_main_chain = 1;
    source->refreshStatuses();
    QCOMPARE(source->requestCount(), 0);
    QVERIFY(!Find(*source, Id(tx)).data(Model::IsInactiveRole).toBool());
}

void TransactionActivityModelTests::sharesAddressAssociationsWithoutDuplicatingParents()
{
    Fixture f;
    const auto tx = MakeTx({{100'000, false}}, {{50'000, true}, {49'000, true}});
    f.state->put(tx);
    f.request(Request(1, Address(tx), "First"));
    f.request(Request(2, Address(tx), "Second", 0));
    auto* source = f.model();
    QCOMPARE(source->rowCount(), 1);
    QCOMPARE(source->transactionDetails(Id(tx)).value("paymentRequests").toList().size(), 2);
    for (const auto& action : source->index(0).data(Model::ActionsRole).toList()) {
        QCOMPARE(action.toMap().value("paymentRequests").toList().size(), 2);
    }
    // Match policy is address-based, including partial/unspecified amounts and
    // several requests for a reused address; it makes no one-to-one claim.
    f.wallet->receiveRequests()->setEntries({Request(3, "another-address", "New request", 100)});
    QCOMPARE(source->requestCount(), 1);
    QVERIFY(!Find(*source, Id(tx)).data(Model::HasPaymentRequestRole).toBool());
    Proxy proxy;
    proxy.setSourceModel(source);
    proxy.setTypeFilter(Proxy::PaymentRequest);
    proxy.setMinAmount(101);
    QCOMPARE(proxy.rowCount(), 0);
    proxy.setMinAmount(100);
    QCOMPARE(proxy.rowCount(), 1);
}

void TransactionActivityModelTests::preservesStatusWhenBusyAndRetries()
{
    Fixture f;
    const auto tx = MakeTx({{50'000, false}}, {{49'000, true}});
    f.state->put(tx, 2);
    f.request(Request(1, Address(tx)));
    auto* source = f.model();
    const QPersistentModelIndex row{Find(*source, Id(tx))};
    f.state->busy = true;
    source->refreshStatuses();
    source->reload();
    QCOMPARE(row.data(Model::DepthRole).toInt(), 2);
    QCOMPARE(row.data(Model::StatusRole).toInt(), int(Transaction::Confirming));
    QCOMPARE(source->requestCount(), 0);
    f.state->statuses[tx.tx->GetHash()].depth_in_main_chain = 6;
    f.state->busy = false;
    QTRY_COMPARE(row.data(Model::DepthRole).toInt(), 6);
    QVERIFY(!row.data(Model::IsPendingRole).toBool());

    Fixture unknown;
    unknown.state->put(tx);
    unknown.state->busy = true;
    auto* uninitialized = unknown.model();
    QVERIFY(!uninitialized->index(0).data(Model::StatusKnownRole).toBool());
    QVERIFY(!uninitialized->index(0).data(Model::IsInactiveRole).toBool());
    unknown.state->busy = false;
    QTRY_VERIFY(uninitialized->index(0).data(Model::StatusKnownRole).toBool());
}

void TransactionActivityModelTests::refreshesOnWalletTipAndSameHeightReorg()
{
    Fixture f;
    const auto tx = MakeTx({{50'000, false}}, {{49'000, true}});
    f.state->put(tx, 5);
    auto* source = f.model();
    f.state->statuses[tx.tx->GetHash()].depth_in_main_chain = 6;
    QTRY_COMPARE(source->index(0).data(Model::DepthRole).toInt(), 6);
    f.state->statuses[tx.tx->GetHash()].depth_in_main_chain = 0;
    f.state->tip = uint256{2}; // Same height, different wallet-processed block.
    QTRY_COMPARE(source->index(0).data(Model::DepthRole).toInt(), 0);
    QVERIFY(source->index(0).data(Model::IsPendingRole).toBool());
}

void TransactionActivityModelTests::queuesWalletNotificationsAndRetainsStableIndexes()
{
    Fixture f;
    auto* source = f.model();
    QAbstractItemModelTester tester(source, QAbstractItemModelTester::FailureReportingMode::QtTest);
    const auto tx = MakeTx({{50'000, false}}, {{49'000, true}});
    f.state->put(tx, 2);
    std::thread notify([&] { f.state->notify(tx, CT_NEW); });
    notify.join();
    QCOMPARE(source->rowCount(), 0);
    QTRY_COMPARE(source->rowCount(), 1);
    QVERIFY(!f.state->snapshot_on_worker);
    const QPersistentModelIndex row{source->index(0)};
    const auto id = row.data(Model::IdRole);
    QSignalSpy resets(source, &QAbstractItemModel::modelReset);
    QSignalSpy changes(source, &QAbstractItemModel::dataChanged);
    source->reload();
    QCOMPARE(resets.count(), 0);
    QCOMPARE(changes.count(), 0);
    f.state->labels[Address(tx).toStdString()] = "Updated label";
    Q_EMIT f.wallet->addressListChanged();
    QVERIFY(row.data(Model::LabelRole).toString().isEmpty());
    QVERIFY(row.data(Model::SearchTextRole).toString().contains("Updated label"));
    QVERIFY(changes.count() > 0);
    QCOMPARE(row.data(Model::IdRole), id);
    f.request(Request(1, "unpaid-address"));
    QVERIFY(row.isValid());
    QCOMPARE(row.data(Model::IdRole), id);
    source->reload();
    QCOMPARE(source->rowCount(), 2);
    f.state->transactions.clear();
    f.state->statuses.clear();
    source->reload();
    QVERIFY(!row.isValid());
    QCOMPARE(source->requestCount(), 1);
}

void TransactionActivityModelTests::recognizesSelfSendToPaymentRequest_data()
{
    QTest::addColumn<bool>("multiple_inputs");
    QTest::newRow("single-input") << false;
    QTest::newRow("multiple-inputs") << true;
}

void TransactionActivityModelTests::recognizesSelfSendToPaymentRequest()
{
    QFETCH(bool, multiple_inputs);
    Fixture f;
    const std::vector<std::pair<CAmount, bool>> inputs = multiple_inputs
        ? std::vector<std::pair<CAmount, bool>>{{60'000, true}, {40'000, true}}
        : std::vector<std::pair<CAmount, bool>>{{100'000, true}};
    const auto tx = MakeTx(inputs, {{39'000, true, true}, {60'000, true, false, 2}});
    auto request = Request(1, Address(tx, 1), "Public label", 60'000);
    request.recipient.noteSelf = "Personal savings";
    f.request(request);
    f.state->put(tx);
    auto* model = f.model();
    const auto row = Find(*model, Id(tx));
    QVERIFY(row.isValid());
    QCOMPARE(row.data(Model::ActivityTypeRole).toInt(), int(Model::InternalTransfer));
    QCOMPARE(row.data(Model::TypeRole).toInt(), int(Transaction::SendToSelf));
    QCOMPARE(row.data(Model::NetAmountSatRole).toLongLong(), -1'000);
    QCOMPARE(row.data(Model::LabelRole).toString(), QString("Personal savings"));
    QCOMPARE(row.data(Model::AddressRole).toString(), Address(tx, 1));
    QCOMPARE(model->requestCount(), 0);
    QVERIFY(row.data(Model::HasPaymentRequestRole).toBool());
    const auto actions = row.data(Model::ActionsRole).toList();
    QCOMPARE(actions.size(), 1);
    QCOMPARE(actions[0].toMap().value("outputIndex").toInt(), 1);
    QCOMPARE(actions[0].toMap().value("amountSat").toLongLong(), 60'000);
    QCOMPARE(actions[0].toMap().value("direction").toInt(), int(Model::InternalAction));
    QVERIFY(actions[0].toMap().value("hasPaymentRequest").toBool());
    Proxy proxy;
    proxy.setSourceModel(model);
    proxy.setTypeFilter(Proxy::Split);
    QCOMPARE(proxy.rowCount(), 0);
    proxy.setTypeFilter(Proxy::Consolidation);
    QCOMPARE(proxy.rowCount(), 0);
    proxy.setTypeFilter(Proxy::SentToSelf);
    QCOMPARE(proxy.rowCount(), 1);
}

void TransactionActivityModelTests::handlesInternalAndMinedTypes()
{
    Fixture f;
    const auto consolidation = MakeTx({{50'000, true}, {50'000, true}}, {{99'000, true}});
    const auto split = MakeTx({{100'000, true}}, {{50'000, true}, {49'000, true, false, 2}});
    const auto internal = MakeTx({{60'000, true}}, {{59'000, true}});
    auto mined = MakeTx({{0, false}}, {{312'500'000, true}});
    mined.is_coinbase = true;
    for (const auto& tx : {consolidation, split, internal, mined}) f.state->put(tx);
    f.state->statuses[mined.tx->GetHash()].blocks_to_maturity = 94;
    Proxy proxy;
    proxy.setSourceModel(f.model());
    proxy.setTypeFilter(Proxy::SentToSelf);
    QCOMPARE(proxy.rowCount(), 3);
    QVERIFY(Find(proxy, Id(consolidation)).isValid());
    QVERIFY(Find(proxy, Id(split)).isValid());
    QVERIFY(Find(proxy, Id(internal)).isValid());
    QVERIFY(!Find(proxy, Id(mined)).isValid());
    proxy.setTypeFilter(Proxy::Consolidation);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(Model::NetAmountSatRole).toLongLong(), -1'000);
    QCOMPARE(proxy.index(0, 0).data(Model::ActionsRole).toList()[0].toMap().value("amountSat").toLongLong(), 99'000);
    QVERIFY(proxy.index(0, 0).data(Model::LabelRole).toString().isEmpty());
    proxy.setTypeFilter(Proxy::Split);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(Model::ActionCountRole).toInt(), 2);
    QVERIFY(proxy.index(0, 0).data(Model::LabelRole).toString().isEmpty());
    proxy.setTypeFilter(Proxy::Mined);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(Model::StatusRole).toInt(), int(Transaction::Immature));
    QCOMPARE(proxy.index(0, 0).data(Model::BlocksToMaturityRole).toInt(), 94);
    f.state->statuses[mined.tx->GetHash()].is_in_main_chain = false;
    f.model()->refreshStatuses();
    QCOMPARE(proxy.index(0, 0).data(Model::StatusRole).toInt(), int(Transaction::NotAccepted));
    QVERIFY(proxy.index(0, 0).data(Model::IsInactiveRole).toBool());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(TransactionActivityModelTests)
#else
QTEST_MAIN(TransactionActivityModelTests)
#endif
#include "test_transactionactivitymodel.moc"
