// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/mocks/psbtfile.h>
#include <test/mocks/preparationtest.h>
#include <test/mocks/sendtest.h>
#include <test/mocks/psbtactiontest.h>
#include <test/mocks/messagebumptest.h>
#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <qml/models/psbtqmlmodel.h>
#include <qml/psbtwork.h>
#include <core_io.h>
#include <key.h>
#include <key_io.h>
#include <util/moneystr.h>
#include <util/string.h>
#include <util/strencodings.h>

#include <interfaces/wallet.h>
#include <sync.h>
#include <wallet/wallet.h>

#include <QFile>
#include <QPointer>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <chrono>
#include <future>
#include <thread>

#ifndef WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
// Holds a wallet's cs_wallet on another thread, as a long Core operation
// would, until released or until a hang-breaker deadline expires.
class HeldWalletLock
{
public:
    HeldWalletLock(BitcoinQmlApplication& app, const QString& name, std::chrono::milliseconds timeout = std::chrono::seconds{60})
    {
        const auto wallet = qmltest::Call(app, [name](interfaces::Node* node) {
            return wallet::GetWallet(*node->walletLoader().context(), name.toStdString());
        });
        if (!wallet) throw std::runtime_error("held wallet");
        std::promise<void> held; auto acquired = held.get_future();
        m_thread = std::thread([wallet, held = std::move(held), released = m_release.get_future(), timeout, this]() mutable {
            LOCK(wallet->cs_wallet); held.set_value();
            if (released.wait_for(timeout) == std::future_status::timeout) m_expired.store(true);
        });
        acquired.wait();
    }
    ~HeldWalletLock() { release(); }
    void release() { if (m_thread.joinable()) { m_release.set_value(); m_thread.join(); } }
    bool expired() const { return m_expired.load(); }
private:
    std::promise<void> m_release;
    std::atomic_bool m_expired{false};
    std::thread m_thread;
};
#ifndef WIN32
// Writes to a FIFO only if a reader is already waiting, so a test never blocks.
bool FeedWaitingFifo(const QString& path, const QByteArray& bytes)
{
    const int fd = ::open(QFile::encodeName(path).constData(), O_WRONLY | O_NONBLOCK);
    if (fd < 0) return false;
    const bool written = ::write(fd, bytes.constData(), size_t(bytes.size())) == bytes.size();
    ::close(fd);
    return written;
}
#endif
} // namespace

class PsbtIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

