// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletsend.h>
#include <qml/retainedvalue.h>
#include <qml/backendexecutor.h>
#include <qml/snapshotquery.h>
#include <qml/psbtwork.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/mockwallet.h>
#include <qml/psbtsignatures.h>
#include <test/mocks/psbtfeefixture.h>
#include <test/qt_test_registry.h>

#include <chainparams.h>
#include <script/sign.h>
#include <QSemaphore>
#include <QScopeGuard>
#include <QtTest/QtTest>
#include <algorithm>
#include <array>
#include <mutex>
#include <string_view>
#include <vector>

namespace {
using walletsend::Error;
using execution::Lane;
CTransactionRef Transaction()
{
    CMutableTransaction transaction; transaction.vin.resize(1); transaction.vout.emplace_back(1000, CScript{} << OP_TRUE);
    return MakeTransactionRef(std::move(transaction));
}
walletsend::Request SignedRequest()
{
    walletsend::Request request; request.attempt_id = 1; request.review_id = 1; request.reviewed = Transaction(); return request;
}
walletsend::Request PsbtRequest()
{
    CMutableTransaction funding; funding.vin.resize(1); funding.vout.emplace_back(2000, CScript{} << OP_DROP << OP_TRUE);
    const auto previous = MakeTransactionRef(std::move(funding));
    CMutableTransaction transaction{*Transaction()}; transaction.vin[0].prevout = COutPoint{previous->GetHash(), 0};
    PartiallySignedTransaction psbt{transaction, 2}; psbt.inputs[0].non_witness_utxo = previous;
    walletsend::Request request; request.attempt_id = 1; request.review_id = 1; request.reviewed = MakeTransactionRef(transaction);
    request.psbt = std::make_shared<const PartiallySignedTransaction>(psbt);
    request.proof = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(psbt)); request.approved_fee = 1000;
    return request;
}
struct Wallet : StubWallet {
    bool locked{true}, encrypted{false}, no_keys{false}, signer{false};
    bool report_sign_error{false}, mutate_then_throw{false}, change_transaction{false}, throw_sign{false}, fail_lock{false}, throw_commit{false}, partial_sign{false};
    int commits{0}, signs{0}, unlocks{0}, locks{0}, labels{0}, status_reads{0};
    CTransactionRef last_transaction;
    std::shared_ptr<const PartiallySignedTransaction> completed_document;
    std::function<void()> after_unlock,before_commit,before_sign;
    std::function<void(PartiallySignedTransaction&, bool&)> metadata;
    std::function<std::optional<common::PSBTError>(PartiallySignedTransaction&, bool&)> signing_response;
    std::optional<common::PSBTError> metadata_error;
    std::vector<std::pair<bool, bool>> fill_options;
    int metadata_reads{0};
    std::vector<bool> metadata_derivations;
    std::string expected_password{"secret"};std::atomic<QThread*> unlock_thread{nullptr}, sign_thread{nullptr};
    int depth{0}; bool in_mempool{true}, abandoned{false}, missing{false}, mismatch{false}, throw_status{false};
    bool isCrypted() override { return encrypted; }
    bool isLocked() override { return locked; }
    bool privateKeysDisabled() override { return no_keys; }
    bool hasExternalSigner() override { return signer; }
    util::Expected<void, wallet::WalletError> unlock(const SecureString& secret) override {
        ++unlocks;unlock_thread=QThread::currentThread();
        if (std::string_view{secret.data(),secret.size()} != std::string_view{expected_password}) return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::PassphraseIncorrect, Untranslated("wrong")}};
        locked = false;if(after_unlock)after_unlock();return {};
    }
    bool lock() override { ++locks; if (fail_lock) return false; locked = true; return true; }
    std::optional<common::PSBTError> fillPSBT(const common::PSBTFillOptions& options, size_t* signed_inputs, PartiallySignedTransaction& psbt, bool& complete) override {
        sign_thread = QThread::currentThread();
        if (signed_inputs) *signed_inputs = partial_sign ? 0 : 1;
        fill_options.emplace_back(options.sign, options.bip32_derivs);
        if (!options.sign && metadata) {
            ++metadata_reads;
            metadata_derivations.push_back(options.bip32_derivs);
            metadata(psbt, complete);
            return metadata_error;
        }
        if (before_sign) before_sign();
        ++signs;
        if (signing_response) return signing_response(psbt, complete);
        if (report_sign_error || mutate_then_throw) {
            psbt.inputs[0].final_script_sig = CScript{} << OP_TRUE;
            if (mutate_then_throw) throw std::runtime_error{"unknown signer result after partial output"};
            return common::PSBTError::EXTERNAL_SIGNER_FAILED;
        }
        if (throw_sign) throw std::runtime_error{"signer failure"};
        if (partial_sign) { complete = false; return {}; }
        if (completed_document) {
            psbt = *completed_document;
            if (change_transaction) psbt.outputs[0].amount = 999;
            complete = true; return {};
        }
        if (change_transaction) psbt.outputs[0].amount = 999;
        psbt.inputs[0].final_script_sig = CScript{} << OP_0; complete = true; return {};
    }
    void commitTransaction(CTransactionRef transaction, const std::vector<std::string>&) override { if(before_commit)before_commit();++commits; last_transaction = std::move(transaction); if (throw_commit) throw std::runtime_error{"commit uncertainty"}; }
    interfaces::WalletTx getWalletTxDetails(const Txid&, interfaces::WalletTxStatus& status, std::vector<std::string>&, std::vector<std::string>&, bool& mempool, int&) override {
        ++status_reads; if (throw_status) throw std::runtime_error{"status unavailable"};
        status = {}; status.depth_in_main_chain = depth; status.is_abandoned = abandoned; mempool = in_mempool;
        interfaces::WalletTx record{}; if (!missing) record.tx = last_transaction;
        if (mismatch) { CMutableTransaction other{*last_transaction}; ++other.vout[0].nValue; record.tx = MakeTransactionRef(other); }
        return record;
    }
    bool getAddress(const CTxDestination&, std::string*, wallet::AddressPurpose*) override { return true; }
    bool setAddressBook(const CTxDestination&, const std::string&, const std::optional<wallet::AddressPurpose>&) override { ++labels; return true; }
};
walletsigning::Request VerifiedApprovalRequest(Wallet& wallet)
{
    wallet.completed_document = std::make_shared<const PartiallySignedTransaction>(psbtfeefixture::Legacy(2, false));
    auto unsigned_document = *wallet.completed_document;
    for (auto& input : unsigned_document.inputs) {
        input.final_script_sig.clear(); input.final_script_witness.SetNull();
    }
    return {MakeTransactionRef(*unsigned_document.GetUnsignedTx()),
        std::make_shared<const PartiallySignedTransaction>(std::move(unsigned_document)),
        std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(*wallet.completed_document)),
        psbtfeefixture::ACTUAL_FEE};
}
struct Fixture {
    ManualRoutedExecution commands, retirement;
    std::shared_ptr<Wallet> wallet{std::make_shared<Wallet>()};
    std::shared_ptr<walletopening::Session> opening{std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler, std::make_shared<std::atomic_bool>(false), [] {})};
    std::shared_ptr<walletrequests::Store> notes{std::make_shared<walletrequests::Store>(opening, [] {})};
    int notifications{0};
    explicit Fixture(std::shared_ptr<Wallet> original = std::make_shared<Wallet>()) : wallet(std::move(original)) { opening->start(wallet); commands.drain(); opening->take(); }
    ~Fixture() { notes->close(); opening->retire(); commands.drain(); retirement.drain(); }
    std::shared_ptr<walletsend::Operation> operation() { return std::make_shared<walletsend::Operation>(opening, notes, [this](auto) { ++notifications; }); }
};
}

class WalletSendTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { SelectParams(ChainType::MAIN); }
    void replacementSignerMustServiceTheReviewedKeys_data()
    {
        QTest::addColumn<int>("replacement");
        QTest::addColumn<bool>("prepared");
        for (bool prepared : {false, true}) {
            const auto prefix = prepared ? "prepared-" : "imported-";
            QTest::newRow(qPrintable(QString::fromLatin1(prefix) + "compatible-keys-control")) << 0 << prepared;
            QTest::newRow(qPrintable(QString::fromLatin1(prefix) + "incompatible-keys-same-terms")) << 1 << prepared;
            QTest::newRow(qPrintable(QString::fromLatin1(prefix) + "invalid-final-script-same-terms")) << 2 << prepared;
        }
    }
    void replacementSignerMustServiceTheReviewedKeys()
    {
        QFETCH(int, replacement);
        QFETCH(bool, prepared);
        ECC_Context ecc;
        const auto original = psbtfeefixture::Legacy(2, false);
        auto returned = original;
        if (replacement == 1) {
            const auto other_keys = psbtfeefixture::Legacy(2, false);
            for (size_t i = 0; i < returned.inputs.size(); ++i) {
                returned.inputs[i].final_script_sig = other_keys.inputs[i].final_script_sig;
            }
        } else if (replacement == 2) {
            for (auto& input : returned.inputs) input.final_script_sig = CScript{} << OP_TRUE;
        }
        auto unsigned_document = original;
        for (auto& input : unsigned_document.inputs) {
            input.final_script_sig.clear(); input.final_script_witness.SetNull();
        }
        const auto original_tx = original.GetUnsignedTx(); QVERIFY(original_tx);
        const auto returned_tx = returned.GetUnsignedTx(); QVERIFY(returned_tx);
        QCOMPARE(returned_tx->GetHash(), original_tx->GetHash());
        const auto proof = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(original));
        QVERIFY(psbtfee::MatchesReview(returned, *proof, psbtfeefixture::ACTUAL_FEE));
        const auto txdata = PrecomputePSBTData(returned); QVERIFY(txdata);
        bool signatures_valid{true};
        for (size_t i = 0; i < returned.inputs.size(); ++i) {
            signatures_valid &= PSBTInputSignedAndVerified(returned, static_cast<unsigned int>(i), &*txdata);
        }
        QCOMPARE(signatures_valid, replacement == 0);
        struct ReplacementWallet : Wallet {
            std::vector<interfaces::WalletTxOut> known;
            std::map<Txid, CTransactionRef> previous;
            CTransactionRef getTx(const Txid& id) override { const auto found = previous.find(id); return found == previous.end() ? CTransactionRef{} : found->second; }
            size_t input_reads{0};
            std::vector<interfaces::WalletTxOut> getCoins(const std::vector<COutPoint>&) override {
                ++input_reads; return known;
            }
        } signer;
        signer.signer = true;
        for (const auto& input : original.inputs) {
            interfaces::WalletTxOut coin{};
            coin.txout = input.non_witness_utxo->vout[input.GetOutPoint().n];
            signer.known.push_back(std::move(coin));
            signer.previous.emplace(input.GetOutPoint().hash, input.non_witness_utxo);
        }
        // Controlled public-wallet return, independent of physical hardware.
        // A completion flag and unchanged terms cannot replace verification.
        signer.metadata = [&returned](PartiallySignedTransaction& document, bool& complete) {
            document = returned; complete = true;
        };
        const auto authority = prepared
            ? std::make_shared<const psbtfee::Analysis>(psbtfee::Prepared(CTransaction{*original_tx}, psbtfeefixture::ACTUAL_FEE))
            : proof;
        walletsigning::Request request{MakeTransactionRef(*original_tx),
            std::make_shared<const PartiallySignedTransaction>(unsigned_document), authority,
            psbtfeefixture::ACTUAL_FEE};
        const auto result = walletsigning::Approve(signer, request);
        QCOMPARE(signer.commits, 0); QCOMPARE(signer.input_reads, prepared ? size_t{1} : size_t{0});
        if (replacement == 0) {
            QCOMPARE(result.error, walletsigning::Error::None);
            QVERIFY(result.signed_transaction);
            auto finalized = original; CMutableTransaction extracted;
            QVERIFY(FinalizeAndExtractPSBT(finalized, extracted));
            QCOMPARE(result.signed_transaction->GetHash(), MakeTransactionRef(extracted)->GetHash());
        } else {
            QCOMPARE(result.error, walletsigning::Error::InvalidSignature);
            QVERIFY(!result.signed_transaction); QVERIFY(!result.partial);
            QVERIFY(result.document);
            QCOMPARE(result.document->GetUnsignedTx()->GetHash(), original_tx->GetHash());
        }
    }
    void signerSignaturesMustCommitToEveryOutput_data()
    {
        QTest::addColumn<int>("sighash");
        QTest::addColumn<bool>("finalized");
        QTest::addColumn<bool>("accepted");
        for (const bool finalized : {true, false}) {
            const QString shape = finalized ? QStringLiteral("finalized-") : QStringLiteral("partial-");
            QTest::newRow(qPrintable(shape + "all-control")) << int(SIGHASH_ALL) << finalized << true;
            QTest::newRow(qPrintable(shape + "none")) << int(SIGHASH_NONE) << finalized << false;
            QTest::newRow(qPrintable(shape + "single")) << int(SIGHASH_SINGLE) << finalized << false;
            QTest::newRow(qPrintable(shape + "all-anyonecanpay")) << int(SIGHASH_ALL | SIGHASH_ANYONECANPAY) << finalized << false;
        }
    }
    void signerSignaturesMustCommitToEveryOutput()
    {
        // #1122: a signature that does not commit to every output leaves the
        // reviewed recipients replaceable, even though it verifies.
        QFETCH(int, sighash);
        QFETCH(bool, finalized);
        QFETCH(bool, accepted);
        ECC_Context ecc;
        const auto returned = psbtfeefixture::Legacy(2, false, /*with_change=*/true, sighash, finalized);
        auto unsigned_document = returned;
        for (auto& input : unsigned_document.inputs) {
            input.final_script_sig.clear(); input.final_script_witness.SetNull(); input.partial_sigs.clear();
        }
        const auto original_tx = unsigned_document.GetUnsignedTx(); QVERIFY(original_tx);
        Wallet signer; signer.signer = true;
        signer.metadata = [&returned](PartiallySignedTransaction& document, bool& complete) { document = returned; complete = true; };
        const walletsigning::Request request{MakeTransactionRef(*original_tx),
            std::make_shared<const PartiallySignedTransaction>(unsigned_document),
            std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(returned)), psbtfeefixture::ACTUAL_FEE};
        const auto result = walletsigning::Approve(signer, request);
        QCOMPARE(signer.commits, 0);
        QCOMPARE(psbtsignatures::CommitsToAllOutputs(returned), accepted);
        if (accepted) {
            QCOMPARE(result.error, walletsigning::Error::None); QVERIFY(result.signed_transaction);
        } else {
            QCOMPARE(result.error, walletsigning::Error::UncommittedOutputs);
            QVERIFY(!result.signed_transaction); QVERIFY(!result.partial); QVERIFY(!result.quarantined_document);
            QVERIFY(result.document); QCOMPARE(result.document->GetUnsignedTx()->GetHash(), original_tx->GetHash());
        }
    }
    void directExternalCommitRequiresTheOriginalKeys_data()
    {
        QTest::addColumn<int>("candidate");
        QTest::newRow("compatible-signed-candidate") << 0;
        QTest::newRow("unsigned-after-failed-approval") << 1;
        QTest::newRow("incompatible-key-candidate") << 2;
    }
    void directExternalCommitRequiresTheOriginalKeys()
    {
        QFETCH(int, candidate);
        ECC_Context ecc;
        const auto original = psbtfeefixture::Legacy(2, false);
        const auto signatures = candidate == 2 ? psbtfeefixture::Legacy(2, false) : original;
        CMutableTransaction reviewed = *original.GetUnsignedTx();
        if (candidate != 1) for (size_t i = 0; i < reviewed.vin.size(); ++i) {
            reviewed.vin[i].scriptSig = signatures.inputs[i].final_script_sig;
            reviewed.vin[i].scriptWitness = signatures.inputs[i].final_script_witness;
        }
        struct ExternalWallet : Wallet {
            std::vector<interfaces::WalletTxOut> known;
            std::map<Txid, CTransactionRef> previous;
            CTransactionRef getTx(const Txid& id) override { const auto found = previous.find(id); return found == previous.end() ? CTransactionRef{} : found->second; }
            std::vector<interfaces::WalletTxOut> getCoins(const std::vector<COutPoint>&) override { return known; }
        };
        auto wallet = std::make_shared<ExternalWallet>(); wallet->signer = true; wallet->no_keys = true;
        for (const auto& input : original.inputs) {
            interfaces::WalletTxOut coin{}; coin.txout = input.non_witness_utxo->vout[input.GetOutPoint().n];
            wallet->known.push_back(std::move(coin));
            wallet->previous.emplace(input.GetOutPoint().hash, input.non_witness_utxo);
        }
        Fixture f{wallet}; auto operation = f.operation();
        walletsend::Request request; request.reviewed = MakeTransactionRef(reviewed); request.approved_fee = psbtfeefixture::ACTUAL_FEE;
        QVERIFY(operation->start(std::move(request))); QCOMPARE(wallet->commits, 0);
        f.commands.drain(); QVERIFY(operation->result());
        QCOMPARE(operation->result()->error, candidate == 0 ? Error::None : Error::NeedsSignerApproval);
        QCOMPARE(wallet->commits, candidate == 0 ? 1 : 0);
        QCOMPARE(bool(operation->result()->committed), candidate == 0);
        if (candidate == 0) QCOMPARE(operation->result()->committed->GetHash(), MakeTransactionRef(reviewed)->GetHash());
        QVERIFY(wallet->fill_options.empty()); QCOMPARE(wallet->unlocks, 0);
    }
    void signerErrorCannotRetainInvalidFinalizedSignatures_data()
    {
        QTest::addColumn<int>("replacement");
        QTest::addColumn<bool>("throws");
        for (bool throws : {false, true}) for (int replacement : {0, 1, 2}) {
            const auto name = QStringLiteral("%1-%2").arg(throws ? "unknown" : "error")
                .arg(replacement == 0 ? "compatible-keys" : replacement == 1 ? "incompatible-keys" : "invalid-script");
            QTest::newRow(qPrintable(name)) << replacement << throws;
        }
    }
    void signerErrorCannotRetainInvalidFinalizedSignatures()
    {
        QFETCH(int, replacement);
        QFETCH(bool, throws);
        ECC_Context ecc;
        struct ErrorSigner : Wallet {
            PartiallySignedTransaction returned;
            bool throws;
            ErrorSigner(PartiallySignedTransaction response, bool uncertain)
                : returned(std::move(response)), throws(uncertain) { signer = true; }
            std::optional<common::PSBTError> fillPSBT(const common::PSBTFillOptions& options, size_t*, PartiallySignedTransaction& document, bool& complete) override
            {
                complete = false;
                if (!options.sign) return {};
                ++signs;
                document = returned;
                if (throws) throw std::runtime_error("signer returned bytes without a confirmed outcome");
                return common::PSBTError::EXTERNAL_SIGNER_FAILED;
            }
        } signer{psbtfeefixture::Legacy(2, false), throws};
        const auto original = signer.returned;
        if (replacement == 1) {
            const auto incompatible = psbtfeefixture::Legacy(2, false);
            for (size_t i = 0; i < signer.returned.inputs.size(); ++i)
                signer.returned.inputs[i].final_script_sig = incompatible.inputs[i].final_script_sig;
        } else if (replacement == 2) {
            for (auto& input : signer.returned.inputs) input.final_script_sig = CScript{} << OP_TRUE;
        }
        const auto txdata = PrecomputePSBTData(signer.returned); QVERIFY(txdata);
        for (size_t i = 0; i < signer.returned.inputs.size(); ++i)
            QCOMPARE(PSBTInputSignedAndVerified(signer.returned, static_cast<unsigned int>(i), &*txdata), replacement == 0);
        auto unsigned_document = original;
        for (auto& input : unsigned_document.inputs) input.final_script_sig.clear();
        const auto captured = psbtwork::SerializeRaw(unsigned_document);
        const walletsigning::Request request{MakeTransactionRef(*original.GetUnsignedTx()),
            std::make_shared<const PartiallySignedTransaction>(unsigned_document),
            std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(original)), psbtfeefixture::ACTUAL_FEE};
        const auto result = walletsigning::Approve(signer, request);
        QCOMPARE(result.error, throws ? walletsigning::Error::Uncertain : walletsigning::Error::SignerFailed);
        QCOMPARE(bool(result.document), replacement == 0);
        QCOMPARE(result.partial, replacement == 0);
        QVERIFY(!result.signed_transaction);
        QCOMPARE(signer.signs, 1); QCOMPARE(signer.commits, 0);
        QCOMPARE(psbtwork::SerializeRaw(*request.document), captured);
        if (result.document) QCOMPARE(psbtwork::SerializeRaw(*result.document), psbtwork::SerializeRaw(original));
    }
    void preparedSignerErrorCannotReplaceAuthenticatedInputKeys_data()
    {
        QTest::addColumn<int>("returned");
        QTest::newRow("valid-partial-output-after-error") << 0;
        QTest::newRow("forged-input-keys-after-error") << 1;
        QTest::newRow("forged-input-keys-after-unknown-outcome") << 2;
    }
    void preparedSignerErrorCannotReplaceAuthenticatedInputKeys()
    {
        QFETCH(int, returned);
        ECC_Context ecc;
        const auto original = psbtfeefixture::Legacy(2, false);
        struct PartialWallet : Wallet {
            PartiallySignedTransaction original;
            int returned;
            int device_calls{0};
            PartialWallet(PartiallySignedTransaction source, int value) : original(std::move(source)), returned(value) { signer = true; }
            CTransactionRef getTx(const Txid& id) override {
                const auto source = original.inputs.front().non_witness_utxo;
                return source->GetHash() == id ? source : CTransactionRef{};
            }
            std::vector<interfaces::WalletTxOut> getCoins(const std::vector<COutPoint>& inputs) override {
                std::vector<interfaces::WalletTxOut> result;
                for (const auto& input : inputs) {
                    interfaces::WalletTxOut coin{}; coin.txout = original.inputs.front().non_witness_utxo->vout[input.n];
                    result.push_back(std::move(coin));
                }
                return result;
            }
            std::optional<common::PSBTError> fillPSBT(const common::PSBTFillOptions& options, size_t*, PartiallySignedTransaction& document, bool& complete) override {
                complete = false;
                if (!options.sign) return {};
                ++device_calls; document = original;
                if (returned) {
                    auto& input = document.inputs.front();
                    input.witness_utxo = input.non_witness_utxo->vout[input.GetOutPoint().n];
                    input.witness_utxo.scriptPubKey = CScript{} << OP_TRUE;
                    input.non_witness_utxo.reset();
                }
                if (returned == 2) throw std::runtime_error("device returned no outcome");
                return common::PSBTError::EXTERNAL_SIGNER_FAILED;
            }
        } signer{original, returned};
        auto unsigned_document = original;
        for (auto& input : unsigned_document.inputs) input.final_script_sig.clear();
        const auto tx = MakeTransactionRef(*original.GetUnsignedTx());
        walletsigning::Request request{tx, std::make_shared<const PartiallySignedTransaction>(unsigned_document),
            std::make_shared<const psbtfee::Analysis>(psbtfee::Prepared(*tx, psbtfeefixture::ACTUAL_FEE)), psbtfeefixture::ACTUAL_FEE};
        const auto result = walletsigning::Approve(signer, request);
        QCOMPARE(result.error, returned == 2 ? walletsigning::Error::Uncertain : walletsigning::Error::SignerFailed);
        QCOMPARE(bool(result.document), returned == 0); QCOMPARE(result.partial, returned == 0);
        QVERIFY(!result.signed_transaction); QCOMPARE(signer.device_calls, 1); QCOMPARE(signer.commits, 0);
        if (result.document) QVERIFY(psbtfee::MatchesReview(*result.document, psbtfee::Analyze(original), psbtfeefixture::ACTUAL_FEE));
    }
    void directPsbtSendRejectsIncompatibleSignatures_data()
    {
        QTest::addColumn<int>("returned");
        QTest::newRow("compatible-keys") << 0;
        QTest::newRow("incompatible-keys-same-terms") << 1;
        QTest::newRow("invalid-final-script-same-terms") << 2;
    }
    void directPsbtSendRejectsIncompatibleSignatures()
    {
        QFETCH(int, returned);
        ECC_Context ecc;
        const auto original = psbtfeefixture::Legacy(2, false);
        struct ReturningWallet : Wallet {
            PartiallySignedTransaction response;
            explicit ReturningWallet(PartiallySignedTransaction source) : response(std::move(source)) { signer = true; no_keys = true; }
            std::optional<common::PSBTError> fillPSBT(const common::PSBTFillOptions& options, size_t*, PartiallySignedTransaction& document, bool& complete) override {
                ++signs; complete = options.sign;
                if (options.sign) document = response;
                return {};
            }
        };
        auto response = original;
        if (returned == 1) {
            const auto other_keys = psbtfeefixture::Legacy(2, false);
            for (size_t i = 0; i < response.inputs.size(); ++i) response.inputs[i].final_script_sig = other_keys.inputs[i].final_script_sig;
        } else if (returned == 2) {
            for (auto& input : response.inputs) input.final_script_sig = CScript{} << OP_TRUE;
        }
        auto wallet = std::make_shared<ReturningWallet>(response); Fixture f{wallet};
        auto unsigned_document = original;
        for (auto& input : unsigned_document.inputs) input.final_script_sig.clear();
        walletsend::Request request; request.reviewed = MakeTransactionRef(*original.GetUnsignedTx());
        request.psbt = std::make_shared<const PartiallySignedTransaction>(unsigned_document);
        request.proof = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(original)); request.approved_fee = psbtfeefixture::ACTUAL_FEE;
        auto operation = f.operation(); QVERIFY(operation->start(std::move(request))); f.commands.drain();
        QVERIFY(operation->result()); QCOMPARE(operation->result()->error, returned == 0 ? Error::None : Error::InvalidSignature);
        QCOMPARE(wallet->commits, returned == 0 ? 1 : 0); QCOMPARE(wallet->signs, 1);
        QCOMPARE(bool(operation->result()->committed), returned == 0);
    }
    void unverifiedPreparedInputsCannotReachTheSigner_data()
    {
        QTest::addColumn<int>("failure");
        QTest::newRow("missing-public-outputs") << 0;
        QTest::newRow("null-public-output") << 1;
        QTest::newRow("public-read-throws") << 2;
    }
    void unverifiedPreparedInputsCannotReachTheSigner()
    {
        QFETCH(int, failure);
        struct UnavailableWallet : Wallet {
            CTransactionRef previous;
            CTransactionRef getTx(const Txid&) override { return previous; }
            int failure;
            explicit UnavailableWallet(int value) : failure(value) { signer = true; }
            std::vector<interfaces::WalletTxOut> getCoins(const std::vector<COutPoint>&) override {
                if (failure == 2) throw std::runtime_error("public prevout read unavailable");
                return failure == 0 ? std::vector<interfaces::WalletTxOut>{} : std::vector<interfaces::WalletTxOut>(1);
            }
        } signer{failure};
        const auto send = PsbtRequest();
        signer.previous = send.psbt->inputs.front().non_witness_utxo;
        walletsigning::Request request{send.reviewed, send.psbt,
            std::make_shared<const psbtfee::Analysis>(psbtfee::Prepared(*send.reviewed, *send.approved_fee)), send.approved_fee};
        const auto result = walletsigning::Approve(signer, request);
        QCOMPARE(result.error, walletsigning::Error::UnverifiedInputs);
        QVERIFY(!result.document && !result.signed_transaction && !result.partial);
        QVERIFY(signer.fill_options.empty());
        QCOMPARE(signer.commits, 0); QCOMPARE(signer.unlocks, 0);
    }
    void signingApprovalRejectsIncompleteAuthorityBeforeDeviceWork_data()
    {
        QTest::addColumn<int>("mode");
        const std::array names{"missing-review", "missing-proof", "missing-fee", "fee-changed", "unsigned-terms-changed", "signer-missing", "missing-output-owner", "unknown-proof-fee"};
        for (size_t i{0}; i < names.size(); ++i) QTest::newRow(names[i]) << int(i);
    }
    void signingApprovalRejectsIncompleteAuthorityBeforeDeviceWork()
    {
        QFETCH(int, mode);
        Fixture f; f.wallet->signer = mode != 5;
        const auto send = PsbtRequest(); const auto original = psbtwork::SerializeRaw(*send.psbt);
        walletsigning::Request request{send.reviewed, send.psbt, send.proof, send.approved_fee};
        if (mode == 0) request.reviewed.reset();
        if (mode == 1) request.proof.reset();
        if (mode == 2) request.approved_fee.reset();
        if (mode == 3) request.approved_fee = 999;
        if (mode == 4) {
            auto changed = *request.document;
            changed.outputs[0].amount = 999;
            request.document = std::make_shared<const PartiallySignedTransaction>(std::move(changed));
        }
        if (mode == 7) {
            auto proof = std::make_shared<psbtfee::Analysis>(*request.proof);
            proof->amount.reset(); proof->state = psbtfee::State::Missing;
            request.proof = std::move(proof);
        }
        const auto result = mode == 6 ? walletsigning::ApproveInto(*f.wallet, request, {})
            : walletsigning::Approve(*f.wallet, request, f.opening);
        const auto expected = mode == 1 || mode == 2 || mode == 7 ? walletsigning::Error::FeeUnavailable
            : mode == 5 ? walletsigning::Error::SignerNotFound : mode == 6 ? walletsigning::Error::Failed : walletsigning::Error::InvalidFee;
        QCOMPARE(result.error, expected);
        QVERIFY(!result.document && !result.signed_transaction && !result.partial);
        QCOMPARE(f.wallet->fill_options.size(), size_t{0});
        QCOMPARE(f.wallet->commits, 0); QCOMPARE(f.wallet->unlocks, 0); QCOMPARE(f.wallet->locks, 0);
        QCOMPARE(psbtwork::SerializeRaw(*send.psbt), original);
    }
    void unavailableFeeOwnsTheOriginalApprovalOutcome_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("missing-proof") << 0;
        QTest::newRow("missing-approved-fee") << 1;
        QTest::newRow("unknown-proof-amount") << 2;
    }
    void unavailableFeeOwnsTheOriginalApprovalOutcome()
    {
        QFETCH(int, mode);
        Fixture f; f.wallet->signer = true;
        const auto send = PsbtRequest(); const auto original = psbtwork::SerializeRaw(*send.psbt);
        auto request = walletopening::RetainValues<walletsigning::Request>(f.opening, [&] {
            return std::make_unique<walletsigning::Request>(send.reviewed, send.psbt, send.proof, send.approved_fee);
        });
        QVERIFY(request);
        if (mode == 0) request->proof.reset();
        if (mode == 1) request->approved_fee.reset();
        if (mode == 2) {
            auto proof = std::make_shared<psbtfee::Analysis>(*request->proof);
            proof->amount.reset(); proof->state = psbtfee::State::Missing;
            request->proof = std::move(proof);
        }
        std::vector<walletsigning::ApprovalSnapshot> snapshots;
        auto owner = std::make_shared<walletsigning::ApprovalOwner>(f.opening, [&snapshots](auto snapshot) { snapshots.push_back(std::move(snapshot)); });
        constexpr uint64_t review{71}, view{4}, attempt{91};
        QVERIFY(owner->reviewChanged(review, view)); f.commands.drain();
        QVERIFY(owner->request(attempt, review, view, std::move(request)));
        QCOMPARE(f.wallet->signs, 0); QVERIFY(f.wallet->fill_options.empty());
        f.commands.drain();
        const auto original_outcome = snapshots.back();
        QCOMPARE(original_outcome.error, walletsigning::Error::FeeUnavailable);
        QCOMPARE(original_outcome.phase, walletsigning::ApprovalPhase::Failed);
        QCOMPARE(original_outcome.attempt_id, attempt); QCOMPARE(original_outcome.attempt_review, review); QCOMPARE(original_outcome.attempt_view, view);
        QVERIFY(original_outcome.publishable); QVERIFY(!original_outcome.in_flight);
        QVERIFY(owner->reviewChanged(review + 1, view + 1)); f.commands.drain();
        QCOMPARE(snapshots.back().current_review, review + 1); QCOMPARE(snapshots.back().current_view, view + 1);
        QCOMPARE(original_outcome.error, walletsigning::Error::FeeUnavailable); QCOMPARE(original_outcome.attempt_review, review);
        QCOMPARE(f.wallet->signs, 0); QCOMPARE(f.wallet->commits, 0); QCOMPARE(f.wallet->unlocks, 0); QCOMPARE(f.wallet->locks, 0);
        QVERIFY(f.wallet->fill_options.empty()); QCOMPARE(psbtwork::SerializeRaw(*send.psbt), original);
        QVERIFY(owner->close()); f.commands.drain();
    }
    void approvalCannotStartDeviceWorkWithoutReservedOutput()
    {
        Fixture f; f.wallet->signer = true;
        const auto send = PsbtRequest();
        const walletsigning::Request request{send.reviewed, send.psbt, send.proof, send.approved_fee};
        f.commands.scheduler->close();
        const auto result = walletsigning::Approve(*f.wallet, request, f.opening);
        QCOMPARE(result.error, walletsigning::Error::Failed);
        QVERIFY(!result.document && !result.signed_transaction && !result.lifetime);
        QVERIFY(f.wallet->fill_options.empty()); QCOMPARE(f.wallet->commits, 0);
    }
    void approvalRetainsOnlyAuthenticatedMetadataAfterFailure_data()
    {
        QTest::addColumn<int>("mode");
        const std::array names{"signer-not-found", "signer-failed", "metadata-error", "error-after-terms-changed", "error-after-fee-changed", "throw-after-authenticated-output", "throw-after-terms-changed", "throw-after-fee-changed"};
        for (size_t i{0}; i < names.size(); ++i) QTest::newRow(names[i]) << int(i);
    }
    void approvalCannotInstallInvalidUnfinalizedContributions_data()
    {
        QTest::addColumn<int>("outcome");
        QTest::newRow("normal-return") << 0;
        QTest::newRow("error-return") << 1;
        QTest::newRow("exception-return") << 2;
    }
    void unfinishedSignatureValueVerification_data()
    {
        QTest::addColumn<bool>("nested");
        QTest::addColumn<int>("kind");
        for (bool nested : {false, true}) for (int kind = 0; kind < 5; ++kind)
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(nested ? "nested" : "witness").arg(kind))) << nested << kind;
    }
    void unfinishedSignatureValueVerification()
    {
        QFETCH(bool, nested);
        QFETCH(int, kind);
        ECC_Context ecc;
        const auto fixture = psbtfeefixture::SignedWithoutScripts(nested);
        auto partial = fixture.document;
        partial.inputs[0].witness_script = fixture.witness;
        partial.inputs[0].redeem_script = fixture.redeem;
        partial.inputs[0].partial_sigs.erase(std::next(partial.inputs[0].partial_sigs.begin()));
        if (kind == 1) partial.inputs[0].partial_sigs.begin()->second.second[5] ^= 1;
        if (kind == 2) partial.inputs[0].witness_script.clear();
        if (kind == 3) partial.inputs[0].partial_sigs.begin()->second.second.clear();
        if (kind == 4) partial.inputs[0].m_musig2_partial_sigs[{}][{}] = uint256::ONE;
        const auto bytes = psbtwork::SerializeRaw(partial);
        QCOMPARE(psbtsignatures::VerifyPartialInputs(partial), kind == 0 ? psbtsignatures::Status::Valid
            : kind == 1 || kind == 3 ? psbtsignatures::Status::Invalid : psbtsignatures::Status::Unknown);
        QCOMPARE(psbtwork::SerializeRaw(partial), bytes);
        if (kind == 0) {
            CMutableTransaction extracted;
            QVERIFY(!FinalizeAndExtractPSBT(partial, extracted));
        }
    }
    void unfinishedLegacySignatureValueVerification_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("valid") << 0;
        QTest::newRow("corrupt") << 1;
        QTest::newRow("missing-prevout") << 2;
    }
    void unfinishedLegacySignatureValueVerification()
    {
        QFETCH(int, kind);
        ECC_Context ecc;
        auto document = psbtfeefixture::Legacy(2, false);
        auto completed = document;
        CMutableTransaction transaction;
        QVERIFY(FinalizeAndExtractPSBT(completed, transaction));
        for (size_t i = 0; i < document.inputs.size(); ++i) {
            auto& input = document.inputs[i];
            const auto data = DataFromTransaction(transaction, i, input.non_witness_utxo->vout[input.GetOutPoint().n]);
            input.final_script_sig.clear();
            input.partial_sigs = data.signatures;
            QVERIFY(!input.partial_sigs.empty());
        }
        if (kind == 1) document.inputs[0].partial_sigs.begin()->second.second[5] ^= 1;
        if (kind == 2) document.inputs[0].non_witness_utxo.reset();
        QCOMPARE(psbtsignatures::VerifyPartialInputs(document), kind == 0 ? psbtsignatures::Status::Valid
            : kind == 1 ? psbtsignatures::Status::Invalid : psbtsignatures::Status::Unknown);
    }
    void unfinishedTaprootSignatureValueVerification_data()
    {
        QTest::addColumn<bool>("script_path");
        QTest::addColumn<int>("kind");
        for (bool script_path : {false, true}) for (int kind = 0; kind < 3; ++kind)
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(script_path ? "script" : "key").arg(kind))) << script_path << kind;
        QTest::newRow("script-unused-pushed-key") << true << 3;
    }
    void unfinishedTaprootSignatureValueVerification()
    {
        QFETCH(bool, script_path);
        QFETCH(int, kind);
        ECC_Context ecc;
        CKey key;
        key.MakeNewKey(true);
        const XOnlyPubKey internal{key.GetPubKey()};
        TaprootBuilder builder;
        CScript script = CScript{} << std::vector<unsigned char>{internal.begin(), internal.end()} << (kind == 3 ? OP_DROP : OP_CHECKSIG);
        if (kind == 3) script << OP_TRUE;
        if (script_path) builder.Add(0, script, TAPROOT_LEAF_TAPSCRIPT);
        builder.Finalize(internal);
        CMutableTransaction parent;
        parent.vin.resize(1);
        parent.vout.emplace_back(2000, GetScriptForDestination(builder.GetOutput()));
        const auto previous = MakeTransactionRef(parent);
        CMutableTransaction transaction;
        transaction.vin.emplace_back(COutPoint{previous->GetHash(), 0});
        transaction.vout.emplace_back(1500, CScript{} << OP_TRUE);
        PartiallySignedTransaction document{transaction, 2};
        document.inputs[0].non_witness_utxo = previous;
        document.inputs[0].m_tap_scripts = builder.GetSpendData().scripts;
        const auto data = PrecomputePSBTData(document);
        QVERIFY(data);
        FillableSigningProvider provider;
        QVERIFY(provider.AddKey(key));
        MutableTransactionSignatureCreator creator{transaction, 0, 2000, &*data, {.sighash_type = SIGHASH_DEFAULT}};
        const auto leaf = ComputeTapleafHash(TAPROOT_LEAF_TAPSCRIPT, script);
        const uint256 no_script_root;
        std::vector<unsigned char> signature;
        QVERIFY(creator.CreateSchnorrSig(provider, signature, internal, script_path ? &leaf : nullptr,
            script_path ? nullptr : &no_script_root, script_path ? SigVersion::TAPSCRIPT : SigVersion::TAPROOT));
        if (kind == 1) signature[5] ^= 1;
        if (script_path) document.inputs[0].m_tap_script_sigs[{internal, leaf}] = signature;
        else document.inputs[0].m_tap_key_sig = signature;
        if (kind == 2) {
            if (script_path) document.inputs[0].m_tap_scripts.clear();
            else document.inputs[0].non_witness_utxo.reset();
        }
        QCOMPARE(psbtsignatures::VerifyPartialInputs(document), kind == 0 ? psbtsignatures::Status::Valid
            : kind == 1 ? psbtsignatures::Status::Invalid : psbtsignatures::Status::Unknown);
    }
    void mathematicallyValidUnrelatedKeyDoesNotVerifyPolicyContribution()
    {
        ECC_Context ecc;
        const auto fixture = psbtfeefixture::SignedWithoutScripts(false);
        auto document = fixture.document;
        document.inputs[0].partial_sigs.clear();
        document.inputs[0].witness_script = fixture.witness;
        CKey unrelated;
        unrelated.MakeNewKey(true);
        FillableSigningProvider provider;
        QVERIFY(provider.AddKey(unrelated));
        const auto transaction = document.GetUnsignedTx();
        const auto data = PrecomputePSBTData(document);
        QVERIFY(transaction); QVERIFY(data);
        MutableTransactionSignatureCreator creator{*transaction, 0, 2000, &*data, {.sighash_type = SIGHASH_ALL}};
        std::vector<unsigned char> signature;
        QVERIFY(creator.CreateSig(provider, signature, unrelated.GetPubKey().GetID(), fixture.witness, SigVersion::WITNESS_V0));
        const auto key = unrelated.GetPubKey();
        MutableTransactionSignatureChecker checker{&*transaction, 0, 2000, *data, MissingDataBehavior::FAIL};
        QVERIFY(checker.CheckECDSASignature(signature, {key.begin(), key.end()}, fixture.witness, SigVersion::WITNESS_V0));
        document.inputs[0].partial_sigs[key.GetID()] = {key, signature};
        QCOMPARE(psbtsignatures::VerifyPartialInputs(document), psbtsignatures::Status::Invalid);
    }
    void approvalQuarantinesMissingSignatureContext_data()
    {
        QTest::addColumn<bool>("nested");
        QTest::newRow("witness") << false;
        QTest::newRow("nested") << true;
    }
    void approvalQuarantinesMissingSignatureContext()
    {
        QFETCH(bool, nested);
        ECC_Context ecc;
        const auto fixture = psbtfeefixture::SignedWithoutScripts(nested);
        auto original = fixture.document;
        original.inputs[0].partial_sigs.clear();
        auto returned = fixture.document;
        returned.inputs[0].partial_sigs.erase(std::next(returned.inputs[0].partial_sigs.begin()));
        Fixture f;
        f.wallet->signer = true;
        f.wallet->metadata = [](PartiallySignedTransaction&, bool& complete) { complete = false; };
        f.wallet->completed_document = std::make_shared<const PartiallySignedTransaction>(returned);
        const walletsigning::Request request{MakeTransactionRef(*original.GetUnsignedTx()),
            std::make_shared<const PartiallySignedTransaction>(original),
            std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(original)), 500};
        const auto result = walletsigning::Approve(*f.wallet, request, f.opening);
        QCOMPARE(result.error, walletsigning::Error::UnverifiedSignatures);
        QCOMPARE(result.signature_status, psbtsignatures::Status::Unknown);
        QVERIFY(!result.partial);
        QVERIFY(!result.signed_transaction);
        QVERIFY(result.document);
        QCOMPARE(psbtwork::SerializeRaw(*result.document), psbtwork::SerializeRaw(original));
        QVERIFY(result.quarantined_document);
        QCOMPARE(psbtwork::SerializeRaw(*result.quarantined_document), psbtwork::SerializeRaw(returned));
        auto reconciled = *result.quarantined_document;
        reconciled.inputs[0].witness_script = fixture.witness;
        reconciled.inputs[0].redeem_script = fixture.redeem;
        reconciled.inputs[0].partial_sigs.insert(*std::next(fixture.document.inputs[0].partial_sigs.begin()));
        QVERIFY(FinalizePSBT(reconciled));
        QVERIFY(psbtsignatures::VerifyFinalizedInputs(reconciled, true));
        QCOMPARE(f.wallet->commits, 0);
    }
    void approvalCannotInstallInvalidUnfinalizedContributions()
    {
        QFETCH(int, outcome);
        ECC_Context ecc;
        const auto fixture = psbtfeefixture::SignedWithoutScripts(false);
        auto original = fixture.document;
        original.inputs[0].witness_script = fixture.witness;
        original.inputs[0].partial_sigs.clear();
        auto returned = original;
        returned.inputs[0].partial_sigs.insert(*fixture.document.inputs[0].partial_sigs.begin());
        returned.inputs[0].partial_sigs.begin()->second.second[5] ^= 1;
        Fixture f;
        f.wallet->signer = true;
        f.wallet->metadata = [returned, outcome](PartiallySignedTransaction& document, bool& complete) {
            document = returned;
            complete = false;
            if (outcome == 2) throw std::runtime_error("device failed after returning a partial signature");
        };
        if (outcome == 0) {
            f.wallet->metadata = [original](PartiallySignedTransaction& document, bool& complete) {
                document = original;
                complete = false;
            };
            f.wallet->completed_document = std::make_shared<const PartiallySignedTransaction>(returned);
        }
        if (outcome == 1) f.wallet->metadata_error = common::PSBTError::EXTERNAL_SIGNER_FAILED;
        const walletsigning::Request request{MakeTransactionRef(*original.GetUnsignedTx()),
            std::make_shared<const PartiallySignedTransaction>(original),
            std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(original)), 500};
        const auto result = walletsigning::Approve(*f.wallet, request, f.opening);
        QVERIFY(!result.signed_transaction);
        QCOMPARE(f.wallet->commits, 0);
        // No failed or uncertain device response may install a corrupted
        // unfinished signature into the original recoverable review.
        QVERIFY(!result.document || result.document->inputs[0].partial_sigs.empty());
        QVERIFY(!result.partial);
    }
    void approvalRetainsOnlyAuthenticatedMetadataAfterFailure()
    {
        QFETCH(int, mode);
        Fixture f; f.wallet->signer = true;
        const auto send = PsbtRequest(); const auto original = psbtwork::SerializeRaw(*send.psbt);
        const walletsigning::Request request{send.reviewed, send.psbt, send.proof, send.approved_fee};
        f.wallet->metadata_error = mode == 0 ? common::PSBTError::EXTERNAL_SIGNER_NOT_FOUND
            : mode == 1 ? common::PSBTError::EXTERNAL_SIGNER_FAILED : common::PSBTError::INVALID_TX;
        f.wallet->metadata = [mode](PartiallySignedTransaction& document, bool& complete) {
            document.inputs[0].final_script_sig = CScript{} << OP_TRUE;
            complete = true;
            if (mode == 3 || mode == 6) document.outputs[0].amount = 999;
            if (mode == 4 || mode == 7) {
                auto previous = CMutableTransaction{*document.inputs[0].non_witness_utxo};
                previous.vout[0].nValue = 1'999;
                document.inputs[0].non_witness_utxo = MakeTransactionRef(std::move(previous));
            }
            if (mode >= 5) throw std::runtime_error("metadata effect returned no completion");
        };
        const auto result = walletsigning::Approve(*f.wallet, request, f.opening);
        QCOMPARE(result.error, mode >= 5 ? walletsigning::Error::Uncertain : mode == 0 ? walletsigning::Error::SignerNotFound
            : mode == 1 ? walletsigning::Error::SignerFailed : walletsigning::Error::Fill);
        const bool retained = mode <= 2 || mode == 5;
        QCOMPARE(bool(result.document), retained); QCOMPARE(result.partial, retained);
        QVERIFY(!result.signed_transaction); QCOMPARE(f.wallet->commits, 0);
        QCOMPARE(f.wallet->metadata_reads, 1); QCOMPARE(f.wallet->signs, 0);
        QCOMPARE(f.wallet->fill_options.size(), size_t{1});
        QCOMPARE(f.wallet->fill_options.front(), (std::pair{false, true}));
        QCOMPARE(result.fill_error, mode >= 5 ? std::optional<common::PSBTError>{} : f.wallet->metadata_error);
        QCOMPARE(psbtwork::SerializeRaw(*send.psbt), original);
        if (retained) {
            QCOMPARE(result.document->inputs[0].final_script_sig, CScript{} << OP_TRUE);
            QCOMPARE(result.document->GetUnsignedTx()->GetHash(), send.reviewed->GetHash());
            QVERIFY(psbtfee::MatchesReview(*result.document, *send.proof, send.approved_fee));
            QVERIFY(result.lifetime);
        }
    }
    void committedTransactionRemainsTruthfulWhenTheNotesStoreIsAbsent()
    {
        Fixture f;
        auto operation = std::make_shared<walletsend::Operation>(f.opening, nullptr, [&f](auto) { ++f.notifications; });
        auto request = SignedRequest(); const auto reviewed = request.reviewed->GetHash();
        request.notes.insert("1BoatSLRHtKNngkdXEeobR76b53LETtpyT", "outgoing note");
        QVERIFY(operation->start(std::move(request))); QCOMPARE(f.wallet->commits, 0);
        f.commands.drain(); const auto result = operation->result(); QVERIFY(result);
        QCOMPARE(result->error, Error::None); QVERIFY(result->committed);
        QCOMPARE(result->committed->GetHash(), reviewed);
        QCOMPARE(result->submission, walletsend::Submission::Mempool);
        QVERIFY(result->notes_requested && !result->notes);
        QCOMPARE(result->notes_rejection, execution::Rejection::Closed);
        QCOMPARE(f.wallet->commits, 1); QCOMPARE(f.wallet->labels, 0); QCOMPARE(f.notifications, 1);
    }
    void invalidReviewOrForeignActionCannotReachEitherWallet_data()
    {
        QTest::addColumn<bool>("foreign");
        QTest::newRow("missing-review") << false;
        QTest::newRow("foreign-opening-capability") << true;
    }
    void invalidReviewOrForeignActionCannotReachEitherWallet()
    {
        QFETCH(bool, foreign);
        Fixture f, other;
        auto operation = f.operation(); auto request = SignedRequest();
        if (!foreign) request.reviewed.reset();
        const auto admitted = operation->start(std::move(request), foreign ? other.opening->startAction() : nullptr);
        QCOMPARE(admitted.rejection, execution::Rejection::Invalid);
        f.commands.drain(); other.commands.drain();
        QVERIFY(!operation->result()); QCOMPARE(f.notifications, 0);
        QCOMPARE(f.wallet->commits, 0); QCOMPARE(other.wallet->commits, 0);
        QVERIFY(f.wallet->fill_options.empty() && other.wallet->fill_options.empty());
        const auto valid = f.operation(); QVERIFY(valid->start(SignedRequest())); f.commands.drain();
        QVERIFY(valid->result() && valid->result()->committed); QCOMPARE(f.wallet->commits, 1);
        QCOMPARE(other.wallet->commits, 0);
    }
    void assessedCompleteScriptsDoNotRequireSigning_data()
    {
        QTest::addColumn<bool>("nested");
        QTest::addColumn<int>("boundary");
        for (bool nested : {false, true}) for (int boundary = 0; boundary < 6; ++boundary)
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(nested ? "nested" : "witness").arg(boundary))) << nested << boundary;
    }
    void assessedCompleteScriptsDoNotRequireSigning()
    {
        QFETCH(bool, nested);
        QFETCH(int, boundary);
        ECC_Context ecc;
        const auto original = psbtfeefixture::SignedWithoutScripts(nested);
        const auto raw = psbtwork::SerializeRaw(original.document);
        auto expected_document = original.document;
        expected_document.inputs[0].witness_script = original.witness;
        expected_document.inputs[0].redeem_script = original.redeem;
        CMutableTransaction expected;
        QVERIFY(FinalizeAndExtractPSBT(expected_document, expected));
        auto incomplete = original.document;
        CMutableTransaction absent;
        QVERIFY(!FinalizeAndExtractPSBT(incomplete, absent));
        Fixture f;
        f.wallet->no_keys = true;
        f.wallet->encrypted = true;
        f.wallet->metadata = [](PartiallySignedTransaction&, bool&) {
            throw std::runtime_error("The reviewed assessment must not be reconstructed");
        };
        auto assessment = expected_document;
        if (boundary == 2) assessment.outputs[0].amount = 1'499;
        if (boundary == 3) assessment.inputs[0].witness_utxo.nValue = 1'999;
        if (boundary == 5) assessment = original.document;
        walletsend::Request request;
        request.reviewed = MakeTransactionRef(expected);
        request.psbt = std::make_shared<const PartiallySignedTransaction>(original.document);
        request.proof = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(original.document));
        request.approved_fee = 500;
        request.assessed_complete = true;
        if (boundary != 4) request.assessment = std::make_shared<const PartiallySignedTransaction>(std::move(assessment));
        auto operation = f.operation();
        QVERIFY(operation->start(std::move(request)));
        if (boundary == 1) f.opening->retire();
        QCOMPARE(f.wallet->metadata_reads, 0);
        QVERIFY(f.commands.step(Lane::Domain));
        QVERIFY(f.commands.step(Lane::Core));
        QCOMPARE(f.wallet->metadata_reads, 0);
        QVERIFY(f.wallet->metadata_derivations.empty());
        QCOMPARE(f.wallet->signs, 0);
        QCOMPARE(f.wallet->unlocks, 0);
        QCOMPARE(f.wallet->locks, 0);
        QVERIFY(f.wallet->locked);
        QCOMPARE(f.wallet->commits, boundary < 2 ? 1 : 0);
        QVERIFY(!operation->result()); // Actual effect exists while its Domain receipt is held.
        QCOMPARE(f.commands.queuedCountIf([](const auto& turn) { return turn.lane == Lane::Domain; }), 2);
        QVERIFY(f.commands.step(Lane::Domain)); // Retire the planning command's empty outcome.
        QVERIFY(!operation->result());
        QVERIFY(f.commands.step(Lane::Domain));
        auto result = operation->result();
        QVERIFY(result);
        QCOMPARE(result->error, boundary < 2 ? Error::None : boundary == 2 || boundary == 4 ? Error::ChangedTransaction : boundary == 3 ? Error::InvalidFee : Error::Incomplete);
        if (boundary < 2) {
            QVERIFY(result->committed);
            QCOMPARE(result->committed->GetWitnessHash(), MakeTransactionRef(expected)->GetWitnessHash());
        }
        QCOMPARE(psbtwork::SerializeRaw(original.document), raw);
        QCOMPARE(original.document.inputs[0].partial_sigs.size(), size_t{2});
        result.reset(); operation.reset();
        f.notes->close(); f.opening->retire();
        f.commands.drain(); f.retirement.drain(); f.commands.drain();
        QVERIFY(f.commands.quiescent()); QVERIFY(f.retirement.quiescent());
    }
    void retainedCoreTransactionDoesNotRetainAppCleanupLease()
    {
        Fixture f;
        const auto original = Transaction();
        auto retained = walletopening::RetainValues<CTransactionRef>(f.opening, [&] {
            return std::make_unique<CTransactionRef>(original);
        });
        QVERIFY(retained);
        const std::weak_ptr<CTransactionRef> original_owner{retained};
        walletsend::Request request;
        request.reviewed = CTransactionRef{retained, retained->get()};
        auto operation = f.operation();
        QVERIFY(operation->start(std::move(request)));
        retained.reset();
        QVERIFY(!original_owner.expired());
        QVERIFY(f.commands.step(Lane::Domain));
        QVERIFY(f.commands.step(Lane::Core));
        QCOMPARE(f.wallet->commits, 1); QVERIFY(!operation->result());
        QVERIFY(f.wallet->last_transaction);
        QCOMPARE(f.wallet->last_transaction->GetHash(), original->GetHash());
        QCOMPARE(f.wallet->last_transaction->GetWitnessHash(), original->GetWitnessHash());
        f.commands.drain();
        auto outcome = operation->result(); QVERIFY(outcome); QCOMPARE(outcome->error, Error::None);
        QVERIFY(outcome->committed); QCOMPARE(outcome->committed->GetHash(), original->GetHash());
        // Keep the mock's Core wallet history alive through the entire app drain.
        // A retained app alias in commitTransaction would prevent retirement.
        outcome.reset(); operation.reset(); f.notes->close(); f.opening->retire();
        f.commands.drain(); f.retirement.drain(); f.commands.drain();
        QVERIFY(original_owner.expired());
        const auto closed = f.opening->take(); QVERIFY(closed);
        QCOMPARE(closed->phase, walletopening::Phase::Closed);
        QVERIFY(f.commands.quiescent()); QVERIFY(f.retirement.quiescent());
        QVERIFY(f.wallet->last_transaction);
        QCOMPARE(f.wallet->last_transaction->GetHash(), original->GetHash());
    }

    void explicitReviewWithdrawalHasOneCommitBoundary_data()
    {
        QTest::addColumn<int>("phase");QTest::newRow("before-effect")<<0;QTest::newRow("after-unlock-before-commit")<<1;QTest::newRow("after-commit-claim")<<2;QTest::newRow("passive-close-preserves-consent")<<3;
    }
    void explicitReviewWithdrawalHasOneCommitBoundary()
    {
        QFETCH(int,phase);Fixture f;f.wallet->encrypted=true;
        auto consent=std::make_shared<walletsend::CommitConsent>();auto request=PsbtRequest();request.passphrase=SecureString{"secret"};request.consent=consent;
        if(phase==1)f.wallet->after_unlock=[consent]{consent->withdraw();};
        if(phase==2)f.wallet->before_commit=[consent]{consent->withdraw();};
        auto operation=f.operation();QVERIFY(operation->start(std::move(request)));QVERIFY(!operation->result());
        if(phase==3)f.opening->close();
        QVERIFY(f.commands.step(Lane::Domain));
        if(phase==0)consent->withdraw();
        QVERIFY(f.commands.step(Lane::Core));QCOMPARE(f.wallet->commits,phase<2?0:1);QVERIFY(!operation->result());
        if(phase==0)QCOMPARE(f.wallet->unlocks,0);else{QCOMPARE(f.wallet->unlocks,1);QCOMPARE(f.wallet->locks,1);QVERIFY(f.wallet->locked);}
        QCOMPARE(f.commands.queuedCountIf([](const auto& turn) { return turn.lane == Lane::Domain; }), 2);
        QVERIFY(f.commands.step(Lane::Domain)); // Retire the planning command's empty outcome.
        QVERIFY(!operation->result());
        QVERIFY(f.commands.step(Lane::Domain));
        auto operation_result = operation->result();
        QVERIFY(operation_result);
        QCOMPARE(operation->result()->error,phase<2?Error::Cancelled:Error::None);QCOMPARE(bool(operation->result()->committed),phase>=2);
        if(phase>=2){consent->withdraw();QVERIFY(operation->result()->committed);QCOMPARE(f.wallet->commits,1);QVERIFY(!consent->withdrawn());}
    }
    void capturedPasswordOwnsOriginalSend_data()
    {
        QTest::addColumn<int>("scenario");QTest::newRow("held-result")<<0;QTest::newRow("quiet-close-before-work")<<1;QTest::newRow("native-submitted-refused")<<2;
    }
    void capturedPasswordOwnsOriginalSend()
    {
        QFETCH(int,scenario);Fixture f;f.wallet->encrypted=true;f.wallet->expected_password=std::string(40'000,'x');
        QString source=QString::fromStdString(f.wallet->expected_password);auto captured=secretinput::Capture(f.commands.scheduler,source);QVERIFY(captured);
        auto request=PsbtRequest();request.captured_passphrase=captured.input;auto operation=f.operation();
        if(scenario==2)f.commands.rejectLane(Lane::Core);
        const auto admitted=operation->start(std::move(request));source.fill(QChar{'y'});QCOMPARE(f.wallet->unlocks,0);QCOMPARE(f.wallet->commits,0);
        if(scenario==2){QVERIFY(admitted);f.commands.drain();QCOMPARE(f.wallet->unlocks,0);QCOMPARE(f.wallet->commits,0);QCOMPARE(operation->result()->error,Error::Rejected);}
        else{QVERIFY(admitted);if(scenario==1)f.opening->close();QVERIFY(f.commands.step(Lane::Domain));QVERIFY(f.commands.step(Lane::Core));QCOMPARE(f.wallet->unlocks,1);QCOMPARE(f.wallet->commits,1);QCOMPARE(f.wallet->locks,1);QVERIFY(f.wallet->locked);QVERIFY(!operation->result());
            QCOMPARE(f.commands.queuedCountIf([](const auto& turn) { return turn.lane == Lane::Domain; }), 2);
            QVERIFY(f.commands.step(Lane::Domain)); QVERIFY(!operation->result());
            QVERIFY(f.commands.step(Lane::Domain));
            QCOMPARE(operation->result()->error,Error::None);QVERIFY(operation->result()->committed);}
        captured.input.reset();operation.reset();f.commands.drain();f.retirement.drain();
    }
    void signerApprovalOwnsItsOriginalOutcome_data()
    {
        QTest::addColumn<int>("scenario");
        for (int scenario = 0; scenario < 6; ++scenario) QTest::newRow(qPrintable(QString::number(scenario))) << scenario;
    }
    void signerApprovalOwnsItsOriginalOutcome()
    {
        QFETCH(int, scenario);
        ECC_Context ecc;
        Fixture f;
        f.wallet->signer = scenario != 4;
        f.wallet->partial_sign = scenario == 1;
        f.wallet->change_transaction = scenario == 2;
        f.wallet->throw_sign = scenario == 5;
        auto request = VerifiedApprovalRequest(*f.wallet);
        if (scenario == 3) request.approved_fee = 999;
        const auto action = f.opening->startAction();
        QVERIFY(action);
        auto command = std::make_shared<execution::OwnedCommand<walletsigning::Result>>(f.commands.scheduler, [] {});
        QVERIFY(command->startWith([&](auto done, auto failed) {
            return f.opening->perform(action, [request, opening = f.opening](interfaces::Wallet& wallet) {
                return walletsigning::Approve(wallet, request, opening);
            }, std::move(done), std::move(failed));
        }));
        QCOMPARE(f.wallet->signs, 0);
        QVERIFY(!command->result());
        QVERIFY(f.commands.step(Lane::Core));
        QVERIFY(!command->result());
        f.opening->close();
        QVERIFY(!f.opening->startAction());
        QVERIFY(f.commands.step(Lane::Domain));
        QVERIFY(command->result());
        QVERIFY(command->result()->value);
        const auto result = *command->result()->value;
        const walletsigning::Error expected[] = {walletsigning::Error::None, walletsigning::Error::None,
            walletsigning::Error::ChangedTransaction, walletsigning::Error::InvalidFee,
            walletsigning::Error::SignerNotFound, walletsigning::Error::Uncertain};
        QCOMPARE(result.error, expected[scenario]);
        QCOMPARE(result.partial, scenario == 1 || scenario == 5);
        QCOMPARE(bool(result.signed_transaction), scenario == 0);
        if (scenario == 1 || scenario == 2) QVERIFY(result.document);
        if (scenario == 2) QCOMPARE(result.document->GetUnsignedTx()->GetHash(), request.document->GetUnsignedTx()->GetHash());
        QCOMPARE(f.wallet->commits, 0);
        QCOMPARE(f.wallet->signs, scenario == 3 || scenario == 4 ? 0 : scenario == 1 ? 2 : 1);
    }
    void admittedSignerRunsAfterQueueClosureUsingReservedOutput()
    {
        ECC_Context ecc;
        Fixture f;
        f.wallet->signer = true;
        const auto request = VerifiedApprovalRequest(*f.wallet);
        auto output = walletopening::RetainValues<walletsigning::Result>(f.opening, [] { return std::make_unique<walletsigning::Result>(); });
        QVERIFY(output);
        auto action = f.opening->startAction(); QVERIFY(action);
        auto command = std::make_shared<execution::OwnedCommand<walletsigning::Result>>(f.commands.scheduler, [] {});
        QVERIFY(command->startWith([&](auto done, auto failed) {
            return f.opening->perform(action, [request, output](interfaces::Wallet& wallet) {
                return walletsigning::ApproveInto(wallet, request, output);
            }, std::move(done), std::move(failed));
        }));
        f.opening->close();
        f.commands.scheduler->close();
        QVERIFY(f.commands.step(execution::Lane::Core));
        QCOMPARE(f.wallet->signs, 1);
        QVERIFY(!command->result());
        QVERIFY(f.commands.step(execution::Lane::Domain));
        QVERIFY(command->result()->value);
        QCOMPARE(command->result()->value->error, walletsigning::Error::None);
        QVERIFY(command->result()->value->signed_transaction);
        command.reset(); output.reset(); action.reset();
        f.commands.drain();
    }
    void approvalOwnerPreservesTheCapturedReviewAcrossManualTurns_data()
    {
        QTest::addColumn<bool>("review_changes_while_device_is_held");
        QTest::addColumn<bool>("unverified");
        QTest::newRow("current-review") << false << false;
        QTest::newRow("replacement-review") << true << false;
        QTest::newRow("unverified-current-review") << false << true;
        QTest::newRow("unverified-replacement-review") << true << true;
    }
    void approvalOwnerPreservesTheCapturedReviewAcrossManualTurns()
    {
        QFETCH(bool, review_changes_while_device_is_held);
        QFETCH(bool, unverified);
        ECC_Context ecc;
        Fixture f;
        f.wallet->signer = true;
        auto request = VerifiedApprovalRequest(*f.wallet);
        if (unverified) {
            const auto fixture = psbtfeefixture::SignedWithoutScripts(false);
            auto original = fixture.document;
            original.inputs[0].partial_sigs.clear();
            auto returned = fixture.document;
            returned.inputs[0].partial_sigs.erase(std::next(returned.inputs[0].partial_sigs.begin()));
            f.wallet->metadata = [](PartiallySignedTransaction&, bool& complete) { complete = false; };
            f.wallet->completed_document = std::make_shared<const PartiallySignedTransaction>(returned);
            request = {MakeTransactionRef(*original.GetUnsignedTx()),
                std::make_shared<const PartiallySignedTransaction>(original),
                std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(original)), 500};
        }
        std::vector<walletsigning::ApprovalSnapshot> snapshots;
        auto owner = std::make_shared<walletsigning::ApprovalOwner>(f.opening, [&snapshots](auto snapshot) {
            snapshots.push_back(std::move(snapshot));
        });

        constexpr uint64_t review = 71, view = 4, attempt = 91;
        QVERIFY(owner->reviewChanged(review, view));
        QVERIFY(f.commands.step(Lane::Domain));
        QCOMPARE(snapshots.back().current_review, review);
        QCOMPARE(snapshots.back().current_view, view);
        auto retained = walletopening::RetainValues<walletsigning::Request>(f.opening, [&] {
            return std::make_unique<walletsigning::Request>(request);
        });
        QVERIFY(retained);
        QVERIFY(owner->request(attempt, review, view, retained));
        retained.reset();
        for (int turn = 0; snapshots.back().phase != walletsigning::ApprovalPhase::WaitingDevice && turn < 8; ++turn)
            QVERIFY(f.commands.step(Lane::Domain)); // Drain the owner command's Domain receipts, never the held Core effect.
        QCOMPARE(snapshots.back().phase, walletsigning::ApprovalPhase::WaitingDevice);
        QCOMPARE(snapshots.back().attempt_id, attempt);
        QCOMPARE(snapshots.back().attempt_review, review);
        QCOMPARE(snapshots.back().attempt_view, view);
        QCOMPARE(f.wallet->signs, 0);
        QCOMPARE(f.wallet->commits, 0);

        if (review_changes_while_device_is_held) {
            QVERIFY(owner->reviewChanged(review + 1, view + 1));
            for (int turn = 0; snapshots.back().current_review != review + 1 && turn < 8; ++turn)
                QVERIFY(f.commands.step(Lane::Domain));
            QCOMPARE(snapshots.back().phase, walletsigning::ApprovalPhase::Cancelling);
            QVERIFY(snapshots.back().in_flight);
            QVERIFY(!snapshots.back().publishable);
        }
        QVERIFY(f.commands.step(Lane::Core)); // The captured signer request is the only device effect.
        QCOMPARE(f.wallet->signs, 1);
        QCOMPARE(f.wallet->commits, 0);
        for (int turn = 0; snapshots.back().in_flight && turn < 8; ++turn)
            QVERIFY(f.commands.step(Lane::Domain)); // Consume the exact native result on Domain.
        QVERIFY(!snapshots.back().in_flight);
        auto terminal = snapshots.back();
        QCOMPARE(terminal.attempt_id, attempt);
        QCOMPARE(terminal.attempt_review, review);
        QCOMPARE(terminal.attempt_view, view);
        QCOMPARE(terminal.current_review, review_changes_while_device_is_held ? review + 1 : review);
        QCOMPARE(terminal.current_view, review_changes_while_device_is_held ? view + 1 : view);
        QVERIFY(!terminal.in_flight);
        QCOMPARE(terminal.publishable, !review_changes_while_device_is_held);
        if (review_changes_while_device_is_held) {
            QCOMPARE(terminal.phase, walletsigning::ApprovalPhase::Idle);
            QVERIFY(!terminal.result);
        } else if (unverified) {
            QCOMPARE(terminal.phase, walletsigning::ApprovalPhase::Unverified);
            QVERIFY(terminal.result);
            QCOMPARE(terminal.result->error, walletsigning::Error::UnverifiedSignatures);
            QVERIFY(terminal.result->quarantined_document);
            QVERIFY(!terminal.result->partial);
            QVERIFY(!terminal.result->signed_transaction);
        } else {
            QCOMPARE(terminal.phase, walletsigning::ApprovalPhase::Complete);
            QVERIFY(terminal.result);
            QCOMPARE(terminal.result->error, walletsigning::Error::None);
            QVERIFY(terminal.result->signed_transaction);
            CMutableTransaction signed_terms{*terminal.result->signed_transaction};
            for (auto& input : signed_terms.vin) {
                input.scriptSig.clear();
                input.scriptWitness.SetNull();
            }
            QCOMPARE(MakeTransactionRef(std::move(signed_terms))->GetHash(), request.reviewed->GetHash());
        }

        terminal.result.reset();
        snapshots.clear();
        QVERIFY(owner->close());
        f.commands.drain();
        owner.reset();
        f.opening->retire();
        f.commands.drain(); f.retirement.drain(); f.commands.drain();
        QVERIFY(f.commands.quiescent()); QVERIFY(f.retirement.quiescent());
    }
    void approvalOwnerRunsSignerAndDomainReceiptOffGuiWithoutBlocking_data()
    {
        QTest::addColumn<bool>("replace_review_while_device_is_held");
        QTest::addColumn<bool>("close_while_device_is_held");
        QTest::addColumn<bool>("incompatible_signer_result");
        QTest::newRow("current-review-compatible") << false << false << false;
        QTest::newRow("current-review-incompatible") << false << false << true;
        QTest::newRow("replacement-review-compatible") << true << false << false;
        QTest::newRow("replacement-review-incompatible") << true << false << true;
        QTest::newRow("closed-compatible") << false << true << false;
        QTest::newRow("closed-incompatible") << false << true << true;
    }
    void approvalOwnerRunsSignerAndDomainReceiptOffGuiWithoutBlocking()
    {
        QFETCH(bool, replace_review_while_device_is_held);
        QFETCH(bool, close_while_device_is_held);
        QFETCH(bool, incompatible_signer_result);
        struct State {
            QSemaphore entered, release, initial_review, replacement_review, cancelling, terminal, closed;
            std::mutex mutex;
            std::vector<walletsigning::ApprovalSnapshot> snapshots;
            std::atomic<QThread*> notification_thread{nullptr};
        };
        ECC_Context ecc;
        auto state = std::make_shared<State>();
        BackendExecutor commands{execution::Options{}}, retirement;
        auto opening = std::make_shared<walletopening::Session>(commands.scheduler(), retirement.scheduler(),
            std::make_shared<std::atomic_bool>(false), [] {});
        auto wallet = std::make_shared<Wallet>();
        wallet->signer = true;
        wallet->before_sign = [state] {
            state->entered.release();
            state->release.acquire();
        };
        auto registered = opening->start(wallet);
        QVERIFY(registered.borrower);
        const auto request = VerifiedApprovalRequest(*wallet);
        auto owner = std::make_shared<walletsigning::ApprovalOwner>(opening, [state](auto snapshot) {
            state->notification_thread = QThread::currentThread();
            {
                std::lock_guard lock{state->mutex};
                state->snapshots.push_back(snapshot);
            }
            if (snapshot.phase == walletsigning::ApprovalPhase::Cancelling && snapshot.in_flight) state->cancelling.release();
            if (snapshot.phase == walletsigning::ApprovalPhase::Closed) state->closed.release();
            if (snapshot.current_review == 51) state->initial_review.release();
            if (snapshot.current_review == 52) state->replacement_review.release();
            if (snapshot.attempt_id == 101 && !snapshot.in_flight) state->terminal.release();
        });
        QVERIFY(owner->reviewChanged(51, 3));
        QVERIFY(state->initial_review.tryAcquire(1, 5000));
        auto retained = walletopening::RetainValues<walletsigning::Request>(opening, [&] {
            return std::make_unique<walletsigning::Request>(request);
        });
        QVERIFY(retained);
        QVERIFY(owner->request(101, 51, 3, retained));
        retained.reset();
        registered.borrower.reset();

        const auto gui_thread = QCoreApplication::instance()->thread();
        QVERIFY(state->entered.tryAcquire(1, 5000));
        QVERIFY(wallet->sign_thread.load());
        QVERIFY(wallet->sign_thread.load() != gui_thread);

        // Domain remains available while the physical signer call is held.
        if (replace_review_while_device_is_held) {
            QVERIFY(owner->reviewChanged(52, 4));
            QVERIFY(state->replacement_review.tryAcquire(1, 5000));
        }
        if (close_while_device_is_held) {
            QVERIFY(owner->close());
            QVERIFY(state->cancelling.tryAcquire(1, 5000));
            std::lock_guard lock{state->mutex};
            const auto found = std::find_if(state->snapshots.rbegin(), state->snapshots.rend(), [](const auto& snapshot) {
                return snapshot.attempt_id == 101 && snapshot.phase == walletsigning::ApprovalPhase::Cancelling;
            });
            QVERIFY(found != state->snapshots.rend());
            QVERIFY(found->in_flight);
            QVERIFY(!found->publishable);
        }
        QVERIFY(state->notification_thread.load());
        QVERIFY(state->notification_thread.load() != gui_thread);
        QCOMPARE(wallet->commits, 0);

        if (incompatible_signer_result) {
            auto invalid = *wallet->completed_document;
            const auto incompatible = psbtfeefixture::Legacy(2, false);
            for (size_t i = 0; i < invalid.inputs.size(); ++i)
                invalid.inputs[i].final_script_sig = incompatible.inputs[i].final_script_sig;
            // The wallet is held in before_sign; the semaphore handoff below
            // makes this replacement response deterministic before it reads it.
            wallet->completed_document = std::make_shared<const PartiallySignedTransaction>(std::move(invalid));
        }

        bool released{false};
        const auto release_on_exit = qScopeGuard([&] { if (!released) state->release.release(); });
        state->release.release();
        released = true;
        QVERIFY(state->terminal.tryAcquire(1, 5000));
        {
            std::lock_guard lock{state->mutex};
            QVERIFY(!state->snapshots.empty());
            const auto found = std::find_if(state->snapshots.rbegin(), state->snapshots.rend(), [](const auto& snapshot) {
                return snapshot.attempt_id == 101 && !snapshot.in_flight;
            });
            QVERIFY(found != state->snapshots.rend());
            QCOMPARE(found->current_review, uint64_t{replace_review_while_device_is_held ? 52U : 51U});
            QCOMPARE(found->current_view, uint64_t{replace_review_while_device_is_held ? 4U : 3U});
            if (close_while_device_is_held || replace_review_while_device_is_held) {
                QVERIFY(!found->publishable);
                QVERIFY(!found->result);
                QCOMPARE(found->phase, close_while_device_is_held ? walletsigning::ApprovalPhase::Closed : walletsigning::ApprovalPhase::Idle);
            } else {
                QVERIFY(found->publishable);
                QVERIFY(found->result);
                if (incompatible_signer_result) {
                    QCOMPARE(found->phase, walletsigning::ApprovalPhase::Failed);
                    QCOMPARE(found->result->error, walletsigning::Error::InvalidSignature);
                    QVERIFY(!found->result->signed_transaction);
                    QCOMPARE(found->result->document->GetUnsignedTx()->GetHash(), request.document->GetUnsignedTx()->GetHash());
                } else {
                    QCOMPARE(found->phase, walletsigning::ApprovalPhase::Complete);
                    QCOMPARE(found->result->error, walletsigning::Error::None);
                    QVERIFY(found->result->signed_transaction);
                    CMutableTransaction signed_terms{*found->result->signed_transaction};
                    for (auto& input : signed_terms.vin) {
                        input.scriptSig.clear();
                        input.scriptWitness.SetNull();
                    }
                    QCOMPARE(MakeTransactionRef(std::move(signed_terms))->GetHash(), request.reviewed->GetHash());
                }
            }
        }
        QCOMPARE(wallet->signs, 1);
        QCOMPARE(wallet->commits, 0);

        if (!close_while_device_is_held) QVERIFY(owner->close());
        QVERIFY(state->closed.tryAcquire(1, 5000));
        owner.reset();
        {
            std::lock_guard lock{state->mutex};
            state->snapshots.clear();
        }
        opening->retire();
        wallet.reset();
        commands.shutdown();
        opening.reset();
        QTRY_VERIFY(commands.isDrained());
        retirement.shutdown();
        QTRY_VERIFY(retirement.isDrained());
    }
    void signerPreservesAuthenticatedOutputOnError_data()
    {
        QTest::addColumn<bool>("throws");
        QTest::newRow("reported-failure") << false;
        QTest::newRow("throw-after-output") << true;
    }
    void signerPreservesAuthenticatedOutputOnError()
    {
        QFETCH(bool, throws);
        Fixture f;
        f.wallet->signer = true;
        f.wallet->mutate_then_throw = throws;
        f.wallet->report_sign_error = !throws;
        const auto send = PsbtRequest();
        walletsigning::Request request{send.reviewed, send.psbt, send.proof, send.approved_fee};
        const auto result = walletsigning::Approve(*f.wallet, request, f.opening);
        QCOMPARE(result.error, throws ? walletsigning::Error::Uncertain : walletsigning::Error::SignerFailed);
        QVERIFY(result.partial);
        QVERIFY(result.document);
        QVERIFY(result.lifetime);
        QCOMPARE(result.document->inputs[0].final_script_sig, CScript{} << OP_TRUE);
        QVERIFY(psbtfee::MatchesReview(*result.document, *request.proof, request.approved_fee));
        QCOMPARE(f.wallet->commits, 0);
    }
    void realWorkerConsumesCommitAfterCloseWithoutGuiDelivery()
    {
        struct State { QSemaphore entered, release, consumed; std::atomic<QThread*> effect{nullptr}, outcome{nullptr}, observation{nullptr}; };
        struct RealWallet : Wallet {
            std::shared_ptr<State> state;
            explicit RealWallet(std::shared_ptr<State> value) : state(std::move(value)) {}
            void commitTransaction(CTransactionRef transaction, const std::vector<std::string>&) override {
                state->effect = QThread::currentThread(); state->entered.release(); state->release.acquire(); last_transaction = std::move(transaction);
            }
            interfaces::WalletTx getWalletTxDetails(const Txid& id, interfaces::WalletTxStatus& status, std::vector<std::string>& messages, std::vector<std::string>& requests, bool& mempool, int& blocks) override {
                state->observation = QThread::currentThread(); return Wallet::getWalletTxDetails(id, status, messages, requests, mempool, blocks);
            }
        };
        auto state = std::make_shared<State>(); BackendExecutor commands{execution::Options{}}, retirement;
        auto opening = std::make_shared<walletopening::Session>(commands.scheduler(), retirement.scheduler(), std::make_shared<std::atomic_bool>(false), [] {});
        auto wallet = std::make_shared<RealWallet>(state);wallet->encrypted=true;wallet->expected_password=std::string(40'000,'r');auto client = opening->start(wallet).borrower; QVERIFY(client);
        auto captured=secretinput::Capture(commands.scheduler(),QString::fromStdString(wallet->expected_password));QVERIFY(captured);
        auto request=PsbtRequest();request.captured_passphrase=captured.input;
        auto operation = std::make_shared<walletsend::Operation>(opening, nullptr, [state](auto) { state->outcome = QThread::currentThread(); state->consumed.release(); });
        QVERIFY(operation->start(std::move(request)));captured.input.reset();
        const auto release = qScopeGuard([state] { state->release.release(); });
        QVERIFY(state->entered.tryAcquire(1, 5000)); QVERIFY(state->effect.load() != QThread::currentThread());
        QCOMPARE(wallet->unlocks,1);QVERIFY(wallet->unlock_thread.load()!=QThread::currentThread());
        opening->retire(); client.reset(); wallet.reset(); commands.shutdown();
        QVERIFY(!operation->result()); state->release.release();
        QVERIFY(state->consumed.tryAcquire(1, 5000)); QVERIFY(state->outcome.load() != QThread::currentThread());
        QVERIFY(state->observation.load()); QVERIFY(state->observation.load() != QThread::currentThread());
        QVERIFY(operation->result()->committed); QCOMPARE(operation->result()->error, Error::None);
        QVERIFY(!commands.isDrained()); operation.reset();
        QTRY_VERIFY(commands.isDrained()); opening.reset(); retirement.shutdown(); QTRY_VERIFY(retirement.isDrained());
    }
    void acceptedCommitAndConsumedOutcomeSurviveCloseWithoutGui()
    {
        Fixture f; auto operation = f.operation(); QVERIFY(operation->start(SignedRequest()));
        QVERIFY(f.commands.step(Lane::Domain));
        const auto effect = f.commands.dispatchNext(Lane::Core); QVERIFY(effect); QCOMPARE(f.wallet->commits, 0);
        f.notes->close(); f.opening->retire(); QVERIFY(!operation->result());
        QVERIFY(f.commands.finishWork(*effect)); QCOMPARE(f.wallet->commits, 1); QVERIFY(!operation->result());
        const auto planning_outcome = f.commands.deliverOutcome(); QVERIFY(planning_outcome);
        QVERIFY(f.commands.stepMailbox(*planning_outcome)); QVERIFY(!operation->result());
        const auto outcome = f.commands.deliverOutcome(); QVERIFY(outcome); QVERIFY(f.commands.stepMailbox(*outcome));
        QCOMPARE(operation->result()->error, Error::None); QVERIFY(operation->result()->committed); QCOMPARE(f.notifications, 1);
        operation.reset();
        f.commands.drain(); f.retirement.drain(); QVERIFY(f.commands.quiescent() && f.retirement.quiescent());
    }
    void committedSubmissionObservationIsIndependent_data()
    {
        QTest::addColumn<int>("scenario");
        const char* names[] = {"mempool", "confirmed", "not-in-mempool", "conflicted", "abandoned", "missing", "foreign-transaction", "read-failed"};
        for (int n{0}; n < 8; ++n) QTest::newRow(names[n]) << n;
    }
    void committedSubmissionObservationIsIndependent()
    {
        QFETCH(int, scenario); Fixture f;
        f.wallet->in_mempool = scenario == 0; f.wallet->depth = scenario == 1 ? 2 : scenario == 3 ? -1 : 0;
        f.wallet->abandoned = scenario == 4; f.wallet->missing = scenario == 5; f.wallet->mismatch = scenario == 6; f.wallet->throw_status = scenario == 7;
        const auto operation = f.operation(); QVERIFY(operation->start(SignedRequest())); f.commands.drain();
        const auto result = *operation->result(); const walletsend::Submission expected[] = {
            walletsend::Submission::Mempool, walletsend::Submission::Confirmed, walletsend::Submission::NotInMempool, walletsend::Submission::Conflicted,
            walletsend::Submission::Abandoned, walletsend::Submission::Unknown, walletsend::Submission::Unknown, walletsend::Submission::Unknown};
        QCOMPARE(result.error, Error::None); QVERIFY(result.committed); QCOMPARE(result.submission, expected[scenario]);
        const walletsend::Record expected_record[] = {walletsend::Record::Recorded, walletsend::Record::Recorded,
            walletsend::Record::Recorded, walletsend::Record::Recorded, walletsend::Record::Recorded,
            walletsend::Record::AbsentAtObservation, walletsend::Record::AbsentAtObservation, walletsend::Record::Unknown};
        QCOMPARE(result.record, expected_record[scenario]);
        QCOMPARE(result.candidate->GetHash(), result.committed->GetHash());
        QCOMPARE(result.relay, walletsend::Relay::Unknown);
        QCOMPARE(f.wallet->commits, 1); QCOMPARE(f.wallet->status_reads, 1);
    }
    void sweepConsentAndPasswordContinuationRetainOneReviewedOperation()
    {
        Fixture f; f.wallet->encrypted = true;
        auto request = PsbtRequest(); request.attempt_id = 41; request.review_id = 17;
        request.sweeps_wallet = true; request.consent = std::make_shared<walletsend::CommitConsent>();
        const auto reviewed = request.reviewed->GetHash();
        auto operation = f.operation(); QVERIFY(operation->start(std::move(request)));
        f.commands.drain();
        QCOMPARE(operation->stage(), walletsend::Stage::AwaitingSweepConsent);
        QCOMPARE(f.wallet->commits, 0); QVERIFY(!operation->result());
        QVERIFY(operation->confirmSweep(41, 17));
        QCOMPARE(operation->confirmSweep(41, 17).rejection, execution::Rejection::Invalid);
        f.commands.drain();
        QCOMPARE(operation->stage(), walletsend::Stage::AwaitingCredentials);
        QCOMPARE(f.wallet->commits, 0); QVERIFY(!operation->result());
        auto captured = secretinput::Capture(f.commands.scheduler, QStringLiteral("secret")); QVERIFY(captured);
        QVERIFY(operation->providePassphrase(41, 17, std::move(captured.input)));
        const auto duplicate = secretinput::Capture(f.commands.scheduler, QStringLiteral("secret")); QVERIFY(duplicate);
        QCOMPARE(operation->providePassphrase(41, 17, duplicate.input).rejection, execution::Rejection::Invalid);
        f.commands.drain();
        const auto result = operation->result(); QVERIFY(result);
        QCOMPARE(result->error, Error::None); QVERIFY(result->committed);
        QCOMPARE(result->candidate->GetHash(), reviewed);
        CMutableTransaction reviewed_terms{*result->candidate};
        CMutableTransaction committed_terms{*result->committed};
        for (auto* transaction : {&reviewed_terms, &committed_terms}) {
            for (auto& input : transaction->vin) { input.scriptSig.clear(); input.scriptWitness.SetNull(); }
        }
        QCOMPARE(committed_terms.GetHash(), reviewed_terms.GetHash());
        QCOMPARE(result->record, walletsend::Record::Recorded);
        QCOMPARE(f.wallet->unlocks, 1); QCOMPARE(f.wallet->commits, 1);
        QCOMPARE(operation->stage(), walletsend::Stage::Finished);
    }
    void cancelledSweepPromptCannotSubmitOrBeConfirmedLater()
    {
        Fixture f; auto request = SignedRequest(); request.attempt_id = 52; request.review_id = 23;
        request.sweeps_wallet = true; request.consent = std::make_shared<walletsend::CommitConsent>();
        auto operation = f.operation(); QVERIFY(operation->start(std::move(request))); f.commands.drain();
        QCOMPARE(operation->stage(), walletsend::Stage::AwaitingSweepConsent);
        QVERIFY(operation->cancelPrompt(52, 23));
        QCOMPARE(operation->confirmSweep(52, 23).rejection, execution::Rejection::Invalid);
        f.commands.drain();
        QVERIFY(operation->result()); QCOMPARE(operation->result()->error, Error::Cancelled);
        QCOMPARE(operation->stage(), walletsend::Stage::Finished); QCOMPARE(f.wallet->commits, 0);
    }
    void heldOutcomeRetainsTheNativeObservationRatherThanLaterState()
    {
        Fixture f; f.wallet->in_mempool = false; auto operation = f.operation(); QVERIFY(operation->start(SignedRequest()));
        QVERIFY(f.commands.step(Lane::Domain));
        QVERIFY(f.commands.step(Lane::Core)); QVERIFY(!operation->result()); f.wallet->in_mempool = true;
        f.commands.drain(); QCOMPARE(operation->result()->submission, walletsend::Submission::NotInMempool); QCOMPARE(f.wallet->status_reads, 1);
    }
    void publicationFailureCannotOverwriteAConfirmedCommit()
    {
        Fixture f; auto operation = std::make_shared<walletsend::Operation>(f.opening, f.notes, [](auto) { throw std::runtime_error{"publication transport failed"}; });
        QVERIFY(operation->start(SignedRequest())); f.commands.drain();
        QVERIFY(operation->result()->committed); QCOMPARE(operation->result()->error, Error::None); QCOMPARE(f.wallet->commits, 1);
        QVERIFY(operation->result()->publication_failed);
    }
    void localSignOnlyDoesNotInstallUnverifiedContributions_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<int>("outcome");
        for (int kind = 0; kind < 3; ++kind) for (int outcome = 0; outcome < 3; ++outcome)
            QTest::newRow(qPrintable(QStringLiteral("kind-%1-outcome-%2").arg(kind).arg(outcome))) << kind << outcome;
    }
    void localSignOnlyDoesNotInstallUnverifiedContributions()
    {
        QFETCH(int, kind);
        QFETCH(int, outcome);
        ECC_Context ecc;
        const auto fixture = psbtfeefixture::Contribution(kind != 2, kind == 1);
        Fixture f;
        f.wallet->signing_response = [returned = fixture.returned, outcome](auto& document, bool& complete) -> std::optional<common::PSBTError> {
            document = returned;
            complete = false;
            if (outcome == 2) throw std::runtime_error("signing outcome unavailable after contribution");
            return outcome == 1 ? std::optional{common::PSBTError::EXTERNAL_SIGNER_FAILED} : std::nullopt;
        };
        walletsend::Request request;
        request.intent = walletsend::Intent::SignOnly;
        request.attempt_id = 88;
        request.review_id = 34;
        request.reviewed = MakeTransactionRef(*fixture.original.GetUnsignedTx());
        request.psbt = std::make_shared<const PartiallySignedTransaction>(fixture.original);
        request.proof = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(fixture.original));
        request.approved_fee = 500;
        const auto operation = f.operation();
        QVERIFY(operation->start(std::move(request)));
        f.commands.drain();
        const auto result = operation->result();
        QVERIFY(result);
        QCOMPARE(f.wallet->signs, 1);
        QCOMPARE(f.wallet->commits, 0);
        QVERIFY(!result->committed);
        if (kind == 0) {
            QVERIFY(result->document);
            QCOMPARE(result->document->inputs[0].partial_sigs, fixture.returned.inputs[0].partial_sigs);
            QVERIFY(result->partial);
        } else {
            QVERIFY(!result->document || result->document->inputs[0].partial_sigs.empty());
            QVERIFY(!result->partial);
        }
    }
    void localSignOnlyPreservesTheCapturedDocumentAndNeverCommits_data()
    {
        QTest::addColumn<bool>("partial");
        QTest::newRow("complete") << false;
        QTest::newRow("existing-partial-signatures") << true;
    }
    void localSignOnlyPreservesTheCapturedDocumentAndNeverCommits()
    {
        QFETCH(bool, partial);
        ECC_Context ecc;
        Fixture f;
        walletsend::Request request;
        if (partial) {
            const auto fixture = psbtfeefixture::SignedWithoutScripts(false);
            const auto proof = psbtfee::Analyze(fixture.document);
            QVERIFY(proof.amount);
            request.reviewed = MakeTransactionRef(*fixture.document.GetUnsignedTx());
            request.psbt = std::make_shared<const PartiallySignedTransaction>(fixture.document);
            request.proof = std::make_shared<const psbtfee::Analysis>(proof);
            request.approved_fee = proof.amount;
            f.wallet->partial_sign = true;
        } else {
            request = PsbtRequest();
            request.psbt = std::make_shared<const PartiallySignedTransaction>(*request.psbt);
        }
        request.intent = walletsend::Intent::SignOnly;
        request.attempt_id = 88;
        request.review_id = 34;
        const auto original = request.psbt;
        const auto original_proof = request.proof;
        const auto approved_fee = request.approved_fee;
        const auto reviewed_hash = request.reviewed->GetHash();
        const auto operation = f.operation();
        QVERIFY(operation->start(std::move(request)));
        QCOMPARE(operation->stage(), walletsend::Stage::Idle);
        QCOMPARE(f.wallet->signs, 0);
        QCOMPARE(f.wallet->commits, 0);

        QVERIFY(f.commands.step(Lane::Domain));
        QCOMPARE(operation->stage(), walletsend::Stage::SigningOnly);
        QCOMPARE(f.wallet->signs, 0);
        QVERIFY(f.commands.step(Lane::Core));
        QCOMPARE(f.wallet->signs, 1);
        QCOMPARE(f.wallet->commits, 0);
        QVERIFY(!operation->result());
        QCOMPARE(f.commands.queuedCountIf([](const auto& turn) { return turn.lane == Lane::Domain; }), 2);
        QVERIFY(f.commands.step(Lane::Domain)); // Consume the planning command's empty outcome.
        QVERIFY(!operation->result());
        const auto native_outcome = f.commands.deliverOutcome();
        QVERIFY(native_outcome);
        QVERIFY(f.commands.stepMailbox(*native_outcome));

        const auto result = operation->result();
        QVERIFY(result);
        QCOMPARE(result->error, Error::None);
        QVERIFY(result->document);
        QVERIFY(!result->committed);
        QVERIFY(!result->candidate || result->candidate->GetHash() == reviewed_hash);
        QCOMPARE(result->partial, partial);
        QCOMPARE(result->signed_inputs, partial ? size_t{0} : size_t{1});
        QCOMPARE(result->submission, walletsend::Submission::Unknown);
        QCOMPARE(result->record, walletsend::Record::Unknown);
        QCOMPARE(result->document->GetUnsignedTx()->GetHash(), reviewed_hash);
        QVERIFY(psbtfee::MatchesReview(*result->document, *original_proof, approved_fee));
        if (partial) {
            QCOMPARE(result->document->inputs[0].partial_sigs, original->inputs[0].partial_sigs);
        }
        QCOMPARE(f.wallet->commits, 0);
        QCOMPARE(operation->stage(), walletsend::Stage::Finished);
    }
    void localSignOnlyPassphraseContinuationKeepsIntentAndOriginalReview()
    {
        Fixture f;
        f.wallet->encrypted = true;
        auto request = PsbtRequest();
        request.intent = walletsend::Intent::SignOnly;
        request.attempt_id = 89;
        request.review_id = 35;
        const auto reviewed_hash = request.reviewed->GetHash();
        auto operation = f.operation();
        QVERIFY(operation->start(std::move(request)));
        f.commands.drain();
        QCOMPARE(operation->stage(), walletsend::Stage::AwaitingCredentials);
        QCOMPARE(operation->promptError(), Error::NeedsUnlock);
        QCOMPARE(f.wallet->signs, 0);
        QCOMPARE(f.wallet->commits, 0);
        auto captured = secretinput::Capture(f.commands.scheduler, QStringLiteral("secret"));
        QVERIFY(captured);
        QVERIFY(operation->providePassphrase(89, 35, std::move(captured.input)));
        f.commands.drain();
        const auto result = operation->result();
        QVERIFY(result);
        QCOMPARE(result->error, Error::None);
        QVERIFY(result->document);
        QCOMPARE(result->document->GetUnsignedTx()->GetHash(), reviewed_hash);
        QVERIFY(!result->committed);
        QCOMPARE(f.wallet->unlocks, 1);
        QCOMPARE(f.wallet->locks, 1);
        QVERIFY(f.wallet->locked);
        QCOMPARE(f.wallet->commits, 0);
        QCOMPARE(operation->stage(), walletsend::Stage::Finished);
    }
    void duplicateInputsAreRejectedBeforeWalletSigningOrCommit_data()
    {
        QTest::addColumn<int>("intent");
        QTest::newRow("submit") << int(walletsend::Intent::Submit);
        QTest::newRow("sign-only") << int(walletsend::Intent::SignOnly);
    }
    void duplicateInputsAreRejectedBeforeWalletSigningOrCommit()
    {
        QFETCH(int, intent);
        Fixture f;
        auto request = PsbtRequest();
        const auto proof = request.proof;
        const auto approved_fee = request.approved_fee;
        CMutableTransaction duplicate{*request.reviewed};
        duplicate.vin.push_back(duplicate.vin.front());
        PartiallySignedTransaction document{duplicate, 2};
        document.inputs[0] = request.psbt->inputs[0];
        document.inputs[1] = request.psbt->inputs[0];
        request.reviewed = MakeTransactionRef(duplicate);
        request.psbt = std::make_shared<const PartiallySignedTransaction>(std::move(document));
        request.proof = proof;
        request.approved_fee = approved_fee;
        request.intent = walletsend::Intent(intent);
        auto operation = f.operation();
        QVERIFY(operation->start(std::move(request)));
        f.commands.drain();
        const auto result = operation->result();
        QVERIFY(result);
        QCOMPARE(result->error, Error::InvalidStructure);
        QVERIFY(!result->document);
        QVERIFY(!result->committed);
        QCOMPARE(f.wallet->signs, 0);
        QCOMPARE(f.wallet->metadata_reads, 0);
        QCOMPARE(f.wallet->commits, 0);
    }
    void realWorkerSignsCapturedPsbtOffGuiAndNeverCommits()
    {
        struct State { QSemaphore finished; std::atomic<QThread*> outcome{nullptr}; };
        auto state = std::make_shared<State>();
        BackendExecutor commands{execution::Options{}}, retirement;
        auto opening = std::make_shared<walletopening::Session>(commands.scheduler(), retirement.scheduler(), std::make_shared<std::atomic_bool>(false), [] {});
        auto wallet = std::make_shared<Wallet>();
        auto registered = opening->start(wallet);
        QVERIFY(registered.borrower);
        auto request = PsbtRequest();
        request.intent = walletsend::Intent::SignOnly;
        auto operation = std::make_shared<walletsend::Operation>(opening, nullptr, [state](auto notice) {
            if (notice == walletsend::Notice::Finished) { state->outcome = QThread::currentThread(); state->finished.release(); }
        });
        QVERIFY(operation->start(std::move(request)));
        registered.borrower.reset();
        QVERIFY(state->finished.tryAcquire(1, 5000));
        QCOMPARE(wallet->signs, 1);
        QVERIFY(wallet->sign_thread.load());
        QVERIFY(wallet->sign_thread.load() != QCoreApplication::instance()->thread());
        QVERIFY(state->outcome.load() != QCoreApplication::instance()->thread());
        QVERIFY(operation->result());
        QVERIFY(operation->result()->document);
        QVERIFY(!operation->result()->committed);
        QCOMPARE(wallet->commits, 0);
        opening->retire(); wallet.reset(); commands.shutdown();
        operation.reset(); opening.reset();
        QTRY_VERIFY(commands.isDrained());
        retirement.shutdown();
        QTRY_VERIFY(retirement.isDrained());
    }
    void sameOperationCannotCommitTwice()
    {
        Fixture f; auto operation = f.operation(); QVERIFY(operation->start(SignedRequest()));
        QCOMPARE(operation->start(SignedRequest()).rejection, execution::Rejection::Invalid);
        f.commands.drain(); QCOMPARE(f.wallet->commits, 1); QCOMPARE(f.notifications, 1);
    }
    void closedAndRejectedAdmissionCannotClaimCommit_data()
    {
        QTest::addColumn<bool>("close"); QTest::newRow("closed") << true; QTest::newRow("transport-rejected") << false;
    }
    void closedAndRejectedAdmissionCannotClaimCommit()
    {
        QFETCH(bool, close); Fixture f; if (close) f.opening->retire(); else f.commands.rejectLane(Lane::Core);
        auto operation = f.operation(); const auto submitted = operation->start(SignedRequest());
        if (close) QVERIFY(!submitted);
        f.commands.drain(); QVERIFY(operation->result()); QCOMPARE(operation->result()->error, Error::Rejected);
        QVERIFY(!operation->result()->committed); QCOMPARE(f.wallet->commits, 0);
    }
    void psbtSigningPreservesReviewAndRestoresLock_data()
    {
        QTest::addColumn<int>("scenario");
        for (int n{0}; n < 7; ++n) QTest::newRow(qPrintable(QString::number(n))) << n;
    }
    void psbtSigningPreservesReviewAndRestoresLock()
    {
        QFETCH(int, scenario); Fixture f; f.wallet->encrypted = true;
        auto request = PsbtRequest(); QCOMPARE(request.proof->state, psbtfee::State::Known);
        request.attempt_id = 73; request.review_id = 29;
        if (scenario != 1) request.passphrase = SecureString{scenario == 2 ? "wrong" : "secret"};
        f.wallet->change_transaction = scenario == 3; f.wallet->throw_sign = scenario == 4; f.wallet->fail_lock = scenario == 5;
        if (scenario == 6) request.approved_fee = 999;
        const auto operation = f.operation(); QVERIFY(operation->start(std::move(request))); f.commands.drain();
        if (scenario == 1 || scenario == 2) {
            QCOMPARE(operation->stage(), walletsend::Stage::AwaitingCredentials);
            QCOMPARE(operation->promptError(), scenario == 1 ? Error::NeedsUnlock : Error::IncorrectPassphrase);
            QVERIFY(!operation->result()); QCOMPARE(f.wallet->commits, 0);
            auto captured = secretinput::Capture(f.commands.scheduler, QStringLiteral("secret")); QVERIFY(captured);
            QVERIFY(operation->providePassphrase(73, 29, std::move(captured.input)));
            f.commands.drain();
        }
        const auto result = *operation->result();
        const Error expected[] = {Error::None, Error::None, Error::None, Error::ChangedTransaction, Error::Uncertain, Error::Uncertain, Error::InvalidFee};
        QCOMPARE(result.error, expected[scenario]); QCOMPARE(f.wallet->commits, scenario <= 2 ? 1 : 0);
        QCOMPARE(bool(result.committed), scenario <= 2);
        if (scenario <= 4) { QVERIFY(f.wallet->locked); QCOMPARE(f.wallet->locks, 1); }
        QCOMPARE(result.relock_failed, scenario == 5);
    }
    void notesAreConsequencesOfTheOriginalConfirmedCommit()
    {
        Fixture f; auto request = SignedRequest(); request.notes.insert("1BoatSLRHtKNngkdXEeobR76b53LETtpyT", "outgoing note");
        auto operation = f.operation(); QVERIFY(operation->start(request));
        f.notes->close(); f.opening->retire(); f.commands.drain(); f.retirement.drain();
        const auto result = *operation->result(); QVERIFY(result.committed); QVERIFY(result.notes);
        QCOMPARE(result.notes->result()->error, walletrequests::Error::None); QCOMPARE(f.wallet->labels, 1);
        Fixture failed; failed.wallet->throw_commit = true; auto uncertain = failed.operation(); QVERIFY(uncertain->start(std::move(request))); failed.commands.drain();
        QCOMPARE(uncertain->result()->error, Error::Uncertain); QVERIFY(!uncertain->result()->committed); QCOMPARE(failed.wallet->labels, 0);
    }
};
BITCOINQML_REGISTER_QT_TEST(WalletSendTests)
#include <test/test_walletsend.moc>