public:
    explicit PsbtIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void externalUnloadCannotTransferAnImportedReviewToAReopenedWallet()
    {
        QPointer<WalletQmlModel> original{qmltest::CreateWallet(m_app, "original-review")}; QVERIFY(original);
        QVERIFY(qmltest::FundWallet(m_app, *original));
        const auto address = qmltest::NewAddress(m_app, original->name());
        const auto funded = qmltest::Rpc(m_app, "walletcreatefundedpsbt",
            qmltest::Json("[[],[{\"" + address.toStdString() + "\":0.005}]]"), original->name());
        const auto signed_psbt = qmltest::Rpc(m_app, "walletprocesspsbt",
            qmltest::Json("[\"" + funded.find_value("psbt").get_str() + "\"]"), original->name());
        QVERIFY(signed_psbt.find_value("complete").get_bool());
        const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(signed_psbt.find_value("psbt").get_str()));
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("original.psbt");
        QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size()); file.close();
        QCOMPARE(AwaitPsbtImport(*original, path), WalletQmlModel::PsbtImportResult::WalletCanSign);
        QVERIFY(original->currentTransactionCanBroadcast());
        const auto original_opening = original->opening()->identity();
        auto* standby = qmltest::CreateWallet(m_app, "standby-review"); QVERIFY(standby);
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller);
        QCOMPARE(controller->selectedWallet(), standby);
        QVERIFY(!standby->currentTransaction());
        UniValue request{UniValue::VARR}; request.push_back("original-review");
        qmltest::Rpc(m_app, "unloadwallet", request);
        QTRY_VERIFY_WITH_TIMEOUT(original.isNull(), 30'000);
        QCOMPARE(controller->selectedWallet(), standby);
        QVERIFY(!standby->currentTransactionCanSend()); QVERIFY(!standby->currentTransactionCanBroadcast());
        QVERIFY(!standby->currentTransaction());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
        qmltest::Rpc(m_app, "loadwallet", request);
        auto* reopened = qmltest::SelectOpenWalletForSetup(m_app, "original-review"); QVERIFY(reopened);
        QVERIFY(reopened->opening()->identity() != original_opening);
        QVERIFY(!reopened->currentTransaction());
        QVERIFY(!reopened->currentTransactionCanSend()); QVERIFY(!reopened->currentTransactionCanBroadcast());
        // Only a fresh explicit import arms the new opening. No old callback,
        // wallet name lookup, or review result can supply this authorization.
        QCOMPARE(AwaitPsbtImport(*reopened, path), WalletQmlModel::PsbtImportResult::WalletCanSign);
        QVERIFY(reopened->currentTransactionCanBroadcast());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    // draft-109-RC29: wallet shutdown clears the imported PSBT model's
    // document and its sign and broadcast capabilities before it goes away.
    void importedPsbtCapabilitiesClearOnWalletShutdown()
    {
        QPointer<WalletQmlModel> wallet{qmltest::CreateWallet(m_app, "psbt-shutdown")}; QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto address = qmltest::NewAddress(m_app, wallet->name());
        const auto encoded = qmltest::Rpc(m_app, "walletcreatefundedpsbt",
            qmltest::Json("[[],[{\"" + address.toStdString() + "\":0.005}]]"), wallet->name()).find_value("psbt").get_str();
        const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(encoded));
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("shutdown.psbt");
        QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); file.close();
        QPointer<PsbtQmlModel> model{wallet->importedPsbt()}; QVERIFY(model);
        QVERIFY(model->requestLoadFromFile(path));
        QVERIFY(QTest::qWaitFor([&] { return !model->busy(); }, 5'000));
        QVERIFY(model->loaded()); QVERIFY(model->canSign());
        QVERIFY(model->requestSign());
        QVERIFY(QTest::qWaitFor([&] { return !model->busy(); }, 5'000));
        QVERIFY(model->complete()); QVERIFY(model->canBroadcast());
        QVERIFY(qmltest::CreateWallet(m_app, "psbt-standby"));
        struct { bool seen{false}, loaded{true}, can_sign{true}, can_broadcast{true}; } at_shutdown;
        // Shutdown detaches the model and publishes it while it still exists.
        QObject::connect(model, &PsbtQmlModel::changed, wallet, [&, wallet, model] {
            if (!at_shutdown.seen && wallet && !wallet->available() && model) at_shutdown = {true, model->loaded(), model->canSign(), model->canBroadcast()};
        });
        UniValue request{UniValue::VARR}; request.push_back("psbt-shutdown");
        qmltest::Rpc(m_app, "unloadwallet", request);
        QTRY_VERIFY_WITH_TIMEOUT(at_shutdown.seen, 30'000);
        QVERIFY(!at_shutdown.loaded); QVERIFY(!at_shutdown.can_sign); QVERIFY(!at_shutdown.can_broadcast);
        QTRY_VERIFY_WITH_TIMEOUT(wallet.isNull(), 30'000);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void decodedReviewFailurePreservesTheCurrentOperation_data()
    {
        QTest::addColumn<int>("failure");
        QTest::addColumn<QString>("reason");
        QTest::newRow("nonstandard-output") << 0 << QStringLiteral("standard address outputs");
        QTest::newRow("invalid-finalized-signature") << 1 << QStringLiteral("invalid signatures");
        QTest::newRow("recipient-cardinality") << 2 << QStringLiteral("more recipients");
    }
    void decodedReviewFailurePreservesTheCurrentOperation()
    {
        QFETCH(int, failure); QFETCH(QString, reason);
        auto* wallet = qmltest::CreateWallet(m_app, "review-tuple"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto address = qmltest::NewAddress(m_app, wallet->name());
        const auto encoded = qmltest::Rpc(m_app, "walletcreatefundedpsbt",
            qmltest::Json("[[],[{\"" + address.toStdString() + "\":0.005}]]"), wallet->name()).find_value("psbt").get_str();
        const auto documents = qmltest::Call(m_app, [encoded, failure](interfaces::Node*) {
            const auto parsed = DecodeBase64PSBT(encoded);
            if (!parsed || parsed->inputs.empty() || parsed->outputs.empty()) throw std::runtime_error("funded fixture");
            auto bad = *parsed;
            if (failure == 0) bad.outputs[0].script = CScript{} << OP_TRUE;
            else if (failure == 1) bad.inputs[0].final_script_sig = CScript{} << OP_TRUE;
            else {
                const auto output = bad.outputs.front();
                bad.outputs.assign(26, output);
                for (auto& output : bad.outputs) output.amount = 10'000;
            }
            auto retry = *parsed;
            if (retry.outputs[0].amount <= 1'000) throw std::runtime_error("retry output");
            retry.outputs[0].amount -= 1'000;
            std::array<QByteArray, 3> bytes{PsbtQmlModel::SerializePsbtRaw(*parsed),
                PsbtQmlModel::SerializePsbtRaw(bad), PsbtQmlModel::SerializePsbtRaw(retry)};
            // Each input is a valid decoded PSBT. The intended failure occurs
            // later in analysis/review, not in file I/O or the decoder.
            for (const auto& item : bytes) if (!DecodeBase64PSBT(item.toBase64().toStdString())) throw std::runtime_error("post-decode fixture");
            return bytes;
        });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const std::array paths{directory.filePath("original.psbt"), directory.filePath("unsupported.psbt"), directory.filePath("retry.psbt")};
        for (size_t i = 0; i < paths.size(); ++i) {
            QFile file{paths[i]}; QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(documents[i]), documents[i].size());
        }
        auto* draft = wallet->sendRecipientList()->currentRecipient(); QVERIFY(draft);
        draft->setAddress(address); draft->amount()->setSatoshi(COIN / 4);
        QTRY_VERIFY(!wallet->sendRecipientList()->validationPending());
        QCOMPARE(AwaitPsbtImport(*wallet, paths[0]), WalletQmlModel::PsbtImportResult::WalletCanSign);
        const QPointer<WalletQmlModelTransaction> original{wallet->currentTransaction()}; QVERIFY(original);
        const auto original_id = original->txid(), original_fee = original->fee();
        const auto original_flow = wallet->currentTransactionFlow();
        const bool can_send = wallet->currentTransactionCanSend(), can_broadcast = wallet->currentTransactionCanBroadcast();
        QVERIFY(can_send); QVERIFY(!can_broadcast); QVERIFY(original->feeKnown());
        QCOMPARE(AwaitPsbtImport(*wallet, paths[1]), WalletQmlModel::PsbtImportResult::PsbtUnsupported);
        QCOMPARE(wallet->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtFailed);
        QVERIFY2(wallet->psbtActionError().contains(reason), qPrintable(wallet->psbtActionError()));
        QCOMPARE(wallet->currentTransaction(), original.data());
        QVERIFY(wallet->currentTransactionIsImportedPsbt());
        QCOMPARE(original->txid(), original_id); QCOMPARE(original->fee(), original_fee);
        QCOMPARE(wallet->currentTransactionFlow(), original_flow);
        QCOMPARE(wallet->currentTransactionCanSend(), can_send); QCOMPARE(wallet->currentTransactionCanBroadcast(), can_broadcast);
        QCOMPARE(draft->address()->address(), address); QCOMPARE(draft->amount()->satoshi(), qint64{COIN / 4});
        const auto preserved_path = directory.filePath("preserved.psbt");
        QVERIFY(AwaitPsbtSave(*wallet, preserved_path));
        QFile preserved{preserved_path}; QVERIFY(preserved.open(QIODevice::ReadOnly)); QCOMPARE(preserved.readAll(), documents[0]);
        QCOMPARE(AwaitPsbtImport(*wallet, paths[2]), WalletQmlModel::PsbtImportResult::WalletCanSign);
        QVERIFY(wallet->currentTransaction() && wallet->currentTransaction() != original.data());
        QVERIFY(wallet->currentTransaction()->txid() != original_id);
        QVERIFY(wallet->currentTransactionIsImportedPsbt()); QVERIFY(wallet->currentTransactionCanSend()); QVERIFY(!wallet->currentTransactionCanBroadcast());
        const auto retry_path = directory.filePath("retry-export.psbt");
        QVERIFY(AwaitPsbtSave(*wallet, retry_path));
        QFile retry{retry_path}; QVERIFY(retry.open(QIODevice::ReadOnly)); QCOMPARE(retry.readAll(), documents[2]);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    // draft-254-RC29: once shutdown releases the wallet, the Send facade
    // reports no transaction it can send, sign or broadcast.
    void sendCapabilitiesRequireAWallet()
    {
        QPointer<WalletQmlModel> wallet{qmltest::CreateWallet(m_app, "send-without-wallet")}; QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto address = qmltest::NewAddress(m_app, wallet->name());
        const auto encoded = qmltest::Rpc(m_app, "walletcreatefundedpsbt",
            qmltest::Json("[[],[{\"" + address.toStdString() + "\":0.005}]]"), wallet->name()).find_value("psbt").get_str();
        const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(encoded));
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("walletless.psbt");
        QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); file.close();
        QCOMPARE(AwaitPsbtImport(*wallet, path), WalletQmlModel::PsbtImportResult::WalletCanSign);
        QVERIFY(wallet->currentTransactionCanSend()); QVERIFY(wallet->currentTransactionCanSignOnly());
        QVERIFY(qmltest::CreateWallet(m_app, "send-standby"));
        struct { bool seen{false}, released{false}, can_send{true}, can_sign{true}, can_broadcast{true}; } at_shutdown;
        QObject::connect(wallet, &WalletQmlModel::shutdownFinished, wallet, [&, wallet] {
            if (!at_shutdown.seen && wallet) at_shutdown = {true, !wallet->wallet(), wallet->currentTransactionCanSend(),
                wallet->currentTransactionCanSignOnly(), wallet->currentTransactionCanBroadcast()};
        });
        UniValue request{UniValue::VARR}; request.push_back("send-without-wallet");
        qmltest::Rpc(m_app, "unloadwallet", request);
        QTRY_VERIFY_WITH_TIMEOUT(at_shutdown.seen, 30'000);
        QVERIFY(at_shutdown.released);
        QVERIFY(!at_shutdown.can_send); QVERIFY(!at_shutdown.can_sign); QVERIFY(!at_shutdown.can_broadcast);
        QTRY_VERIFY_WITH_TIMEOUT(wallet.isNull(), 30'000);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    // #1070: Send -> Import PSBT never turns duplicate inputs or out-of-range
    // outputs into an actionable review, and duplicate inputs never reach
    // signing or a wallet commit that would hide the original coin.
    void structurallyInvalidImportNeverReachesSigningOrCommit_data()
    {
        QTest::addColumn<int>("mutation");
        QTest::newRow("duplicate-input") << 0;
        QTest::newRow("int64-max-outputs-wrap-to-two") << 1;
        QTest::newRow("int64-min-output") << 2;
    }
    void structurallyInvalidImportNeverReachesSigningOrCommit()
    {
        QFETCH(int, mutation);
        auto* wallet = qmltest::CreateWallet(m_app, "structure-review"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto destination = qmltest::NewAddress(m_app, wallet->name());
        const auto coins = qmltest::Rpc(m_app, "listunspent", {}, wallet->name()); QVERIFY(!coins.empty());
        const auto txid = coins[0].find_value("txid").get_str(); const auto vout = coins[0].find_value("vout").getInt<uint32_t>();
        const auto raw = qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + txid + "\"]"), wallet->name()).find_value("hex").get_str();
        const auto bytes = qmltest::Call(m_app, [raw, vout, destination, mutation](interfaces::Node*) {
            CMutableTransaction funding; if (!DecodeHexTx(funding, raw)) throw std::runtime_error("funding decode");
            const auto parent = MakeTransactionRef(funding);
            const auto script = GetScriptForDestination(DecodeDestination(destination.toStdString()));
            CMutableTransaction transaction;
            transaction.vin.emplace_back(COutPoint{parent->GetHash(), vout});
            if (mutation == 0) { transaction.vin.push_back(transaction.vin.front()); transaction.vout.emplace_back(parent->vout[vout].nValue - 10'000, script); }
            for (const CAmount amount : mutation == 1 ? std::vector<CAmount>{INT64_MAX, INT64_MAX, 4} : mutation == 2 ? std::vector<CAmount>{INT64_MIN, INT64_MAX, 2} : std::vector<CAmount>{}) transaction.vout.emplace_back(amount, script);
            PartiallySignedTransaction document{transaction, 2};
            // Both duplicate inputs carry their UTXO, as in the issue; the
            // out-of-range rows omit it so the fee stays unknown.
            if (mutation == 0) for (auto& input : document.inputs) { input.non_witness_utxo = parent; input.witness_utxo = parent->vout[vout]; }
            const auto encoded = PsbtQmlModel::SerializePsbtRaw(document);
            if (!DecodeBase64PSBT(encoded.toBase64().toStdString())) throw std::runtime_error("fixture must decode");
            return encoded;
        });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("invalid-structure.psbt");
        QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); file.close();
        const auto history = qmltest::Rpc(m_app, "listtransactions", qmltest::Json("[\"*\",1000]"), wallet->name()).size();
        QCOMPARE(qmltest::SelectOpenWalletForSetup(m_app, wallet->name()), wallet);
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        auto* import_path = qmltest::Find(m_app, "psbtImportPathField"); QVERIFY(import_path); QVERIFY(import_path->setProperty("text", path));
        QVERIFY(qmltest::Click(m_app, "sendOptionsButton")); QVERIFY(qmltest::Click(m_app, "sendImportPsbtFromFileButton"));
        QTRY_VERIFY(wallet->psbtActionId() != 0 && !wallet->psbtActionBusy());
        QVERIFY2(!wallet->currentTransactionCanSend() && !wallet->currentTransactionCanSignOnly() && !wallet->currentTransactionCanBroadcast(), qPrintable(wallet->psbtActionError()));
        if (wallet->currentTransactionIsImportedPsbt()) {
            const auto amount = wallet->currentTransaction()->amountAmount()->satoshi();
            QVERIFY2(amount != 2 && amount != 1 && MoneyRange(amount), qPrintable(QString::number(amount)));
            auto* review = qmltest::Find(m_app, "sendTransactionReviewPage"); auto* send = qmltest::Find(m_app, "sendTransactionReviewSendButton");
            QVERIFY(!review || !review->property("visible").toBool() || !send || !send->property("enabled").toBool());
        }
        // Even a direct request (bypassing the disabled control) must not sign
        // or commit; the wallet keeps the coin spendable and records nothing.
        if (wallet->currentTransaction()) {
            const auto direct = StartSend(*wallet);
            if (direct) QTRY_VERIFY(direct->finished());
            QVERIFY(!direct || (direct->state() != SendOperation::State::Committed && direct->state() != SendOperation::State::SignedOnly));
        }
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
        QCOMPARE(qmltest::Rpc(m_app, "listtransactions", qmltest::Json("[\"*\",1000]"), wallet->name()).size(), history);
        const auto after = qmltest::Rpc(m_app, "listunspent", {}, wallet->name());
        QVERIFY(std::any_of(after.getValues().begin(), after.getValues().end(), [&](const UniValue& coin) { return coin.find_value("txid").get_str() == txid && coin.find_value("vout").getInt<uint32_t>() == vout; }));
    }

    // #762: closing an imported review clears only the draft that predates it.
    // A payment request applied while the review was open is newer and stays.
    void closingAnImportedReviewKeepsANewerPaymentRequestDraft_data()
    {
        QTest::addColumn<bool>("newer");
        QTest::newRow("request-applied-during-review-survives") << true;
        QTest::newRow("draft-older-than-review-is-cleared") << false;
    }
    void closingAnImportedReviewKeepsANewerPaymentRequestDraft()
    {
        QFETCH(bool, newer);
        auto* wallet = qmltest::CreateWallet(m_app, newer ? "draft-newer" : "draft-older"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        auto* selected = qmltest::SelectOpenWalletForSetup(m_app, wallet->name()); QCOMPARE(selected, wallet);
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        auto* page = qmltest::WaitForVisible(m_app, "walletSendPage"); QVERIFY(page);
        auto* recipient = wallet->sendRecipientList()->currentRecipient(); QVERIFY(recipient);
        const auto requested = qmltest::NewAddress(m_app, wallet->name());
        const QString uri = "bitcoin:" + requested + "?amount=0.0123&label=Invoice";
        // The real File > Open URI path; a non-empty draft asks before replacing.
        const auto apply = [&] {
            auto* actions = qmltest::Find(m_app, "desktopMenuActions"); QVERIFY(actions);
            auto* open_uri = actions->property("openUri").value<QObject*>(); QVERIFY(open_uri);
            QTRY_VERIFY(open_uri->property("enabled").toBool());
            QVERIFY(QMetaObject::invokeMethod(open_uri, "trigger"));
            auto* dialog = qmltest::WaitForVisible(m_app, "sendUriImportPopup"); QVERIFY(dialog); QTRY_VERIFY(dialog->property("opened").toBool());
            QVERIFY(qmltest::SetText(m_app, "sendUriImportInput", uri));
            QVERIFY(qmltest::Click(m_app, "sendUriImportApplyButton"));
            auto* confirm = qmltest::Find(m_app, "sendPaymentUriOverwritePopup");
            if (confirm && QTest::qWaitFor([&] { return confirm->property("opened").toBool(); }, 1'000)) QVERIFY(qmltest::Click(m_app, "sendPaymentUriOverwriteConfirmButton"));
            QTRY_VERIFY_WITH_TIMEOUT(recipient->amount()->satoshi() == 1'230'000 && recipient->address()->address() == requested, 10'000);
        };
        if (!newer) { apply(); if (QTest::currentTestFailed()) return; }
        const auto payee = qmltest::NewAddress(m_app, wallet->name());
        const auto funded = qmltest::Rpc(m_app, "walletcreatefundedpsbt", qmltest::Json("[[],[{\"" + payee.toStdString() + "\":0.005}]]"), wallet->name());
        const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(funded.find_value("psbt").get_str()));
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("review.psbt");
        QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); file.close();
        QCOMPARE(AwaitPsbtImport(*wallet, path), WalletQmlModel::PsbtImportResult::WalletCanSign);
        QVERIFY(wallet->currentTransactionIsImportedPsbt());
        if (newer) { apply(); if (QTest::currentTestFailed()) return; }
        // The review's close path (SendCreate.qml onClosed) discards it.
        wallet->discardCurrentTransaction();
        QTRY_VERIFY(!wallet->currentTransaction());
        // Clearing may replace the recipient object; read the current one each time.
        const auto current = [wallet] { return wallet->sendRecipientList()->currentRecipient(); };
        const auto holds = [&] { auto* r = current(); return r && r->amount()->satoshi() == 1'230'000 && r->address()->address() == requested; };
        if (newer) {
            QVERIFY(!QTest::qWaitFor([&] { return !holds(); }, 1'000));
        } else {
            QTRY_VERIFY(current() && current()->amount()->satoshi() == 0 && current()->address()->address().isEmpty());
        }
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void legacySelfTransferReviewAndFinalizedKnownIdentity()
    {
        auto* miner = qmltest::CreateWallet(m_app, "identity-miner"); QVERIFY(miner);
        auto* wallet = qmltest::CreateWallet(m_app, "identity-reviewer"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *miner));
        const auto funding_address = qmltest::NewAddress(m_app, wallet->name(), "legacy");
        const auto destination = qmltest::NewAddress(m_app, wallet->name(), "legacy");
        const auto funding = qmltest::Rpc(m_app, "sendtoaddress",
            qmltest::Json("[\"" + funding_address.toStdString() + "\",0.01]"), miner->name()).get_str();
        const auto mining = qmltest::NewAddress(m_app, miner->name());
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + mining.toStdString() + "\"]"));
        QVERIFY(QTest::qWaitFor([&] {
            try { return qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + funding + "\"]"), wallet->name()).isObject(); }
            catch (...) { return false; }
        }, 5'000));
        const auto raw = qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + funding + "\"]"), miner->name()).find_value("hex").get_str();
        const auto original = qmltest::Call(m_app, [raw, funding_address, destination](interfaces::Node*) {
            CMutableTransaction funding_tx;
            if (!DecodeHexTx(funding_tx, raw)) throw std::runtime_error("funding decode");
            const auto previous = MakeTransactionRef(funding_tx);
            const auto script = GetScriptForDestination(DecodeDestination(funding_address.toStdString()));
            const auto found = std::find_if(previous->vout.begin(), previous->vout.end(), [&](const CTxOut& output) { return output.scriptPubKey == script; });
            if (found == previous->vout.end() || found->nValue != 1'000'000) throw std::runtime_error("independent funding output");
            CMutableTransaction transaction;
            transaction.vin.emplace_back(COutPoint{previous->GetHash(), uint32_t(found - previous->vout.begin())});
            transaction.vout.emplace_back(990'000, GetScriptForDestination(DecodeDestination(destination.toStdString())));
            PartiallySignedTransaction document{transaction, 2};
            document.inputs[0].non_witness_utxo = previous;
            return std::pair{PsbtQmlModel::SerializePsbtRaw(document), transaction.GetHash().ToString()};
        });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto original_path = directory.filePath("self-transfer.psbt");
        QFile source{original_path}; QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write(original.first), original.first.size()); source.close();
        {
            // The same real-Core document can be inspected and exported with
            // no selected wallet. Production background composition owns both
            // the parsed document and its analysis throughout these commands.
            PsbtQmlModel standalone{false, nullptr};
            QVERIFY(standalone.requestLoadFromFile(original_path));
            QTRY_VERIFY(!standalone.busy());
            QVERIFY(standalone.loaded());
            QCOMPARE(standalone.phase(), PsbtQmlModel::Phase::Ready);
            QVERIFY(!standalone.canSign()); QVERIFY(!standalone.canBroadcast());
            QCOMPARE(PsbtQmlModel::SerializePsbtRaw(*standalone.document()), original.first);
            standalone.close();
        }
        QCOMPARE(qmltest::SelectOpenWalletForSetup(m_app, wallet->name()), wallet);
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        auto* import_path = qmltest::Find(m_app, "psbtImportPathField"); QVERIFY(import_path);
        QVERIFY(import_path->setProperty("text", original_path));
        QVERIFY(qmltest::Click(m_app, "sendOptionsButton"));
        QVERIFY(qmltest::Click(m_app, "sendImportPsbtFromFileButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QTRY_VERIFY(wallet->currentTransactionIsImportedPsbt());
        QVERIFY(wallet->currentTransaction());
        const auto* notice = qmltest::WaitForVisible(m_app, "sendReviewImportedOutputsNotice"); QVERIFY(notice);
        QCOMPARE(qmltest::Find(m_app, "sendTransactionReviewRecipients")->property("title").toString(), QStringLiteral("Outputs"));
        QCOMPARE(qmltest::Find(m_app, "sendTransactionReviewTotal")->property("title").toString(), QStringLiteral("Outputs plus fee"));
        QCOMPARE(wallet->currentTransaction()->amountAmount()->satoshi(), qint64{990'000});
        QCOMPARE(wallet->currentTransaction()->getTransactionFee(), CAmount{10'000});
        const auto outputs = wallet->currentTransactionFlow().value("outputs").toList();
        QCOMPARE(outputs.size(), 1);
        QCOMPARE(outputs.front().toMap().value("ownership").toString(), QStringLiteral("wallet"));
        QVERIFY(!outputs.front().toMap().value("changeKnown").toBool());
        QCOMPARE(wallet->currentTransaction()->reviewedRecipients().size(), 1);
        QVERIFY(wallet->currentTransactionCanSend());
        QVERIFY(!wallet->currentTransactionCanBroadcast());
        const auto signed_result = qmltest::Rpc(m_app, "walletprocesspsbt",
            qmltest::Json("[\"" + original.first.toBase64().toStdString() + "\",true,\"ALL\",true,true]"), wallet->name());
        QVERIFY(signed_result.find_value("complete").get_bool());
        const auto encoded = signed_result.find_value("psbt").get_str();
        const auto finalized = qmltest::Rpc(m_app, "finalizepsbt", qmltest::Json("[\"" + encoded + "\"]"));
        QVERIFY(finalized.find_value("complete").get_bool());
        const auto hex = finalized.find_value("hex").get_str();
        const auto decoded = qmltest::Rpc(m_app, "decoderawtransaction", qmltest::Json("[\"" + hex + "\"]"));
        const auto final_txid = decoded.find_value("txid").get_str();
        QVERIFY(final_txid != original.second);
        QCOMPARE(qmltest::Rpc(m_app, "sendrawtransaction", qmltest::Json("[\"" + hex + "\"]")).get_str(), final_txid);
        QVERIFY(QTest::qWaitFor([&] {
            try { return qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + final_txid + "\"]"), wallet->name()).isObject(); }
            catch (...) { return false; }
        }, 5'000));
        const auto finalized_bytes = QByteArray::fromBase64(QByteArray::fromStdString(encoded));
        const auto signed_path = directory.filePath("recorded-finalized.psbt");
        QFile signed_file{signed_path}; QVERIFY(signed_file.open(QIODevice::WriteOnly));
        QCOMPARE(signed_file.write(finalized_bytes), finalized_bytes.size()); signed_file.close();
        QCOMPARE(AwaitPsbtImport(*wallet, signed_path), WalletQmlModel::PsbtImportResult::TransactionAlreadyKnown);
        QCOMPARE(wallet->importedPsbt()->matchedTxid(), QString::fromStdString(final_txid));
        QCOMPARE(qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + final_txid + "\"]"), wallet->name()).find_value("fee").getValStr(), std::string{"-0.00010000"});
    }

    // #1117: the wallet's own change is presented the same way whether or not
    // the wallet can sign. Through Send -> Import PSBT, a keyed wallet and a
    // watch-only copy of it both list the payee and the change as outputs with
    // the same total, and the flow marks the change as the wallet's.
    void ownOutputsArePresentedTheSameWithoutSigningKeys()
    {
        auto* miner = qmltest::CreateWallet(m_app, "own-miner"); QVERIFY(miner);
        QVERIFY(qmltest::FundWallet(m_app, *miner));
        const auto descriptors = qmltest::Call(m_app, [](interfaces::Node*) {
            std::array<unsigned char, 32> bytes{}; bytes.back() = 7;
            CKey key; key.Set(bytes.begin(), bytes.end(), true);
            const auto private_desc = "wpkh(" + EncodeSecret(key) + ")", public_desc = "wpkh(" + HexStr(key.GetPubKey()) + ")";
            return std::pair{private_desc + "#" + GetDescriptorChecksum(private_desc), public_desc + "#" + GetDescriptorChecksum(public_desc)};
        });
        const std::array<std::pair<QString, bool>, 2> wallets{{{"own-keyed", true}, {"own-watch", false}}};
        for (const auto& [name, keys] : wallets) {
            UniValue create{UniValue::VOBJ};
            create.pushKV("wallet_name", name.toStdString()); create.pushKV("disable_private_keys", !keys); create.pushKV("blank", true);
            qmltest::Rpc(m_app, "createwallet", create);
            QVERIFY(qmltest::SelectOpenWalletForSetup(m_app, name));
            UniValue entry{UniValue::VOBJ}; entry.pushKV("desc", keys ? descriptors.first : descriptors.second); entry.pushKV("timestamp", "now");
            UniValue entries{UniValue::VARR}; entries.push_back(entry);
            UniValue params{UniValue::VARR}; params.push_back(entries);
            QVERIFY(qmltest::Rpc(m_app, "importdescriptors", params, name)[0].find_value("success").get_bool());
        }
        UniValue derive{UniValue::VARR}; derive.push_back(descriptors.second);
        const auto own = qmltest::Rpc(m_app, "deriveaddresses", derive)[0].get_str();
        qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("[\"" + own + "\",1]"), miner->name());
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + qmltest::NewAddress(m_app, miner->name()).toStdString() + "\"]"));
        for (const auto& [name, keys] : wallets) {
            QVERIFY(QTest::qWaitFor([&, name = name] { return qmltest::Rpc(m_app, "listunspent", {}, name).size() == 1; }, 5'000));
        }
        const auto payee = qmltest::NewAddress(m_app, miner->name()).toStdString();
        const auto encoded = qmltest::Rpc(m_app, "walletcreatefundedpsbt", qmltest::Json("[[],[{\"" + payee + "\":0.1}],0,{\"changeAddress\":\""
            + own + "\",\"fee_rate\":2}]"), "own-watch").find_value("psbt").get_str();
        const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(encoded));
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("own-change.psbt");
        QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); file.close();
        std::vector<std::tuple<qsizetype, qint64, CAmount, QString, QString>> seen;
        for (const auto& [name, keys] : wallets) {
            auto* wallet = qmltest::SelectOpenWalletForSetup(m_app, name); QVERIFY(wallet);
            QVERIFY(qmltest::Click(m_app, "sendTabButton"));
            QVERIFY(qmltest::Find(m_app, "psbtImportPathField")->setProperty("text", path));
            QVERIFY(qmltest::Click(m_app, "sendOptionsButton"));
            QVERIFY(qmltest::Click(m_app, "sendImportPsbtFromFileButton"));
            QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
            QTRY_VERIFY(wallet->currentTransactionIsImportedPsbt());
            QCOMPARE(wallet->psbtImportResult(), keys ? WalletQmlModel::PsbtImportResult::WalletCanSign : WalletQmlModel::PsbtImportResult::WalletCannotSign);
            QCOMPARE(qmltest::Find(m_app, "sendTransactionReviewRecipients")->property("title").toString(), QStringLiteral("Outputs"));
            QCOMPARE(qmltest::Find(m_app, "sendTransactionReviewTotal")->property("title").toString(), QStringLiteral("Outputs plus fee"));
            QTRY_COMPARE(qmltest::Find(m_app, "sendTransactionReviewRecipientRows")->property("count").toInt(), 2);
            QMap<QString, QString> ownership;
            for (const auto& output : wallet->currentTransactionFlow().value("outputs").toList()) {
                ownership.insert(output.toMap().value("address").toString(), output.toMap().value("ownership").toString());
            }
            QCOMPARE(ownership.value(QString::fromStdString(own)), QStringLiteral("wallet"));
            QVERIFY(ownership.value(QString::fromStdString(payee)) != QStringLiteral("wallet"));
            seen.emplace_back(wallet->currentTransaction()->reviewedRecipients().size(), wallet->currentTransaction()->amountAmount()->satoshi(),
                wallet->currentTransaction()->getTransactionFee(), wallet->currentTransaction()->total(), qmltest::Find(m_app, "sendTransactionReviewTotal")->property("value").toString());
            QVERIFY(qmltest::Click(m_app, "sendTransactionReviewCloseButton"));
            QTRY_VERIFY(!qmltest::Find(m_app, "transactionReviewPopup")->property("visible").toBool());
        }
        QCOMPARE(std::get<0>(seen[0]), qsizetype{2});
        QCOMPARE(std::get<1>(seen[0]) + std::get<2>(seen[0]), qint64{COIN}); // Payee and change, not the payee alone.
        QCOMPARE(seen[1], seen[0]);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void localSignOnlyPinsTheFinalizedCandidate_data()
    {
        QTest::addColumn<bool>("legacy");
        QTest::newRow("legacy-scriptSig-identity") << true;
        QTest::newRow("witness-identity") << false;
    }
    void localSignOnlyPinsTheFinalizedCandidate()
    {
        QFETCH(bool, legacy);
        auto* wallet = qmltest::CreateWallet(m_app, "local-sign-review"); QVERIFY(wallet);
        const auto mining = qmltest::NewAddress(m_app, wallet->name(), legacy ? "legacy" : "bech32");
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[101,\"" + mining.toStdString() + "\"]"));
        QVERIFY(QTest::qWaitFor([&] { return wallet->balanceSatoshi() > 0; }, 5'000));
        const auto recipient = qmltest::NewAddress(m_app, wallet->name());
        const auto encoded = qmltest::Rpc(m_app, "walletcreatefundedpsbt",
            qmltest::Json("[[],[{\"" + recipient.toStdString() + "\":0.005}]]"), wallet->name()).find_value("psbt").get_str();
        const auto bytes = qmltest::Call(m_app, [encoded](interfaces::Node*) {
            const auto document = DecodeBase64PSBT(encoded);
            if (!document) throw std::runtime_error("local sign-only fixture");
            return psbtwork::SerializeRaw(*document);
        });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("unsigned.psbt");
        QFile source{path}; QVERIFY(source.open(QIODevice::WriteOnly)); QCOMPARE(source.write(bytes), bytes.size()); source.close();
        QCOMPARE(AwaitPsbtImport(*wallet, path), WalletQmlModel::PsbtImportResult::WalletCanSign);
        QVERIFY(wallet->currentTransactionCanSignOnly());
        const auto unsigned_identity = wallet->currentTransaction()->getWtx()->GetHash();
        const auto sign = StartSend(*wallet, true); QVERIFY(sign);
        QVERIFY(QTest::qWaitFor([&] { return sign->finished(); }, 5'000));
        QCOMPARE(sign->state(), SendOperation::State::SignedOnly);
        QVERIFY(wallet->currentTransactionCanBroadcast());
        const auto signed_transaction = wallet->currentTransaction()->getWtx(); QVERIFY(signed_transaction);
        QCOMPARE(signed_transaction->GetHash() == unsigned_identity, !legacy);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
        const auto saved_path = directory.filePath("signed.psbt"); QVERIFY(AwaitPsbtSave(*wallet, saved_path));
        QFile saved{saved_path}; QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto exported = qmltest::Rpc(m_app, "finalizepsbt", qmltest::Json("[\"" + saved.readAll().toBase64().toStdString() + "\"]"));
        QVERIFY(exported.find_value("complete").get_bool());
        const auto decoded = qmltest::Rpc(m_app, "decoderawtransaction", qmltest::Json("[\"" + exported.find_value("hex").get_str() + "\"]"));
        QCOMPARE(decoded.find_value("hash").get_str(), signed_transaction->GetWitnessHash().ToString());
        QVERIFY(AwaitPsbtBroadcast(*wallet));
        const auto pool = qmltest::Rpc(m_app, "getrawmempool"); QCOMPARE(pool.size(), 1U);
        QCOMPARE(pool[0].get_str(), signed_transaction->GetHash().ToString());
        QVERIFY(!wallet->currentTransactionCanBroadcast()); QVERIFY(!wallet->currentTransactionCanSend());
    }

    // #762: an imported PSBT keeps its own document, separate from the
    // wallet's analysis copy. Review, recheck and Save export the imported
    // bytes even though the wallet adds input data to that copy; only an
    // explicit Sign changes what is saved; a failed Broadcast leaves it alone.
    void importedPsbtExportChangesOnlyWhenSigned()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "imported-session"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto recipient = qmltest::NewAddress(m_app, wallet->name());
        const auto encoded = qmltest::Rpc(m_app, "walletcreatefundedpsbt",
            qmltest::Json("[[],[{\"" + recipient.toStdString() + "\":0.005}],0,{\"fee_rate\":2},false]"), wallet->name()).find_value("psbt").get_str();
        // Withhold the full previous transactions, which the wallet can add back.
        const auto bytes = qmltest::Call(m_app, [encoded](interfaces::Node*) {
            auto document = DecodeBase64PSBT(encoded);
            if (!document) throw std::runtime_error("imported session fixture");
            for (auto& input : document->inputs) { if (input.witness_utxo.IsNull()) throw std::runtime_error("witness fixture"); input.non_witness_utxo = nullptr; }
            return psbtwork::SerializeRaw(*document);
        });
        const auto enriched = qmltest::Rpc(m_app, "walletprocesspsbt",
            qmltest::Json("[\"" + bytes.toBase64().toStdString() + "\",false,\"ALL\",false,false]"), wallet->name()).find_value("psbt").get_str();
        QVERIFY(QByteArray::fromBase64(QByteArray::fromStdString(enriched)) != bytes);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("imported.psbt");
        QFile source{path}; QVERIFY(source.open(QIODevice::WriteOnly)); QCOMPARE(source.write(bytes), bytes.size()); source.close();
        const auto read = [](const QString& file) { QFile in{file}; return in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray{}; };
        QCOMPARE(AwaitPsbtImport(*wallet, path), WalletQmlModel::PsbtImportResult::WalletCanSign);
        QVERIFY(wallet->currentTransactionCanSignOnly());
        QVERIFY(AwaitPsbtSave(*wallet, directory.filePath("reviewed.psbt")));
        QCOMPARE(read(directory.filePath("reviewed.psbt")), bytes);
        QVERIFY(AwaitPsbtAction(*wallet, wallet->requestRecheckCurrentTransaction()));
        QVERIFY(AwaitPsbtSave(*wallet, directory.filePath("rechecked.psbt")));
        QCOMPARE(read(directory.filePath("rechecked.psbt")), bytes);
        const auto sign = StartSend(*wallet, true); QVERIFY(sign);
        QVERIFY(QTest::qWaitFor([&] { return sign->finished(); }, 5'000));
        QCOMPARE(sign->state(), SendOperation::State::SignedOnly);
        QVERIFY(AwaitPsbtSave(*wallet, directory.filePath("signed.psbt")));
        const auto signed_bytes = read(directory.filePath("signed.psbt"));
        QVERIFY(signed_bytes != bytes);
        QVERIFY(qmltest::Rpc(m_app, "finalizepsbt", qmltest::Json("[\"" + signed_bytes.toBase64().toStdString() + "\"]")).find_value("complete").get_bool());
        QCOMPARE(read(path), bytes);
        // Spend the same coin elsewhere so the reviewed Broadcast fails.
        qmltest::Rpc(m_app, "sendall", qmltest::Json("[[\"" + qmltest::NewAddress(m_app, wallet->name()).toStdString() + "\"]]"), wallet->name());
        QVERIFY(!AwaitPsbtBroadcast(*wallet));
        QVERIFY(!wallet->currentTransactionCanBroadcast());
        QVERIFY(AwaitPsbtSave(*wallet, directory.filePath("after-broadcast.psbt")));
        QCOMPARE(read(directory.filePath("after-broadcast.psbt")), signed_bytes);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 1U);
    }

    // #1071: a signed PSBT whose finalized transaction was already broadcast
    // is recognized by its finalized txid. Legacy and wrapped-SegWit scriptSigs
    // change the txid; native SegWit is the control. Checked before and after
    // confirmation.
    void broadcastSignedPsbtIsKnownByItsFinalizedIdentity_data()
    {
        QTest::addColumn<QString>("type");
        QTest::addColumn<bool>("identity_changes");
        QTest::newRow("legacy-p2pkh") << QStringLiteral("legacy") << true;
        QTest::newRow("wrapped-p2sh-segwit") << QStringLiteral("p2sh-segwit") << true;
        QTest::newRow("native-segwit-control") << QStringLiteral("bech32") << false;
    }
    void broadcastSignedPsbtIsKnownByItsFinalizedIdentity()
    {
        QFETCH(QString, type); QFETCH(bool, identity_changes);
        auto* wallet = qmltest::CreateWallet(m_app, "known-identity"); QVERIFY(wallet);
        const auto mining = qmltest::NewAddress(m_app, wallet->name(), qPrintable(type));
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[101,\"" + mining.toStdString() + "\"]"));
        QVERIFY(QTest::qWaitFor([&] { return wallet->balanceSatoshi() > 0; }, 5'000));
        const auto recipient = qmltest::NewAddress(m_app, wallet->name());
        const auto created = qmltest::Rpc(m_app, "walletcreatefundedpsbt", qmltest::Json("[[],[{\"" + recipient.toStdString() + "\":0.005}]]"), wallet->name()).find_value("psbt").get_str();
        const auto signed_psbt = qmltest::Rpc(m_app, "walletprocesspsbt", qmltest::Json("[\"" + created + "\",true,\"ALL\",true,false]"), wallet->name());
        const auto encoded = signed_psbt.find_value("psbt").get_str();
        const auto [bytes, unsigned_txid] = qmltest::Call(m_app, [encoded](interfaces::Node*) {
            const auto document = DecodeBase64PSBT(encoded);
            if (!document) throw std::runtime_error("signed identity fixture");
            for (const auto& input : document->inputs) if (input.partial_sigs.empty() || !input.final_script_sig.empty() || !input.final_script_witness.IsNull()) throw std::runtime_error("fixture must be signed, not finalized");
            return std::pair{psbtwork::SerializeRaw(*document), document->GetUnsignedTx()->GetHash().ToString()};
        });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("signed.psbt");
        QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); file.close();
        const auto finalized = qmltest::Rpc(m_app, "finalizepsbt", qmltest::Json("[\"" + encoded + "\"]")); QVERIFY(finalized.find_value("complete").get_bool());
        const auto hex = finalized.find_value("hex").get_str();
        const auto final_txid = qmltest::Rpc(m_app, "sendrawtransaction", qmltest::Json("[\"" + hex + "\"]")).get_str();
        QCOMPARE(final_txid != unsigned_txid, identity_changes);
        QVERIFY(QTest::qWaitFor([&] {
            try { return qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + final_txid + "\"]"), wallet->name()).isObject(); }
            catch (...) { return false; }
        }, 5'000));
        for (const bool confirmed : {false, true}) {
            if (confirmed) qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + mining.toStdString() + "\"]"));
            QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), confirmed ? 0U : 1U);
            QCOMPARE(AwaitPsbtImport(*wallet, path), WalletQmlModel::PsbtImportResult::TransactionAlreadyKnown);
            QCOMPARE(wallet->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtKnown);
            QCOMPARE(wallet->importedPsbt()->matchedTxid(), QString::fromStdString(final_txid));
            QVERIFY(!wallet->currentTransactionCanBroadcast()); QVERIFY(!wallet->currentTransactionCanSend());
        }
    }

    void nodeReadinessAndIndependentSpend_data()
    {
        QTest::addColumn<int>("scenario");
        QTest::newRow("confirmed-spend-before-review") << 0;
        QTest::newRow("nonfinal-locktime") << 1;
        QTest::newRow("below-advertised-relay-fee") << 2;
        QTest::newRow("mempool-replacement-rejection") << 3;
        QTest::newRow("confirmed-spend-after-review") << 4;
        QTest::newRow("relative-lock-matures-after-review") << 5;
        // #1071: a valid replacement is accepted, but the review must name
        // the unconfirmed transaction it evicts before broadcast.
        QTest::newRow("mempool-replacement-accepted") << 6;
    }
    void nodeReadinessAndIndependentSpend()
    {
        QFETCH(int, scenario);
        auto* wallet = qmltest::CreateWallet(m_app, "policy-review"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto mining = qmltest::NewAddress(m_app, wallet->name());
        const auto destination = qmltest::NewAddress(m_app, wallet->name());
        auto coins = qmltest::Rpc(m_app, "listunspent", {}, wallet->name());
        QVERIFY(!coins.empty());
        std::string fresh_funding_txid;
        if (scenario == 5) {
            // Spend a freshly confirmed non-coinbase output so a two-block
            // relative height lock is known to be non-final for the next block.
            const auto fresh_address = qmltest::NewAddress(m_app, wallet->name());
            fresh_funding_txid = qmltest::Rpc(m_app, "sendtoaddress",
                qmltest::Json("[\"" + fresh_address.toStdString() + "\",0.01]"), wallet->name()).get_str();
            qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + mining.toStdString() + "\"]"));
            QVERIFY(QTest::qWaitFor([&] {
                coins = qmltest::Rpc(m_app, "listunspent", {}, wallet->name());
                return std::any_of(coins.getValues().begin(), coins.getValues().end(), [&](const UniValue& item) {
                    return item.find_value("txid").get_str() == fresh_funding_txid;
                });
            }, 5'000));
        }
        const auto selected = scenario == 5
            ? std::find_if(coins.getValues().begin(), coins.getValues().end(), [&](const UniValue& item) {
                return item.find_value("txid").get_str() == fresh_funding_txid;
            })
            : coins.getValues().begin();
        QVERIFY(selected != coins.getValues().end());
        const auto& coin = *selected;
        const auto value = ParseMoney(coin.find_value("amount").getValStr()); QVERIFY(value);
        const auto sequence = scenario == 5 ? "2" : "4294967293";
        const auto input = "[{\"txid\":\"" + coin.find_value("txid").get_str() + "\",\"vout\":"
            + coin.find_value("vout").getValStr() + ",\"sequence\":" + sequence + "}]";
        const auto make_signed = [&](CAmount fee, int locktime) {
            const auto outputs = "[{\"" + destination.toStdString() + "\":" + FormatMoney(*value - fee) + "}]";
            const auto created = qmltest::Rpc(m_app, "createpsbt", qmltest::Json("[" + input + "," + outputs + "," + util::ToString(locktime) + "]"));
            const auto signed_value = qmltest::Rpc(m_app, "walletprocesspsbt", qmltest::Json("[\"" + created.get_str() + "\",true,\"ALL\",false]"), wallet->name());
            return signed_value.find_value("psbt").get_str();
        };
        const auto encoded = make_signed(scenario == 2 ? 1 : scenario == 6 ? 10'000 : 1'000, scenario == 1 ? 200 : 0);
        const auto finalized = qmltest::Rpc(m_app, "finalizepsbt", qmltest::Json("[\"" + encoded + "\"]"));
        QVERIFY(finalized.find_value("complete").get_bool());
        const auto raw = finalized.find_value("hex").get_str();
        std::string competing;
        const auto independent_spend = [&](bool confirm) {
            const auto signed_other = make_signed(2'000, 0);
            const auto other = qmltest::Rpc(m_app, "finalizepsbt", qmltest::Json("[\"" + signed_other + "\"]"));
            competing = qmltest::Rpc(m_app, "sendrawtransaction", qmltest::Json("[\"" + other.find_value("hex").get_str() + "\"]")).get_str();
            if (confirm) qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + mining.toStdString() + "\"]"));
        };
        if (scenario == 0 || scenario == 3 || scenario == 6) independent_spend(scenario == 0);
        const auto imported = qmltest::Call(m_app, [encoded](interfaces::Node*) {
            const auto parsed = DecodeBase64PSBT(encoded);
            if (!parsed) throw std::runtime_error("signed policy fixture");
            return psbtwork::SerializeRaw(*parsed);
        });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("reviewed.psbt");
        QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(imported), imported.size()); file.close();
        auto* model = wallet->importedPsbt(); QVERIFY(model);
        QVERIFY(model->requestLoadFromFile(path));
        QVERIFY(QTest::qWaitFor([&] { return !model->busy(); }, 5'000));
        QVERIFY(model->loaded()); QVERIFY(model->complete()); QVERIFY(model->signaturesVerified());
        if (scenario < 3 || scenario == 5) {
            QVERIFY(!model->canBroadcast());
            const auto status = model->status().toLower();
            QVERIFY2(status.contains(scenario == 0 ? "spent" : scenario == 1 ? "lock time"
                : scenario == 2 ? "minimum" : "non-bip68-final"), qPrintable(status));
        } else if (scenario == 6) {
            QVERIFY(model->canBroadcast());
            QVERIFY2(model->status().contains(QString::fromStdString(competing)), qPrintable(model->status()));
        } else {
            if (scenario == 3) {
                QVERIFY(!model->canBroadcast());
                QVERIFY(!model->requestBroadcast());
                QVERIFY(!model->submitted());
            } else {
                QVERIFY(model->canBroadcast());
                independent_spend(true);
                QVERIFY(model->requestBroadcast());
                QVERIFY(QTest::qWaitFor([&] { return !model->busy(); }, 5'000));
                QVERIFY(!model->submitted()); QVERIFY(!model->error().isEmpty());
                QVERIFY(!model->canBroadcast()); QVERIFY(!model->requestBroadcast());
            }
        }
        const auto final_txid = qmltest::Rpc(m_app, "decoderawtransaction", qmltest::Json("[\"" + raw + "\"]")).find_value("txid").get_str();
        if (scenario == 0 || scenario == 6) {
            // #1071: the Send -> Import PSBT review reaches the same verdict.
            const auto result = AwaitPsbtImport(*wallet, path);
            QVERIFY(result == WalletQmlModel::PsbtImportResult::ReviewPending || result == WalletQmlModel::PsbtImportResult::WalletCanSign);
            QVERIFY(wallet->currentTransactionIsImportedPsbt());
            const auto policy = wallet->currentTransaction()->nodePolicyStatus();
            QCOMPARE(wallet->currentTransactionCanBroadcast() || wallet->currentTransactionCanSend(), scenario == 6);
            QVERIFY2(policy.contains(scenario == 0 ? QStringLiteral("spent") : QString::fromStdString(competing)), qPrintable(policy));
            if (scenario == 6) QVERIFY(wallet->currentTransactionCanBroadcast() ? AwaitPsbtBroadcast(*wallet) : AwaitCommittedSend(*wallet));
        }
        const auto verdict = qmltest::Rpc(m_app, "testmempoolaccept", qmltest::Json("[[\"" + raw + "\"]]"))[0];
        QVERIFY(!verdict.find_value("allowed").get_bool());
        const auto reason = verdict.find_value("reject-reason").get_str();
        QVERIFY2(!reason.empty(), verdict.write().c_str());
        if (scenario == 1) QVERIFY2(reason.find("non-final") != std::string::npos, reason.c_str());
        if (scenario == 2) QVERIFY2(reason.find("fee") != std::string::npos, reason.c_str());
        if (scenario == 3) {
            QVERIFY2(reason.find("conflict") != std::string::npos || reason.find("replacement") != std::string::npos
                    || reason.find("insufficient fee") != std::string::npos, reason.c_str());
            QVERIFY(model->status().contains(QString::fromStdString(reason)));
        }
        if (scenario == 5) QVERIFY2(reason.find("non-BIP68-final") != std::string::npos, reason.c_str());
        const auto pool = qmltest::Rpc(m_app, "getrawmempool");
        QCOMPARE(pool.size(), scenario == 3 || scenario == 6 ? 1U : 0U);
        if (scenario == 3) QCOMPARE(pool[0].get_str(), competing);
        if (scenario == 6) QCOMPARE(pool[0].get_str(), final_txid);
        QCOMPARE(qmltest::Call(m_app, [document = model->psbt()](interfaces::Node*) { return psbtwork::SerializeRaw(*document); }), imported);
        if (scenario == 1) {
            QCOMPARE(AwaitPsbtImport(*wallet, path), WalletQmlModel::PsbtImportResult::ReviewPending);
            QVERIFY(wallet->currentTransactionIsImportedPsbt());
            QVERIFY(!wallet->currentTransactionCanBroadcast());
            QVERIFY(!wallet->currentTransactionCanSend());
            const auto original = wallet->currentTransaction()->getWtx()->GetWitnessHash();
            qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[100,\"" + mining.toStdString() + "\"]"));
            QVERIFY(AwaitPsbtAction(*wallet, wallet->requestRecheckCurrentTransaction()));
            QVERIFY(wallet->currentTransactionCanBroadcast());
            QCOMPARE(wallet->currentTransaction()->getWtx()->GetWitnessHash(), original);
            QVERIFY(AwaitPsbtBroadcast(*wallet));
            QVERIFY(!wallet->currentTransactionIsImportedPsbt());
            QVERIFY(!wallet->currentTransactionCanBroadcast()); QVERIFY(!wallet->currentTransactionCanSend());
            // Unavailable facade commands return a terminal error receipt;
            // a nonzero receipt does not imply dispatch to Native.
            QVERIFY(AwaitPsbtAction(*wallet, wallet->requestBroadcastCurrentTransaction()));
            QCOMPARE(wallet->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtFailed);
            const auto submitted = qmltest::Rpc(m_app, "getrawmempool");
            QCOMPARE(submitted.size(), 1U);
            QCOMPARE(submitted[0].get_str(), wallet->currentTransaction()->txid().toStdString());
            QCOMPARE(AwaitPsbtImport(*wallet, path), WalletQmlModel::PsbtImportResult::TransactionAlreadyKnown);
        }
        if (scenario == 5) {
            QCOMPARE(AwaitPsbtImport(*wallet, path), WalletQmlModel::PsbtImportResult::ReviewPending);
            QVERIFY(wallet->currentTransactionIsImportedPsbt());
            QVERIFY(!wallet->currentTransactionCanBroadcast());
            const auto reviewed = wallet->currentTransaction()->getWtx()->GetWitnessHash();
            qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + mining.toStdString() + "\"]"));
            QVERIFY(AwaitPsbtAction(*wallet, wallet->requestRecheckCurrentTransaction()));
            QVERIFY(wallet->currentTransactionCanBroadcast());
            QCOMPARE(wallet->currentTransaction()->getWtx()->GetWitnessHash(), reviewed);
            QVERIFY(AwaitPsbtBroadcast(*wallet));
            const auto pool_after = qmltest::Rpc(m_app, "getrawmempool");
            QCOMPARE(pool_after.size(), 1U);
            QCOMPARE(pool_after[0].get_str(), wallet->currentTransaction()->txid().toStdString());
        }
    }
    // #975: PSBT file reads, wallet review and signing run off the GUI thread.
    // A file still being written (a FIFO) and a wallet lock held by another
    // Core thread leave the request pending while the GUI keeps running, and a
    // second Broadcast is refused while the first is pending.
    void psbtImportSigningAndBroadcastStayOffTheGuiThread()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "off-gui-psbt"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto recipient = qmltest::NewAddress(m_app, wallet->name());
        const auto encoded = qmltest::Rpc(m_app, "walletcreatefundedpsbt",
            qmltest::Json("[[],[{\"" + recipient.toStdString() + "\":0.005}]]"), wallet->name()).find_value("psbt").get_str();
        const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(encoded));
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("unsigned.psbt");
        QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); file.close();
        const auto progressesGui = [this] {
            bool turned{false};
            QMetaObject::invokeMethod(this, [&turned] { turned = true; }, Qt::QueuedConnection);
            return QTest::qWaitFor([&turned] { return turned; }, 120'000);
        };
#ifndef WIN32
        // A FIFO holds the file read itself; Windows has no FIFO, so there only
        // the held wallet lock below stalls the import.
        const auto fifo = directory.filePath("arriving.psbt");
        QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
        std::promise<void> arrive; bool arrived{false};
        std::thread writer([fifo, bytes, ready = arrive.get_future()] {
            ready.wait_for(std::chrono::seconds{4});
            for (int attempt{0}; attempt < 100 && !FeedWaitingFifo(fifo, bytes); ++attempt) std::this_thread::sleep_for(std::chrono::milliseconds{10});
        });
        const auto cleanup = qScopeGuard([&] {
            if (!arrived) arrive.set_value();
            writer.join();
            FeedWaitingFifo(fifo, {}); // Release any reader still waiting.
        });
        const auto slow = wallet->requestImportPsbtFromFile(fifo);
        QVERIFY(slow); QVERIFY(wallet->psbtActionBusy());
        QVERIFY(progressesGui());
        QVERIFY(wallet->psbtActionBusy()); QVERIFY(!wallet->currentTransaction());
        arrived = true; arrive.set_value();
        QVERIFY(AwaitPsbtAction(*wallet, slow));
        QCOMPARE(wallet->psbtImportResult(), WalletQmlModel::PsbtImportResult::WalletCanSign);
#endif
        {
            HeldWalletLock lock{m_app, wallet->name()};
            const auto import = wallet->requestImportPsbtFromFile(path);
            QVERIFY2(!lock.expired(), "PSBT import waited for the held wallet lock");
            QVERIFY(import);
            QVERIFY(progressesGui() && !lock.expired());
            QVERIFY(wallet->psbtActionBusy());
            lock.release();
            QVERIFY(AwaitPsbtAction(*wallet, import));
        }
        QCOMPARE(wallet->psbtImportResult(), WalletQmlModel::PsbtImportResult::WalletCanSign);
        std::unique_ptr<WalletSendOperation> sign;
        {
            HeldWalletLock lock{m_app, wallet->name()};
            sign = StartSend(*wallet, true); QVERIFY(sign);
            QVERIFY2(!lock.expired(), "PSBT signing waited for the held wallet lock");
            QVERIFY(progressesGui() && !lock.expired());
            QVERIFY(wallet->sendBusy());
        }
        QVERIFY(QTest::qWaitFor([&] { return !wallet->sendBusy(); }, 5'000));
        QTRY_COMPARE(sign->state(), SendOperation::State::SignedOnly);
        QVERIFY(wallet->currentTransactionCanBroadcast());
        const auto broadcast = wallet->requestBroadcastCurrentTransaction(); QVERIFY(broadcast);
        QCOMPARE(wallet->requestBroadcastCurrentTransaction(), quint64{0});
        QVERIFY(AwaitPsbtAction(*wallet, broadcast));
        QCOMPARE(wallet->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtSubmitted);
        const auto pool = qmltest::Rpc(m_app, "getrawmempool"); QCOMPARE(pool.size(), 1U);
        QCOMPARE(pool[0].get_str(), wallet->currentTransaction()->txid().toStdString());
    }

    // draft-308-D308: an imported PSBT whose other inputs are already signed
    // can be signed and broadcast by the wallet that owns the only unsigned
    // input. A signature no wallet here can supply keeps signing hidden.
    void importedPsbtSignsWhenOnlyTheWalletsInputIsUnsigned()
    {
        auto* alpha = qmltest::CreateWallet(m_app, "cosign-alpha"); QVERIFY(alpha);
        QVERIFY(qmltest::FundWallet(m_app, *alpha));
        auto* bravo = qmltest::CreateWallet(m_app, "cosign-bravo"); QVERIFY(bravo);
        auto* charlie = qmltest::CreateWallet(m_app, "cosign-charlie"); QVERIFY(charlie);
        for (auto* other : {bravo, charlie}) {
            qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("[\"" + qmltest::NewAddress(m_app, other->name()).toStdString() + "\",1]"), alpha->name());
        }
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[3,\"" + qmltest::NewAddress(m_app, alpha->name()).toStdString() + "\"]"));
        const auto coin = [&](WalletQmlModel* wallet, const std::string& amount, size_t skip = 0) {
            const auto coins = qmltest::Rpc(m_app, "listunspent", {}, wallet->name());
            for (size_t i = 0; i < coins.size(); ++i) {
                if (coins[i].find_value("amount").getValStr() != amount) continue;
                if (skip > 0) {
                    --skip;
                    continue;
                }
                return "{\"txid\":\"" + coins[i].find_value("txid").get_str() + "\",\"vout\":" + coins[i].find_value("vout").getValStr() + "}";
            }
            return std::string{};
        };
        QVERIFY(QTest::qWaitFor([&] { return !coin(bravo, "1.00000000").empty() && !coin(charlie, "1.00000000").empty(); }, 5'000));
        const auto bravo_coin = coin(bravo, "1.00000000");
        // Alpha's input carries only its UTXO data; Bravo signs its own input.
        const auto prepare = [&](const std::string& inputs, const std::string& amount, WalletQmlModel* unsigned_owner) {
            auto psbt = qmltest::Rpc(m_app, "createpsbt", qmltest::Json("[[" + inputs + "],[{\"" + qmltest::NewAddress(m_app, alpha->name()).toStdString() + "\":" + amount + "}]]")).get_str();
            for (auto* owner : {alpha, unsigned_owner}) {
                if (owner) psbt = qmltest::Rpc(m_app, "walletprocesspsbt", qmltest::Json("[\"" + psbt + "\",false]"), owner->name()).find_value("psbt").get_str();
            }
            const auto processed = qmltest::Rpc(m_app, "walletprocesspsbt", qmltest::Json("[\"" + psbt + "\"]"), bravo->name());
            return std::pair{processed.find_value("psbt").get_str(), processed.find_value("complete").get_bool()};
        };
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto write = [&](const std::string& encoded, const QString& name) {
            const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(encoded));
            const auto path = directory.filePath(name);
            QFile file{path};
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() ? path : QString{};
        };

        // Control: Charlie's input stays unsigned, so Alpha cannot complete it.
        const auto control = prepare(coin(alpha, "50.00000000") + "," + bravo_coin + "," + coin(charlie, "1.00000000"), "51.999", charlie);
        QVERIFY(!control.second);
        const auto control_path = write(control.first, "control.psbt"); QVERIFY(!control_path.isEmpty());
        QCOMPARE(AwaitPsbtImport(*alpha, control_path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
        QVERIFY(alpha->currentTransaction()); QCOMPARE(alpha->currentTransaction()->feeAmount()->satoshi(), CAmount{100'000});
        QVERIFY(!alpha->currentTransactionCanSend()); QVERIFY(!alpha->currentTransactionCanSignOnly());
        QVERIFY(!alpha->currentTransactionCanBroadcast());
        QCOMPARE(alpha->currentTransactionReviewMessage(), QStringLiteral("This wallet does not have the keys to sign this transaction."));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);

        const auto joint = prepare(coin(alpha, "50.00000000", 1) + "," + bravo_coin, "50.999", nullptr);
        QVERIFY(!joint.second);
        const auto path = write(joint.first, "joint.psbt"); QVERIFY(!path.isEmpty());
        QCOMPARE(AwaitPsbtImport(*alpha, path), WalletQmlModel::PsbtImportResult::WalletCanSign);
        QVERIFY(alpha->currentTransactionCanSend()); QVERIFY(alpha->currentTransactionCanSignOnly());
        QVERIFY(!alpha->currentTransactionCanBroadcast());
        const auto sign = StartSend(*alpha, true); QVERIFY(sign);
        QVERIFY(QTest::qWaitFor([&] { return sign->finished(); }, 5'000));
        QVERIFY2(sign->state() == SendOperation::State::SignedOnly, qPrintable(alpha->transactionError()));
        QVERIFY(alpha->currentTransactionCanBroadcast());
        const auto broadcast = alpha->requestBroadcastCurrentTransaction(); QVERIFY(broadcast);
        QVERIFY(AwaitPsbtAction(*alpha, broadcast));
        QCOMPARE(alpha->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtSubmitted);
        const auto pool = qmltest::Rpc(m_app, "getrawmempool"); QCOMPARE(pool.size(), 1U);
        const auto txid = alpha->currentTransaction()->txid().toStdString();
        QCOMPARE(pool[0].get_str(), txid);
        QCOMPARE(qmltest::Rpc(m_app, "getrawtransaction", qmltest::Json("[\"" + txid + "\",true]")).find_value("vin").size(), 2U);
    }
    void watchOnlySignedScriptsSurviveOriginalExport_data()
    {
        QTest::addColumn<bool>("nested"); QTest::addColumn<bool>("send");
        for (bool nested : {false, true}) for (bool send : {false, true})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(nested ? "nested" : "witness").arg(send ? "send" : "broadcast"))) << nested << send;
    }
    void watchOnlySignedScriptsSurviveOriginalExport()
    {
        QFETCH(bool, nested); QFETCH(bool, send);
        auto* miner = qmltest::CreateWallet(m_app, "scripts-miner"); QVERIFY(miner);
        QVERIFY(qmltest::FundWallet(m_app, *miner));
        UniValue create{UniValue::VOBJ};
        create.pushKV("wallet_name", "scripts-reviewer"); create.pushKV("disable_private_keys", true); create.pushKV("blank", true);
        qmltest::Rpc(m_app, "createwallet", create);
        auto* reviewer = qmltest::SelectOpenWalletForSetup(m_app, "scripts-reviewer"); QVERIFY(reviewer);
        const auto descriptors = qmltest::Call(m_app, [nested](interfaces::Node*) {
            std::array<unsigned char, 32> bytes{};
            bytes.back() = 1; CKey first; first.Set(bytes.begin(), bytes.end(), true);
            bytes.back() = 2; CKey second; second.Set(bytes.begin(), bytes.end(), true);
            auto private_desc = "wsh(multi(2," + EncodeSecret(first) + "," + EncodeSecret(second) + "))";
            auto public_desc = "wsh(multi(2," + HexStr(first.GetPubKey()) + "," + HexStr(second.GetPubKey()) + "))";
            if (nested) { private_desc = "sh(" + private_desc + ")"; public_desc = "sh(" + public_desc + ")"; }
            return std::pair{private_desc + "#" + GetDescriptorChecksum(private_desc), public_desc + "#" + GetDescriptorChecksum(public_desc)};
        });
        for (const auto& [wallet, descriptor] : std::array<std::pair<QString, std::string>, 2>{{
                 {miner->name(), descriptors.first}, {reviewer->name(), descriptors.second}}}) {
            UniValue entry{UniValue::VOBJ}; entry.pushKV("desc", descriptor); entry.pushKV("timestamp", "now");
            UniValue entries{UniValue::VARR}; entries.push_back(entry);
            UniValue params{UniValue::VARR}; params.push_back(entries);
            const auto imported = qmltest::Rpc(m_app, "importdescriptors", params, wallet);
            QVERIFY(imported[0].find_value("success").get_bool());
        }
        UniValue derive{UniValue::VARR}; derive.push_back(descriptors.second);
        const auto address = qmltest::Rpc(m_app, "deriveaddresses", derive)[0].get_str();
        const auto funding = qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("[\"" + address + "\",0.01]"), miner->name()).get_str();
        const auto mining = qmltest::NewAddress(m_app, miner->name());
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + mining.toStdString() + "\"]"));
        const auto received = qmltest::Json("[0,9999999,[\"" + address + "\"]]");
        QVERIFY(QTest::qWaitFor([&] { return qmltest::Rpc(m_app, "listunspent", received, reviewer->name()).size() == 1; }, 5000));
        const auto coin = qmltest::Rpc(m_app, "listunspent", received, reviewer->name())[0];
        QCOMPARE(coin.find_value("txid").get_str(), funding);
        QCOMPARE(coin.find_value("amount").getValStr(), std::string{"0.01000000"});
        const auto parent = qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + funding + "\"]"), miner->name()).find_value("hex").get_str();
        const auto destination = qmltest::NewAddress(m_app, miner->name()).toStdString();
        const auto index = coin.find_value("vout").getInt<int>();
        const auto unsigned_psbt = qmltest::Call(m_app, [parent, destination, index](interfaces::Node*) {
            CMutableTransaction funding_tx;
            if (!DecodeHexTx(funding_tx, parent)) throw std::runtime_error("funding decode");
            const auto previous = MakeTransactionRef(funding_tx);
            if (index < 0 || size_t(index) >= previous->vout.size() || previous->vout[index].nValue != 1'000'000) throw std::runtime_error("funding output");
            CMutableTransaction transaction;
            transaction.vin.emplace_back(COutPoint{previous->GetHash(), uint32_t(index)});
            transaction.vout.emplace_back(990'000, GetScriptForDestination(DecodeDestination(destination)));
            PartiallySignedTransaction document{transaction, 2}; document.inputs[0].non_witness_utxo = previous;
            return PsbtQmlModel::SerializePsbtRaw(document).toBase64().toStdString();
        });
        const auto signed_psbt = qmltest::Rpc(m_app, "walletprocesspsbt", qmltest::Json("[\"" + unsigned_psbt + "\",true,\"ALL\",false,false]"), miner->name()).find_value("psbt").get_str();
        const auto original = qmltest::Call(m_app, [signed_psbt](interfaces::Node*) {
            const auto parsed = DecodeBase64PSBT(signed_psbt);
            if (!parsed || parsed->inputs[0].partial_sigs.size() != 2) throw std::runtime_error("original signatures");
            auto complete = *parsed; CMutableTransaction extracted;
            if (!FinalizeAndExtractPSBT(complete, extracted)) throw std::runtime_error("signed control");
            auto withheld = *parsed; withheld.inputs[0].witness_script.clear(); withheld.inputs[0].redeem_script.clear();
            auto control = withheld; CMutableTransaction absent;
            if (FinalizeAndExtractPSBT(control, absent)) throw std::runtime_error("scripts were not necessary");
            return std::pair{PsbtQmlModel::SerializePsbtRaw(withheld), MakeTransactionRef(extracted)->GetWitnessHash().ToString()};
        });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto source_path = directory.filePath("withheld.psbt");
        QFile source{source_path}; QVERIFY(source.open(QIODevice::WriteOnly)); QCOMPARE(source.write(original.first), original.first.size()); source.close();
        QCOMPARE(AwaitPsbtImport(*reviewer, source_path), WalletQmlModel::PsbtImportResult::WalletCanSign);
        QVERIFY(reviewer->currentTransactionCanSend()); QVERIFY(reviewer->currentTransactionCanBroadcast());
        QVERIFY(!qmltest::Rpc(m_app, "getwalletinfo", {}, reviewer->name()).find_value("private_keys_enabled").get_bool());
        const auto saved_path = directory.filePath("preserved.psbt"); QVERIFY(AwaitPsbtSave(*reviewer, saved_path));
        QFile saved{saved_path}; QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.readAll(), original.first);
        QVERIFY2(send ? AwaitCommittedSend(*reviewer) : AwaitPsbtBroadcast(*reviewer), qPrintable(reviewer->transactionError()));
        const auto txid = reviewer->currentTransaction()->getWtx()->GetHash().ToString();
        const auto actual = qmltest::Rpc(m_app, "getrawtransaction", qmltest::Json("[\"" + txid + "\",true]"));
        QCOMPARE(actual.find_value("hash").get_str(), original.second);
    }
    void authenticatedLegacyFeeReview_data()
    {
        QTest::addColumn<uint>("version"); QTest::addColumn<int>("owned"); QTest::addColumn<bool>("forged"); QTest::addColumn<bool>("confirmed");
        for (uint version : {0U, 2U}) for (int owned : {0, 1, 2}) for (bool forged : {false, true}) {
            const auto name = QString("v%1-owned%2-%3").arg(version).arg(owned).arg(forged ? "forged" : "control").toUtf8();
            QTest::newRow(name.constData()) << version << owned << forged << true;
        }
        for (uint version : {0U, 2U}) for (bool forged : {false, true}) {
            const auto name = QString("v%1-unconfirmed-%2").arg(version).arg(forged ? "forged" : "control").toUtf8();
            QTest::newRow(name.constData()) << version << 0 << forged << false;
        }
    }
    void authenticatedLegacyFeeReview()
    {
        QFETCH(uint, version); QFETCH(int, owned); QFETCH(bool, forged); QFETCH(bool, confirmed);
        auto* miner = qmltest::CreateWallet(m_app, "fee-miner"); QVERIFY(miner);
        auto* foreign = qmltest::CreateWallet(m_app, "fee-foreign"); QVERIFY(foreign);
        auto* reviewer = qmltest::CreateWallet(m_app, "fee-reviewer"); QVERIFY(reviewer);
        QVERIFY(qmltest::FundWallet(m_app, *miner));
        const auto first = qmltest::NewAddress(m_app, owned > 0 ? reviewer->name() : foreign->name(), "legacy");
        const auto second = qmltest::NewAddress(m_app, owned > 1 ? reviewer->name() : foreign->name(), "legacy");
        const auto mining = qmltest::NewAddress(m_app, miner->name());
        const std::vector<QString> addresses{first, second};
        std::vector<std::string> funding;
        funding.reserve(addresses.size());
        for (const auto& address : addresses) {
            funding.push_back(qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("[\"" + address.toStdString() + "\", 0.01]"), miner->name()).get_str());
        }
        if (confirmed) qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1, \"" + mining.toStdString() + "\"]"));
        CMutableTransaction tx;
        std::vector<CTransactionRef> funding_transactions;
        funding_transactions.reserve(addresses.size());
        CAmount actual_inputs{0};
        for (size_t i = 0; i < addresses.size(); ++i) {
            const auto raw = qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + funding[i] + "\"]"), miner->name()).find_value("hex").get_str();
            const auto funding_transaction = qmltest::Call(m_app, [raw](interfaces::Node*) -> CTransactionRef {
                CMutableTransaction transaction;
                return DecodeHexTx(transaction, raw) ? MakeTransactionRef(std::move(transaction)) : CTransactionRef{};
            });
            QVERIFY(funding_transaction);
            QCOMPARE(funding_transaction->GetHash(), *Txid::FromHex(funding[i]));
            funding_transactions.push_back(funding_transaction);
            const auto decoded = qmltest::Rpc(m_app, "decoderawtransaction", qmltest::Json("[\"" + raw + "\"]"));
            for (const auto& output : decoded.find_value("vout").getValues()) {
                const auto address = output.find_value("scriptPubKey").find_value("address");
                if (!address.isStr() || address.get_str() != addresses[i].toStdString()) continue;
                const auto index = output.find_value("n").getInt<int>();
                tx.vin.emplace_back(COutPoint{*Txid::FromHex(funding[i]), uint32_t(index)});
                const auto coin = qmltest::Rpc(m_app, "gettxout", qmltest::Json("[\"" + funding[i] + "\", " + util::ToString(index) + "]"));
                const auto value = ParseMoney(coin.find_value("value").getValStr());
                QVERIFY(value); QCOMPARE(*value, CAmount{1'000'000});
                QVERIFY(index >= 0 && size_t(index) < funding_transaction->vout.size());
                QCOMPARE(funding_transaction->vout[index].nValue, *value);
                actual_inputs += *value;
            }
        }
        QCOMPARE(tx.vin.size(), size_t{2});
        tx.vout.emplace_back(1'500'000, GetScriptForDestination(DecodeDestination(mining.toStdString())));
        const CAmount actual_fee{actual_inputs - tx.vout[0].nValue};
        QCOMPARE(actual_fee, CAmount{500'000}); // Independent live gettxout oracle, not PSBT analysis.
        PartiallySignedTransaction unsigned_psbt{tx, version};
        // Supply independently authenticated funding transactions explicitly.
        // A sender RPC result does not acknowledge the recipient wallet's
        // asynchronous unconfirmed notification, which walletprocesspsbt would
        // otherwise need to fill missing non-witness UTXOs from wallet history.
        QCOMPARE(unsigned_psbt.inputs.size(), funding_transactions.size());
        for (size_t i{0}; i < unsigned_psbt.inputs.size(); ++i) {
            QCOMPARE(funding_transactions[i]->GetHash(), tx.vin[i].prevout.hash);
            unsigned_psbt.inputs[i].non_witness_utxo = funding_transactions[i];
        }
        auto encoded = PsbtQmlModel::SerializePsbtRaw(unsigned_psbt).toBase64().toStdString();
        for (const auto* signer : {reviewer, foreign}) {
            encoded = qmltest::Rpc(m_app, "walletprocesspsbt", qmltest::Json("[\"" + encoded + "\", true, \"ALL\", true, true]"), signer->name()).find_value("psbt").get_str();
        }
        const auto parsed = DecodeBase64PSBT(encoded); QVERIFY(parsed);
        auto psbt = *parsed;
        CMutableTransaction signed_control; QVERIFY(FinalizeAndExtractPSBT(psbt, signed_control));
        if (forged || !confirmed) {
            for (size_t i{0}; i < psbt.inputs.size(); ++i) {
                if (confirmed && i != 1) continue;
                auto& input = psbt.inputs[i]; QVERIFY(input.non_witness_utxo);
                input.witness_utxo = input.non_witness_utxo->vout[input.prev_out];
                input.non_witness_utxo = nullptr;
                if (forged && i == 1) input.witness_utxo.nValue -= actual_fee - 1;
            }
        }
        auto verification = psbt;
        CMutableTransaction signed_candidate; QVERIFY(FinalizeAndExtractPSBT(verification, signed_candidate));
        QCOMPARE(signed_candidate.GetHash(), signed_control.GetHash());
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("legacy.psbt");
        QCOMPARE(WritePsbtFile(psbt, path), QString{});
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::Find(m_app, "psbtImportPathField")->setProperty("text", path));
        QVERIFY(qmltest::Click(m_app, "sendOptionsButton"));
        QVERIFY(qmltest::Click(m_app, "sendImportPsbtFromFileButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QVERIFY(reviewer->currentTransaction());
        QTRY_VERIFY(!reviewer->transactionFeePending());
        const auto fee_text = qmltest::Find(m_app, "sendTransactionReviewFee")->property("value").toString();
        if (forged) {
            QVERIFY(!reviewer->currentTransactionCanSend()); QVERIFY(!reviewer->currentTransactionCanBroadcast());
            QCOMPARE(fee_text, QString("Unknown"));
            QVERIFY(!reviewer->currentTransactionFlow().value("feeKnown").toBool());
            QVERIFY(!AwaitPsbtBroadcast(*reviewer)); QVERIFY(!AwaitCommittedSend(*reviewer));
            QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), confirmed ? 0U : 2U);
        } else {
            QCOMPARE(reviewer->currentTransaction()->getTransactionFee(), actual_fee);
            QVERIFY(reviewer->currentTransactionCanBroadcast());
            QVERIFY2(AwaitPsbtBroadcast(*reviewer), qPrintable(reviewer->transactionError()));
            const auto entry = qmltest::Rpc(m_app, "getmempoolentry", qmltest::Json("[\"" + signed_control.GetHash().GetHex() + "\"]"));
            QCOMPARE(ParseMoney(entry.find_value("fees").find_value("base").getValStr()), std::optional<CAmount>{actual_fee});
        }
    }
    void failedImportPreservesTheDraftAndAllowsRetry_data()
    {
        QTest::addColumn<bool>("create_invalid_file");
        QTest::addColumn<QString>("expected_error");
        QTest::newRow("missing-file") << false << QStringLiteral("Could not open PSBT file");
        QTest::newRow("malformed-file") << true << QStringLiteral("Could not decode PSBT");
    }

    void failedImportPreservesTheDraftAndAllowsRetry()
    {
        QFETCH(bool, create_invalid_file);
        QFETCH(QString, expected_error);
        auto* recipient = qmltest::CreateWallet(m_app, "recipient");
        QVERIFY(recipient);
        const auto address = qmltest::NewAddress(m_app, recipient->name());
        auto* wallet = qmltest::LoadFundedWallet(m_app, "signer");
        QVERIFY(wallet);
        const auto params = qmltest::Json("[[], {\"" + address.toStdString() + "\": 1}, 0, {\"fee_rate\": 2}, true]");
        const auto psbt = qmltest::Rpc(m_app, "walletcreatefundedpsbt", params, wallet->name()).find_value("psbt").get_str();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto invalid_path = directory.filePath("invalid.psbt");
        if (create_invalid_file) {
            QFile file{invalid_path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            const QByteArray invalid{"not a PSBT"};
            QCOMPARE(file.write(invalid), invalid.size());
        }
        QFile valid{directory.filePath("valid.psbt")};
        QVERIFY(valid.open(QIODevice::WriteOnly));
        const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(psbt));
        QCOMPARE(valid.write(bytes), bytes.size());
        valid.close();

        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", address));
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "0.25"));
        auto* draft = wallet->sendRecipientList()->currentRecipient();
        QCOMPARE(draft->address()->address(), address);
        QCOMPARE(draft->amount()->satoshi(), COIN / 4);

        // Inject only the file-picker result; the menu and error handling are production QML.
        auto* path = qmltest::Find(m_app, "psbtImportPathField");
        QVERIFY(path);
        QVERIFY(path->setProperty("text", invalid_path));
        QVERIFY(qmltest::Click(m_app, "sendOptionsButton"));
        QVERIFY(qmltest::Click(m_app, "sendImportPsbtFromFileButton"));
        auto* error = qmltest::WaitForVisible(m_app, "unsupportedPsbtPopup");
        QVERIFY(error);
        QTRY_VERIFY(error->property("opened").toBool());
        QVERIFY(qmltest::Find(m_app, "unsupportedPsbtPopupMessage")->property("text").toString().contains(expected_error));
        QVERIFY(!qmltest::Find(m_app, "transactionReviewPopup")->property("visible").toBool());
        QVERIFY(!wallet->currentTransaction());
        QCOMPARE(draft->address()->address(), address);
        QCOMPARE(draft->amount()->satoshi(), COIN / 4);
        QCOMPARE(qmltest::Find(m_app, "sendAddressInput")->property("text").toString(), draft->address()->formattedAddress());
        QCOMPARE(qmltest::Find(m_app, "sendAmountInput")->property("text").toString(), QStringLiteral("0.25000000"));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);

        // Closing the error must allow a valid import without restarting or clearing the draft.
        QVERIFY(qmltest::Click(m_app, "unsupportedPsbtPopupOkButton"));
        QTRY_VERIFY(!error->property("visible").toBool());
        QVERIFY(path->setProperty("text", valid.fileName()));
        QVERIFY(qmltest::Click(m_app, "sendOptionsButton"));
        QVERIFY(qmltest::Click(m_app, "sendImportPsbtFromFileButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QTRY_VERIFY(qmltest::Find(m_app, "transactionReviewPopup")->property("opened").toBool());
        QVERIFY(wallet->currentTransaction());
        QVERIFY(wallet->currentTransactionIsImportedPsbt());
        QVERIFY(wallet->currentTransactionCanSend());
        QVERIFY(!wallet->currentTransactionCanBroadcast());
        QVERIFY(wallet->importedPsbt()->error().isEmpty());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void importSignBroadcastAndRecognizeKnownTransaction()
    {
        auto* wallet = qmltest::LoadFundedWallet(m_app, "psbt");
        QVERIFY(wallet);
        auto* recipient = qmltest::CreateWallet(m_app, "recipient");
        QVERIFY(recipient);
        const auto address = qmltest::NewAddress(m_app, recipient->name());
        const auto params = qmltest::Json("[[], {\"" + address.toStdString() + "\": 1}, 0, {\"fee_rate\": 2}, true]");
        const auto psbt = qmltest::Rpc(m_app, "walletcreatefundedpsbt", params, wallet->name()).find_value("psbt").get_str();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("unsigned.psbt");
        QFile file{path};
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(psbt));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        QCOMPARE(AwaitPsbtImport(*wallet, path), WalletQmlModel::PsbtImportResult::WalletCanSign);
        QVERIFY(wallet->currentTransaction());
        QVERIFY(wallet->currentTransactionIsImportedPsbt());
        QVERIFY(wallet->currentTransactionCanSend());
        QVERIFY(!wallet->currentTransactionCanBroadcast());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
        const auto exported_path = directory.filePath("exported.psbt");
        QVERIFY(AwaitPsbtSave(*wallet, exported_path));
        QVERIFY2(AwaitCommittedSend(*wallet), qPrintable(wallet->transactionError()));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 1U);
        QCOMPARE(AwaitPsbtImport(*wallet, exported_path), WalletQmlModel::PsbtImportResult::TransactionAlreadyKnown);
    }

    // draft-108-RC28: after an imported PSBT is committed through Send, that
    // review cannot be sent again, while the next ordinary drafts still send.
    void committedImportedPsbtIsNotSentAgain()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "psbt-resend"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto address = qmltest::NewAddress(m_app, wallet->name());
        const auto encoded = qmltest::Rpc(m_app, "walletcreatefundedpsbt",
            qmltest::Json("[[],[{\"" + address.toStdString() + "\":0.005}]]"), wallet->name()).find_value("psbt").get_str();
        const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(encoded));
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("resend.psbt");
        QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); file.close();
        QCOMPARE(AwaitPsbtImport(*wallet, path), WalletQmlModel::PsbtImportResult::WalletCanSign);
        // A committed send reaches the wallet asynchronously. Its balance update
        // marks a review prepared before it as stale (it may spend the same
        // coins), so wait for it before preparing the next draft, as a user would.
        auto balance = wallet->displayedBalanceSatoshi();
        QVERIFY2(AwaitCommittedSend(*wallet), qPrintable(wallet->transactionError()));
        QTRY_VERIFY(wallet->displayedBalanceSatoshi() != balance);
        QVERIFY(wallet->sendOperation() && wallet->sendOperation()->result() && wallet->sendOperation()->result()->committed);
        const auto committed = QString::fromStdString(wallet->sendOperation()->result()->committed->GetHash().GetHex());
        QVERIFY(!wallet->currentTransactionCanSend()); QVERIFY(!wallet->currentTransactionCanBroadcast()); QVERIFY(!wallet->currentTransactionCanSignOnly());
        // The committed review is not sent again.
        QVERIFY(!StartSend(*wallet));
        auto pool = qmltest::Rpc(m_app, "getrawmempool"); QCOMPARE(pool.size(), 1U); QCOMPARE(pool[0].get_str(), committed.toStdString());
        for (unsigned sent{2}; sent <= 3; ++sent) {
            auto* recipient = wallet->sendRecipientList()->currentRecipient();
            recipient->setAddress(qmltest::NewAddress(m_app, wallet->name()));
            recipient->amount()->setSatoshi(COIN / 100);
            // The new draft's fee estimate can land after a preparation starts,
            // which makes that preparation stale by design; prepare again, as a
            // user pressing Review again would.
            QTRY_VERIFY_WITH_TIMEOUT(AwaitPreparation(*wallet), 30'000);
            QVERIFY(!wallet->currentTransactionIsImportedPsbt());
            balance = wallet->displayedBalanceSatoshi();
            QVERIFY2(AwaitCommittedSend(*wallet), qPrintable(wallet->transactionError()));
            QTRY_VERIFY(wallet->displayedBalanceSatoshi() != balance);
            QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{sent});
        }
    }

    void foreignPsbtCanBeReviewedWithoutSigning()
    {
        auto* source = qmltest::LoadFundedWallet(m_app, "source");
        QVERIFY(source);
        auto* reviewer = qmltest::CreateWallet(m_app, "reviewer");
        QVERIFY(reviewer);
        const auto address = qmltest::NewAddress(m_app, reviewer->name());
        const auto params = qmltest::Json("[[], {\"" + address.toStdString() + "\": 1}, 0, {\"fee_rate\": 2}, true]");
        const auto psbt = qmltest::Rpc(m_app, "walletcreatefundedpsbt", params, source->name()).find_value("psbt").get_str();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file{directory.filePath("foreign.psbt")};
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(psbt));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        auto* import_path = qmltest::Find(m_app, "psbtImportPathField");
        QVERIFY(import_path && import_path->setProperty("text", file.fileName()));
        QVERIFY(qmltest::Click(m_app, "sendOptionsButton"));
        QVERIFY(qmltest::Click(m_app, "sendImportPsbtFromFileButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QTRY_VERIFY(qmltest::Find(m_app, "transactionReviewPopup")->property("opened").toBool());
        auto* warning = qmltest::WaitForVisible(m_app, "sendTransactionReviewWarning");
        QVERIFY(warning);
        QCOMPARE(warning->property("message").toString(), QStringLiteral("This wallet does not have the keys to sign this transaction."));
        QVERIFY(!qmltest::Find(m_app, "sendTransactionReviewSendButton")->property("visible").toBool());
        QVERIFY(qmltest::Find(m_app, "sendTransactionReviewSaveButton")->property("visible").toBool());
        QVERIFY(reviewer->currentTransaction());
        QVERIFY(reviewer->currentTransactionIsImportedPsbt());
        QVERIFY(!reviewer->currentTransactionCanSend());
        QVERIFY(!reviewer->currentTransactionCanBroadcast());
        QVERIFY(!AwaitCommittedSend(*reviewer));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewCloseButton"));
        QTRY_VERIFY(!qmltest::Find(m_app, "transactionReviewPopup")->property("visible").toBool());
        QVERIFY(qmltest::WaitForVisible(m_app, "sendOptionsButton"));
        QCOMPARE(qmltest::Find(m_app, "sendAddressInput")->property("text").toString(), QString{});
        QCOMPARE(qmltest::Find(m_app, "sendAmountInput")->property("text").toString(), QString{});
    }

    // #976: fee-bump preparation and commit run off the GUI thread. While
    // another Core thread holds the wallet lock, Prepare and Update return at
    // once and stay pending, a repeated Update is refused, and exactly one
    // replacement is committed once the lock is released.
    void feeBumpPreparationAndCommitStayOffTheGuiThread()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "off-gui-bump"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        auto* recipient = wallet->sendRecipientList()->currentRecipient();
        recipient->setAddress(qmltest::NewAddress(m_app, wallet->name()));
        recipient->amount()->setSatoshi(COIN);
        wallet->setCustomFeeEnabled(true);
        wallet->setCustomFeeRate("1");
        QVERIFY2(AwaitPreparation(*wallet), qPrintable(wallet->transactionError()));
        QVERIFY2(AwaitCommittedSend(*wallet), qPrintable(wallet->transactionError()));
        const auto txid = wallet->currentTransaction()->txid();
        auto* bump = qobject_cast<BumpTransactionModel*>(wallet->feeBumps()->attemptFor({})); QVERIFY(bump);
        const auto progressesWhileHeld = [this](const HeldWalletLock& lock) {
            bool turned{false};
            QMetaObject::invokeMethod(this, [&turned] { turned = true; }, Qt::QueuedConnection);
            return QTest::qWaitFor([&turned] { return turned; }, 120'000) && !lock.expired();
        };
        {
            HeldWalletLock lock{m_app, wallet->name()};
            QVERIFY(bump->requestPrepareFeeBump(txid, 2));
            QVERIFY2(!lock.expired(), "Fee-bump preparation waited for the held wallet lock");
            QVERIFY(progressesWhileHeld(lock));
            QCOMPARE(bump->state(), BumpTransactionModel::Preparing);
        }
        QVERIFY(QTest::qWaitFor([&] { return !bump->busy(); }, 5'000));
        QCOMPARE(bump->state(), BumpTransactionModel::NeedsConfirmation);
        {
            HeldWalletLock lock{m_app, wallet->name()};
            QVERIFY(bump->requestConfirmFeeBump());
            QVERIFY2(!lock.expired(), "Fee-bump commit waited for the held wallet lock");
            QCOMPARE(bump->requestConfirmFeeBump(), quint64{0});
            QVERIFY(progressesWhileHeld(lock));
            QCOMPARE(bump->state(), BumpTransactionModel::Committing);
            QCOMPARE(qmltest::Rpc(m_app, "getrawmempool")[0].get_str(), txid.toStdString());
        }
        QVERIFY(QTest::qWaitFor([&] { return !bump->busy(); }, 5'000));
        QCOMPARE(bump->state(), BumpTransactionModel::Succeeded);
        const auto pool = qmltest::Rpc(m_app, "getrawmempool");
        QCOMPARE(pool.size(), 1U);
        QCOMPARE(pool[0].get_str(), bump->newTxid().toStdString());
    }

    void feeReplacementRequiresConfirmation()
    {
        auto* wallet = qmltest::LoadFundedWallet(m_app);
        QVERIFY(wallet);
        auto* recipient = wallet->sendRecipientList()->currentRecipient();
        recipient->setAddress(qmltest::NewAddress(m_app, wallet->name()));
        recipient->amount()->setSatoshi(COIN);
        wallet->setCustomFeeEnabled(true);
        wallet->setCustomFeeRate("1");
        QVERIFY2(AwaitPreparation(*wallet), qPrintable(wallet->transactionError()));
        QVERIFY2(AwaitCommittedSend(*wallet), qPrintable(wallet->transactionError()));
        const auto txid = wallet->currentTransaction()->txid();
        auto* bump = qobject_cast<BumpTransactionModel*>(wallet->feeBumps()->attemptFor({}));
        QVERIFY(AwaitBumpPreparation(*bump, txid, 2));
        QCOMPARE(bump->state(), BumpTransactionModel::NeedsConfirmation);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool")[0].get_str(), txid.toStdString());
        QVERIFY2(AwaitBumpCommit(*bump), qPrintable(bump->errorText()));
        QCOMPARE(bump->state(), BumpTransactionModel::Succeeded);
        QVERIFY(bump->newTxid() != txid);
        const auto pool = qmltest::Rpc(m_app, "getrawmempool");
        QCOMPARE(pool.size(), 1U);
        QCOMPARE(pool[0].get_str(), bump->newTxid().toStdString());
    }
};

QString PsbtPreparedWallet(const QString& selector)
{
    const auto function = selector.section(':', 0, 0);
    if (function == "failedImportPreservesTheDraftAndAllowsRetry") return "signer";
    if (function == "importSignBroadcastAndRecognizeKnownTransaction") return "psbt";
    if (function == "foreignPsbtCanBeReviewedWithoutSigning") return "source";
    if (function == "feeReplacementRequiresConfirmation") return "integration";
    return {};
}

std::unique_ptr<QObject> CreatePsbtTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<PsbtIntegrationTests>(app);
}

#include <test_psbt.moc>
