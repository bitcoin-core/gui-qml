// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/signertransaction.h>
#include <qml/psbtwork.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/mocknode.h>
#include <test/mocks/mockwallet.h>
#include <test/mocks/psbtfeefixture.h>

#include <chainparams.h>
#include <common/types.h>
#include <key.h>
#include <key_io.h>
#include <node/types.h>
#include <policy/feerate.h>
#include <script/solver.h>
#include <script/signingprovider.h>
#include <streams.h>

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTranslator>
#include <QUrl>
#include <QtTest/QtTest>

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr CAmount FEE{500};

// This script requires a stack item, so an empty final script is incomplete.
PartiallySignedTransaction Document(bool complete = false)
{
    CMutableTransaction previous;
    previous.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    previous.vout.emplace_back(2'000, CScript{} << OP_DROP << OP_TRUE);
    const auto parent = MakeTransactionRef(previous);
    CMutableTransaction transaction;
    transaction.vin.emplace_back(COutPoint{parent->GetHash(), 0});
    transaction.vout.emplace_back(1'500, CScript{} << OP_TRUE);
    PartiallySignedTransaction document{transaction, 2};
    document.inputs[0].non_witness_utxo = parent;
    if (complete) document.inputs[0].final_script_sig = CScript{} << std::vector<unsigned char>{};
    return document;
}

PartiallySignedTransaction ManyOutputDocument(size_t output_count)
{
    CMutableTransaction previous;
    previous.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    previous.vout.emplace_back(static_cast<CAmount>(output_count + 10'000), CScript{} << OP_TRUE);
    const auto parent = MakeTransactionRef(previous);
    CMutableTransaction transaction;
    transaction.vin.emplace_back(COutPoint{parent->GetHash(), 0});
    for (size_t i{0}; i < output_count; ++i) transaction.vout.emplace_back(1, CScript{} << OP_TRUE);
    PartiallySignedTransaction document{transaction, 2};
    document.inputs[0].non_witness_utxo = parent;
    document.inputs[0].final_script_sig = CScript{} << std::vector<unsigned char>{};
    return document;
}

QByteArray Bytes(const PartiallySignedTransaction& document)
{
    DataStream stream;
    stream << document;
    const auto bytes = stream.str();
    return QByteArray{bytes.data(), static_cast<qsizetype>(bytes.size())};
}

void IncompatibleTimeLocks(PartiallySignedTransaction& document)
{
    document.inputs.push_back(document.inputs.front());
    document.inputs[0].height_locktime = 100;
    document.inputs[1].time_locktime = 500'000'001;
}

psbtwork::Request Request(psbtwork::Kind kind, PartiallySignedTransaction document = Document())
{
    psbtwork::Request request;
    request.kind = kind;
    request.document = std::make_shared<const PartiallySignedTransaction>(std::move(document));
    request.assessment = request.document;
    request.proof = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(*request.document));
    request.approved_fee = FEE;
    return request;
}

class WorkWallet final : public StubWallet
{
public:
    bool keys_disabled{false}, external_signer{false}, encrypted{false}, locked{false};
    bool unlock_ok{true}, lock_ok{true}, throw_lock{false}, throw_inspect{false};
    bool finalize_inspection{true};
    int unlock_calls{0}, lock_calls{0}, sign_calls{0}, inspect_calls{0};
    size_t signed_inputs{0};
    std::string observed_password;
    std::vector<std::pair<bool, bool>> options;
    std::optional<common::PSBTError> sign_error, inspect_error;
    std::function<void(PartiallySignedTransaction&)> sign_effect, inspect_effect;
    bool owned_inputs{false}, owned_outputs{false};
    size_t inspect_signable{1};
    int transaction_reads{0}, debit_reads{0}, address_reads{0};
    std::function<CTransactionRef(const Txid&)> transaction_read;
    CAmount debit{0};
    std::string address_label;
    bool address_found{false};

    CTransactionRef getTx(const Txid& txid) override
    {
        ++transaction_reads;
        return transaction_read ? transaction_read(txid) : CTransactionRef{};
    }
    bool txinIsMine(const CTxIn&) override { return owned_inputs; }
    bool txoutIsMine(const CTxOut&) override { return owned_outputs; }
    CAmount getDebit(const CTxIn&) override { ++debit_reads; return debit; }
    bool getAddress(const CTxDestination&, std::string* label, wallet::AddressPurpose*) override
    {
        ++address_reads;
        if (label) *label = address_label;
        return address_found;
    }

    bool privateKeysDisabled() override { return keys_disabled; }
    bool hasExternalSigner() override { return external_signer; }
    bool isCrypted() override { return encrypted; }
    bool isLocked() override { return locked; }
    bool lock() override
    {
        ++lock_calls;
        if (throw_lock) throw std::runtime_error{"lock outcome unavailable"};
        if (lock_ok) locked = true;
        return lock_ok;
    }
    util::Expected<void, wallet::WalletError> unlock(const SecureString& password) override
    {
        ++unlock_calls;
        observed_password.assign(password.begin(), password.end());
        if (!unlock_ok) return util::Unexpected{wallet::WalletError{
            wallet::WalletErrorCode::PassphraseIncorrect, Untranslated("incorrect test password")}};
        locked = false;
        return {};
    }
    std::optional<common::PSBTError> fillPSBT(const common::PSBTFillOptions& fill_options,
        size_t* count, PartiallySignedTransaction& document, bool& complete) override
    {
        options.emplace_back(fill_options.sign, fill_options.bip32_derivs);
        if (fill_options.sign) {
            ++sign_calls;
            if (count) *count = signed_inputs;
            if (sign_effect) sign_effect(document);
        } else {
            ++inspect_calls;
            if (throw_inspect) throw std::runtime_error{"metadata outcome unavailable"};
            if (count) *count = inspect_signable;
            if (inspect_effect) inspect_effect(document);
        }
        if (fill_options.sign || finalize_inspection) complete = FinalizePSBT(document);
        return fill_options.sign ? sign_error : inspect_error;
    }
};

class ScriptedFileReader final : public psbtwork::FileReader
{
public:
    bool open_result{true}, error_result{false};
    qint64 size_result{0}, read_limit{0};
    QByteArray bytes;
    QString failure{QStringLiteral("injected I/O failure")};
    QStringList calls;

    bool open() override { calls.append(QStringLiteral("open")); return open_result; }
    qint64 initialSize() override { calls.append(QStringLiteral("initialSize")); return size_result; }
    QByteArray read(qint64 maximum) override
    {
        calls.append(QStringLiteral("read"));
        read_limit = maximum;
        return bytes;
    }
    bool hasError() override { calls.append(QStringLiteral("hasError")); return error_result; }
    QString errorString() override { calls.append(QStringLiteral("errorString")); return failure; }
};

class ScriptedFileWriter final : public psbtwork::FileWriter
{
public:
    bool open_result{true}, commit_result{true};
    std::optional<qint64> write_result;
    QByteArray bytes;
    QString failure{QStringLiteral("injected I/O failure")};
    QStringList calls;

    bool open() override { calls.append(QStringLiteral("open")); return open_result; }
    qint64 write(const QByteArray& value) override
    {
        calls.append(QStringLiteral("write"));
        bytes = value;
        return write_result.value_or(value.size());
    }
    void cancel() override { calls.append(QStringLiteral("cancel")); }
    bool commit() override { calls.append(QStringLiteral("commit")); return commit_result; }
    QString errorString() override { calls.append(QStringLiteral("errorString")); return failure; }
};

class EmptyPsbtErrorTranslator final : public QTranslator
{
public:
    explicit EmptyPsbtErrorTranslator(const char* source = "The wallet changed the reviewed transaction.") : m_source{source} {}

    // This in-memory translator contains one entry; Qt skips empty translators.
    bool isEmpty() const override { return false; }

    QString translate(const char* context, const char* source, const char*, int) const override
    {
        if (context && source && std::strcmp(context, "PsbtQmlModel") == 0 &&
            std::strcmp(source, m_source.c_str()) == 0) {
            return QStringLiteral("");
        }
        return {};
    }

private:
    std::string m_source;
};
} // namespace

// Execute is the synchronous Native/Core worker boundary. These tests exercise
// its public requests and effects directly; scheduler timing belongs to the
// manually controlled PsbtQmlModel and wallet-lifecycle tests.
class PsbtWorkTests : public QObject
{
    Q_OBJECT
    std::unique_ptr<ECC_Context> m_ecc;

private Q_SLOTS:
    void initTestCase() { SelectParams(ChainType::MAIN); m_ecc = std::make_unique<ECC_Context>(); }
    void cleanupTestCase() { m_ecc.reset(); }

    void missingDocument_data()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("inspect") << int(psbtwork::Kind::Inspect);
        QTest::newRow("sign") << int(psbtwork::Kind::Sign);
        QTest::newRow("broadcast") << int(psbtwork::Kind::Broadcast);
        QTest::newRow("save") << int(psbtwork::Kind::Save);
        QTest::newRow("clipboard") << int(psbtwork::Kind::Clipboard);
    }
    void missingDocument()
    {
        QFETCH(int, kind);
        WorkWallet wallet;
        psbtwork::Request request;
        request.kind = static_cast<psbtwork::Kind>(kind);
        const auto result = psbtwork::Execute(&wallet, std::move(request), {});
        QCOMPARE(result.code, psbtwork::Error::Invalid);
        QCOMPARE(result.error, QString{"No PSBT is loaded."});
        QVERIFY(!result.snapshot); QVERIFY(!result.submitted); QVERIFY(!result.saved);
        QVERIFY(result.clipboard.isEmpty()); QCOMPARE(wallet.sign_calls, 0); QCOMPARE(wallet.inspect_calls, 0);
    }

    void feeAuthorization_data()
    {
        QTest::addColumn<bool>("broadcast"); QTest::addColumn<int>("fault");
        for (bool broadcast : {false, true}) {
            const auto prefix = broadcast ? QByteArray{"broadcast-"} : QByteArray{"sign-"};
            QTest::newRow(QByteArray(prefix + "no-proof").constData()) << broadcast << 0;
            QTest::newRow(QByteArray(prefix + "no-approved-fee").constData()) << broadcast << 1;
            QTest::newRow(QByteArray(prefix + "different-approved-fee").constData()) << broadcast << 2;
            QTest::newRow(QByteArray(prefix + "reviewed-output-changed").constData()) << broadcast << 3;
        }
    }
    void feeAuthorization()
    {
        QFETCH(bool, broadcast); QFETCH(int, fault);
        WorkWallet wallet;
        auto request = Request(broadcast ? psbtwork::Kind::Broadcast : psbtwork::Kind::Sign);
        if (fault == 0) request.proof.reset();
        if (fault == 1) request.approved_fee.reset();
        if (fault == 2) request.approved_fee = FEE + 1;
        if (fault == 3) {
            auto changed = *request.document;
            changed.outputs[0].amount = 1'499;
            request.document = std::make_shared<const PartiallySignedTransaction>(std::move(changed));
        }
        int broadcasts{0};
        const auto result = psbtwork::Execute(&wallet, std::move(request), {{}, [&](auto) {
            ++broadcasts; return psbtwork::BroadcastResult{true, {}};
        }});
        QCOMPARE(result.code, psbtwork::Error::Fee);
        QCOMPARE(result.error, QString{"The transaction fee is missing or invalid."});
        QCOMPARE(wallet.sign_calls, 0); QCOMPARE(wallet.inspect_calls, 0);
        QCOMPARE(wallet.unlock_calls, 0); QCOMPARE(broadcasts, 0); QVERIFY(!result.snapshot);
    }

    void signingAvailability_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("no-wallet") << 0;
        QTest::newRow("watch-only") << 1;
        QTest::newRow("external-signer") << 2;
    }
    void signingAvailability()
    {
        QFETCH(int, mode);
        WorkWallet wallet;
        wallet.keys_disabled = true; wallet.external_signer = mode == 2;
        const auto result = psbtwork::Execute(mode == 0 ? nullptr : &wallet, Request(psbtwork::Kind::Sign), {});
        QCOMPARE(result.code, mode == 2 ? psbtwork::Error::None : psbtwork::Error::Unavailable);
        QCOMPARE(wallet.sign_calls, mode == 2 ? 1 : 0);
        QCOMPARE(wallet.inspect_calls, mode == 2 ? 1 : 0);
        QCOMPARE(wallet.unlock_calls, 0); QCOMPARE(wallet.lock_calls, 0);
        if (mode != 2) QCOMPARE(result.error, QString{"This wallet cannot sign transactions because private keys are disabled."});
        else { QVERIFY(result.snapshot); QCOMPARE(*result.snapshot->proof->amount, FEE); }
    }

    void passwordAndRelock_data()
    {
        QTest::addColumn<int>("mode"); QTest::addColumn<int>("expected");
        QTest::newRow("already-unlocked") << 0 << int(psbtwork::Error::None);
        QTest::newRow("password-needed") << 1 << int(psbtwork::Error::NeedsUnlock);
        QTest::newRow("wrong-password") << 2 << int(psbtwork::Error::IncorrectPassphrase);
        QTest::newRow("native-password") << 3 << int(psbtwork::Error::None);
        QTest::newRow("captured-password") << 4 << int(psbtwork::Error::None);
        QTest::newRow("relock-failed") << 5 << int(psbtwork::Error::Relock);
        QTest::newRow("relock-threw") << 6 << int(psbtwork::Error::Relock);
        QTest::newRow("wrong-password-relock-failed") << 7 << int(psbtwork::Error::Relock);
    }
    void passwordAndRelock()
    {
        QFETCH(int, mode); QFETCH(int, expected);
        WorkWallet wallet;
        wallet.encrypted = true; wallet.locked = mode != 0;
        wallet.unlock_ok = mode != 2 && mode != 7;
        wallet.lock_ok = mode != 5 && mode != 7; wallet.throw_lock = mode == 6;
        auto request = Request(psbtwork::Kind::Sign);
        ManualRoutedExecution environment;
        const QString password = QString::fromUtf8("signing-\xc3\xa9-password");
        if (mode == 4) {
            auto captured = secretinput::Capture(environment.scheduler, password);
            QVERIFY(captured); request.captured_passphrase = std::move(captured.input);
        } else if (mode >= 2) {
            const auto utf8 = password.toUtf8();
            request.passphrase = SecureString{utf8.constData(), utf8.constData() + utf8.size()};
        }
        const auto result = psbtwork::Execute(&wallet, std::move(request), {});
        QCOMPARE(result.code, static_cast<psbtwork::Error>(expected));
        const bool unlocked = mode != 0 && mode != 1;
        const bool signed_document = mode == 0 || (unlocked && wallet.unlock_ok);
        QCOMPARE(wallet.unlock_calls, unlocked ? 1 : 0);
        QCOMPARE(wallet.lock_calls, unlocked ? 1 : 0);
        QCOMPARE(wallet.sign_calls, signed_document ? 1 : 0);
        QCOMPARE(wallet.inspect_calls, signed_document ? 1 : 0);
        QCOMPARE(bool(result.snapshot), signed_document);
        QCOMPARE(result.relock_failed, mode >= 5);
        if (unlocked) QCOMPARE(wallet.observed_password, password.toUtf8().toStdString());
        if (unlocked && wallet.lock_ok && !wallet.throw_lock) QVERIFY(wallet.locked);
        if (mode == 1) QCOMPARE(result.error, QString{"Enter your wallet password to sign this transaction."});
        if (mode == 2) QCOMPARE(result.error, QString{"The wallet password is incorrect."});
        if (mode >= 5) QCOMPARE(result.error, QString{"The wallet lock could not be restored. Check its security state before retrying."});
        environment.drain();
        QVERIFY(environment.quiescent());
    }

    void passwordMaterializationFailureStartsNoWalletEffect()
    {
        ManualExecutionEnvironment environment;
        auto captured = secretinput::Capture(environment.scheduler, QStringLiteral("original password"));
        QVERIFY(captured);
        WorkWallet wallet;
        wallet.encrypted = wallet.locked = true;
        auto request = Request(psbtwork::Kind::Sign);
        request.captured_passphrase = std::move(captured.input);
        psbtwork::Context context;
        int materializations{0};
        bool input_present{false};
        context.materialize = [&](const secretinput::Captured& input) -> SecureString {
            ++materializations;
            input_present = bool(input);
            throw secretinput::Failure{secretinput::Error::Materialization};
        };
        const auto result = psbtwork::Execute(&wallet, std::move(request), context);
        QCOMPARE(materializations, 1); QVERIFY(input_present);
        QCOMPARE(result.code, psbtwork::Error::Core);
        QCOMPARE(result.error, QString{"The signing password could not be prepared. Signing did not begin."});
        QCOMPARE(wallet.unlock_calls, 0); QCOMPARE(wallet.lock_calls, 0);
        QCOMPARE(wallet.sign_calls, 0); QCOMPARE(wallet.inspect_calls, 0);
        QVERIFY(wallet.locked); QVERIFY(!result.snapshot); QVERIFY(!result.relock_failed);
        environment.drain();
        QVERIFY(environment.quiescent());
    }

    void signerCannotChangeReview_data()
    {
        QTest::addColumn<int>("mutation"); QTest::addColumn<int>("expected");
        QTest::newRow("output") << 0 << int(psbtwork::Error::Changed);
        QTest::newRow("unsigned-transaction-unavailable") << 1 << int(psbtwork::Error::Changed);
        QTest::newRow("conflicting-input-amount") << 2 << int(psbtwork::Error::Fee);
    }
    void signerCannotChangeReview()
    {
        QFETCH(int, mutation); QFETCH(int, expected);
        WorkWallet wallet;
        wallet.sign_effect = [mutation](auto& document) {
            if (mutation == 0) document.outputs[0].amount = 1'499;
            if (mutation == 1) IncompatibleTimeLocks(document);
            if (mutation == 2) document.inputs[0].witness_utxo = CTxOut{1'999, CScript{} << OP_DROP << OP_TRUE};
        };
        auto request = Request(psbtwork::Kind::Sign);
        const auto source = request.document; const auto original = Bytes(*source);
        const auto result = psbtwork::Execute(&wallet, std::move(request), {});
        QCOMPARE(result.code, static_cast<psbtwork::Error>(expected));
        QCOMPARE(result.error, mutation == 2 ? QString{"The transaction fee changed after review."}
            : QString{"The reviewed transaction changed after review."});
        QCOMPARE(wallet.sign_calls, 1); QCOMPARE(wallet.inspect_calls, 0);
        QVERIFY(!result.snapshot); QCOMPARE(Bytes(*source), original);
    }

    void signaturesSurviveLaterFailures_data()
    {
        QTest::addColumn<bool>("sign_error"); QTest::addColumn<bool>("sign_throw");
        QTest::addColumn<bool>("inspect_throw"); QTest::addColumn<int>("expected");
        QTest::newRow("partial-success") << false << false << false << int(psbtwork::Error::None);
        QTest::newRow("signer-reports-error") << true << false << false << int(psbtwork::Error::Core);
        QTest::newRow("signer-throws-after-signature") << false << true << false << int(psbtwork::Error::Uncertain);
        QTest::newRow("inspection-throws-after-signature") << false << false << true << int(psbtwork::Error::Uncertain);
        QTest::newRow("inspection-throws-after-signer-error") << true << false << true << int(psbtwork::Error::Uncertain);
        QTest::newRow("inspection-throws-after-signer-exception") << false << true << true << int(psbtwork::Error::Uncertain);
    }
    void signaturesSurviveLaterFailures()
    {
        QFETCH(bool, sign_error); QFETCH(bool, sign_throw); QFETCH(bool, inspect_throw); QFETCH(int, expected);
        auto fixture = psbtfeefixture::SignedWithoutScripts(false);
        const auto signature = *fixture.document.inputs[0].partial_sigs.begin();
        fixture.document.inputs[0].partial_sigs.clear();
        fixture.document.inputs[0].witness_script = fixture.witness;
        auto request = Request(psbtwork::Kind::Sign, fixture.document);
        const auto source = request.document; const auto original = Bytes(*source);
        auto expected_document = fixture.document;
        expected_document.inputs[0].partial_sigs.insert(signature);
        QVERIFY(!FinalizePSBT(expected_document));
        WorkWallet wallet;
        wallet.signed_inputs = 1;
        if (sign_error) wallet.sign_error = common::PSBTError::EXTERNAL_SIGNER_FAILED;
        wallet.throw_inspect = inspect_throw;
        wallet.sign_effect = [signature, sign_throw](auto& document) {
            document.inputs[0].partial_sigs.insert(signature);
            if (sign_throw) throw std::runtime_error{"signer lost after adding a signature"};
        };
        const auto result = psbtwork::Execute(&wallet, std::move(request), {});
        QCOMPARE(result.code, static_cast<psbtwork::Error>(expected));
        QVERIFY(result.snapshot); QVERIFY(result.snapshot->document);
        QCOMPARE(Bytes(*result.snapshot->document), Bytes(expected_document));
        QVERIFY(Bytes(*result.snapshot->document) != original);
        QCOMPARE(result.snapshot->document->inputs[0].partial_sigs.size(), size_t{1});
        QVERIFY(!result.snapshot->complete); QCOMPARE(*result.snapshot->proof->amount, FEE);
        QCOMPARE(wallet.sign_calls, 1); QCOMPARE(wallet.inspect_calls, 1);
        QVERIFY(!result.submitted); QVERIFY(!result.transaction);
        QCOMPARE(Bytes(*source), original);
        if (inspect_throw) QCOMPARE(result.error, QString{"The PSBT action ended without a confirmed outcome. Reconcile before retrying."});
        else if (sign_throw) QCOMPARE(result.error, QString{"The signer ended without a confirmed outcome. The retained PSBT must be reconciled before retrying."});
        else if (sign_error) QCOMPARE(result.error, QString{"Could not sign PSBT: %1"}.arg(
            QString::fromStdString(common::PSBTErrorString(common::PSBTError::EXTERNAL_SIGNER_FAILED).translated)));
        else QVERIFY(result.error.isEmpty());
    }

    void standaloneSignDoesNotInstallUnverifiedContributions_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<int>("outcome");
        for (int kind = 0; kind < 3; ++kind) for (int outcome = 0; outcome < 3; ++outcome)
            QTest::newRow(qPrintable(QStringLiteral("kind-%1-outcome-%2").arg(kind).arg(outcome))) << kind << outcome;
    }
    void standaloneSignDoesNotInstallUnverifiedContributions()
    {
        QFETCH(int, kind);
        QFETCH(int, outcome);
        const auto fixture = psbtfeefixture::Contribution(kind != 2, kind == 1);
        WorkWallet wallet;
        wallet.sign_effect = [returned = fixture.returned, outcome](auto& document) {
            document = returned;
            if (outcome == 2) throw std::runtime_error("signing outcome unavailable after contribution");
        };
        if (outcome == 1) wallet.sign_error = common::PSBTError::EXTERNAL_SIGNER_FAILED;
        const auto result = psbtwork::Execute(&wallet, Request(psbtwork::Kind::Sign, fixture.original), {});
        QCOMPARE(wallet.sign_calls, 1);
        QVERIFY(!result.submitted);
        QVERIFY(!result.transaction);
        if (kind == 0) {
            QVERIFY(result.snapshot);
            QCOMPARE(result.snapshot->document->inputs[0].partial_sigs, fixture.returned.inputs[0].partial_sigs);
        } else {
            QVERIFY(!result.snapshot || result.snapshot->document->inputs[0].partial_sigs.empty());
        }
    }
    void successfulSigningRetainsFinalizedDocument()
    {
        auto fixture = psbtfeefixture::SignedWithoutScripts(false);
        fixture.document.inputs[0].witness_script = fixture.witness;
        const auto signatures = fixture.document.inputs[0].partial_sigs;
        auto expected = fixture.document;
        QVERIFY(FinalizePSBT(expected));
        fixture.document.inputs[0].partial_sigs.clear();
        WorkWallet wallet; wallet.signed_inputs = 1;
        wallet.sign_effect = [signatures](auto& document) { document.inputs[0].partial_sigs = signatures; };
        const auto result = psbtwork::Execute(&wallet, Request(psbtwork::Kind::Sign, fixture.document), {});
        QCOMPARE(result.code, psbtwork::Error::None); QVERIFY(result.error.isEmpty());
        QVERIFY(result.snapshot); QVERIFY(result.snapshot->complete); QVERIFY(result.snapshot->can_broadcast);
        QCOMPARE(Bytes(*result.snapshot->document), Bytes(expected));
        QCOMPARE(result.snapshot->status, QString{"PSBT signed. Transaction is ready for broadcast."});
        QCOMPARE(wallet.sign_calls, 1); QCOMPARE(wallet.inspect_calls, 1);
        QCOMPARE(wallet.options.size(), size_t{2});
        QVERIFY(wallet.options[0].first); QVERIFY(wallet.options[0].second);
        QVERIFY(!wallet.options[1].first); QVERIFY(!wallet.options[1].second);
    }

    void clipboardPreservesExactDocument_data()
    {
        QTest::addColumn<bool>("known_fee");
        QTest::newRow("known-fee") << true; QTest::newRow("unverified-fee") << false;
    }
    void clipboardPreservesExactDocument()
    {
        QFETCH(bool, known_fee);
        auto document = Document();
        document.unknown[{0x80, 0x01}] = {0x00, 0xff, 0x20};
        if (!known_fee) document.inputs[0].non_witness_utxo = nullptr;
        const auto raw = Bytes(document);
        WorkWallet wallet;
        const auto result = psbtwork::Execute(&wallet, Request(psbtwork::Kind::Clipboard, document), {});
        QCOMPARE(result.code, psbtwork::Error::None); QVERIFY(result.error.isEmpty());
        QCOMPARE(result.clipboard, QString::fromLatin1(raw.toBase64()));
        QCOMPARE(QByteArray::fromBase64(result.clipboard.toLatin1()), raw);
        QCOMPARE(wallet.sign_calls, 0); QCOMPARE(wallet.inspect_calls, 0); QVERIFY(!result.snapshot);
    }

    void filesystemActions_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("save-raw") << 0; QTest::newRow("save-file-url") << 1;
        QTest::newRow("save-replaces-existing") << 2; QTest::newRow("save-missing-parent") << 3;
        QTest::newRow("save-directory") << 4; QTest::newRow("load-raw") << 5;
        QTest::newRow("load-base64-file-url") << 6; QTest::newRow("load-missing") << 7;
        QTest::newRow("load-invalid") << 8; QTest::newRow("load-too-large") << 9;
        QTest::newRow("load-base64-decoded-invalid-psbt") << 10;
    }
    void filesystemActions()
    {
        QFETCH(int, mode);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto document = Document(); const auto raw = Bytes(document);
        QString path = directory.filePath("document.psbt");
        if (mode == 3) path = directory.filePath("missing/document.psbt");
        if (mode == 4) path = directory.path();
        if (mode == 2 || mode == 5 || mode == 6 || mode == 8 || mode == 9 || mode == 10) {
            QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly));
            QByteArray payload{"invalid PSBT"};
            if (mode == 5) payload = raw;
            if (mode == 6) payload = QByteArray{" \n"} + raw.toBase64() + "\n ";
            if (mode == 10) payload = QByteArray{"psbt\xff", 5}.toBase64();
            QCOMPARE(file.write(payload), qint64{payload.size()});
            if (mode == 9) QVERIFY(file.resize(MAX_FILE_SIZE_PSBT + 1));
        }
        auto request = Request(mode >= 5 ? psbtwork::Kind::Load : psbtwork::Kind::Save, document);
        request.path = mode == 1 || mode == 6 ? QUrl::fromLocalFile(path).toString() : path;
        const auto requested_path = request.path;
        WorkWallet wallet;
        // Read-only load does not need a wallet; filesystem actions never sign.
        const auto result = psbtwork::Execute(mode >= 5 ? nullptr : &wallet, std::move(request), {});
        QCOMPARE(result.path, requested_path); QCOMPARE(wallet.sign_calls, 0); QCOMPARE(wallet.inspect_calls, 0);
        const bool success = mode <= 2 || mode == 5 || mode == 6;
        QCOMPARE(result.code, success ? psbtwork::Error::None : mode >= 5 ? psbtwork::Error::Invalid : psbtwork::Error::Core);
        QCOMPARE(result.saved, mode <= 2);
        if (mode <= 2) {
            QFile file{path}; QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), raw);
        } else if (mode == 5 || mode == 6) {
            QVERIFY(result.snapshot); QCOMPARE(Bytes(*result.snapshot->document), raw);
            QCOMPARE(*result.snapshot->proof->amount, FEE);
        } else {
            QVERIFY(!result.error.isEmpty()); QVERIFY(!result.snapshot);
            if (mode == 9) QCOMPARE(result.error, QString{"PSBT file must be smaller than 100 MiB"});
            if (mode == 3 || mode == 4) QVERIFY(result.error.startsWith("Could not save PSBT: "));
            if (mode == 7) QVERIFY(result.error.startsWith("Could not open PSBT file: "));
            if (mode == 8 || mode == 10) QVERIFY(result.error.startsWith("Could not decode PSBT: "));
            if (mode == 10) QVERIFY(!result.error.contains("Invalid PSBT magic bytes"));
        }
    }

    void injectedReaderHonorsLimitsAndOutcomes_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("open-failure") << 0;
        QTest::newRow("initial-size-limit") << 1;
        QTest::newRow("bounded-read-over-limit") << 2;
        QTest::newRow("read-error-before-decode") << 3;
        QTest::newRow("raw-psbt-success") << 4;
        QTest::newRow("initial-limit-shrinks-before-read") << 5;
    }
    void injectedReaderHonorsLimitsAndOutcomes()
    {
        QFETCH(int, mode);
        const auto expected_document = Document();
        const auto expected_bytes = Bytes(expected_document);
        auto destination = Document(true);
        const auto original_destination = Bytes(destination);
        const qint64 maximum_read = static_cast<qint64>(MAX_FILE_SIZE_PSBT + 1);
        ScriptedFileReader reader;
        reader.bytes = expected_bytes;

        if (mode == 0) {
            reader.open_result = false;
            reader.failure = QStringLiteral("injected open failure");
        } else if (mode == 1) {
            reader.size_result = maximum_read;
        } else if (mode == 2) {
            reader.size_result = static_cast<qint64>(MAX_FILE_SIZE_PSBT);
            reader.bytes = QByteArray{static_cast<qsizetype>(maximum_read), '\0'};
        } else if (mode == 3 || mode == 4) {
            reader.size_result = expected_bytes.size();
            reader.error_result = mode == 3;
            reader.failure = QStringLiteral("injected read failure");
        } else if (mode == 5) {
            reader.size_result = static_cast<qint64>(MAX_FILE_SIZE_PSBT);
        }

        const auto error = psbtwork::ReadFile(reader, destination);
        QStringList expected_calls;
        QString expected_error;
        if (mode == 0) {
            expected_calls = {QStringLiteral("open"), QStringLiteral("errorString")};
            expected_error = QStringLiteral("Could not open PSBT file: injected open failure");
        } else if (mode == 1) {
            expected_calls = {QStringLiteral("open"), QStringLiteral("initialSize")};
            expected_error = QStringLiteral("PSBT file must be smaller than 100 MiB");
        } else if (mode == 2) {
            expected_calls = {QStringLiteral("open"), QStringLiteral("initialSize"), QStringLiteral("read")};
            expected_error = QStringLiteral("PSBT file must be smaller than 100 MiB");
        } else if (mode == 3) {
            expected_calls = {QStringLiteral("open"), QStringLiteral("initialSize"), QStringLiteral("read"),
                QStringLiteral("hasError"), QStringLiteral("errorString")};
            expected_error = QStringLiteral("Could not read PSBT file: injected read failure");
        } else {
            expected_calls = {QStringLiteral("open"), QStringLiteral("initialSize"), QStringLiteral("read"),
                QStringLiteral("hasError")};
        }

        QCOMPARE(error, expected_error);
        QCOMPARE(reader.calls, expected_calls);
        if (mode >= 2) QCOMPARE(reader.read_limit, maximum_read);
        if (mode == 4 || mode == 5) QCOMPARE(Bytes(destination), expected_bytes);
        else QCOMPARE(Bytes(destination), original_destination);
    }

    void injectedWriterHonorsCallOrderAndOutcome_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("open-failure") << 0;
        QTest::newRow("short-write-cancels") << 1;
        QTest::newRow("commit-failure") << 2;
        QTest::newRow("commit-success") << 3;
        QTest::newRow("negative-write-cancels") << 4;
    }
    void injectedWriterHonorsCallOrderAndOutcome()
    {
        QFETCH(int, mode);
        const auto document = Document();
        const auto expected_bytes = psbtwork::SerializeRaw(document);
        ScriptedFileWriter writer;
        if (mode == 0) {
            writer.open_result = false;
            writer.failure = QStringLiteral("injected open failure");
        } else if (mode == 1) {
            writer.write_result = expected_bytes.size() - 1;
        } else if (mode == 2) {
            writer.commit_result = false;
            writer.failure = QStringLiteral("injected commit failure");
        } else if (mode == 4) {
            writer.write_result = -1;
        }

        const auto error = psbtwork::WriteFile(writer, document);
        QStringList expected_calls;
        QString expected_error;
        if (mode == 0) {
            expected_calls = {QStringLiteral("open"), QStringLiteral("errorString")};
            expected_error = QStringLiteral("Could not save PSBT: injected open failure");
        } else if (mode == 1 || mode == 4) {
            expected_calls = {QStringLiteral("open"), QStringLiteral("write"), QStringLiteral("cancel")};
            expected_error = QStringLiteral("Could not write the complete PSBT file.");
        } else if (mode == 2) {
            expected_calls = {QStringLiteral("open"), QStringLiteral("write"), QStringLiteral("commit"),
                QStringLiteral("errorString")};
            expected_error = QStringLiteral("Could not save PSBT: injected commit failure");
        } else {
            expected_calls = {QStringLiteral("open"), QStringLiteral("write"), QStringLiteral("commit")};
        }

        QCOMPARE(error, expected_error);
        QCOMPARE(writer.calls, expected_calls);
        QCOMPARE(writer.bytes, mode == 0 ? QByteArray{} : expected_bytes);
        QCOMPARE(writer.calls.contains(QStringLiteral("commit")), mode == 2 || mode == 3);
        QCOMPARE(writer.calls.contains(QStringLiteral("cancel")), mode == 1 || mode == 4);
    }

    void seededInspectionPreservesFeeAuthority_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("adds-nonfee-metadata") << 0;
        QTest::newRow("rejects-changed-prevout-amount") << 1;
    }
    void seededInspectionPreservesFeeAuthority()
    {
        QFETCH(int, mode);
        auto document = Document();
        auto request = Request(psbtwork::Kind::Inspect, document);
        if (mode == 1) {
            const auto prevout = document.inputs[0].non_witness_utxo->vout[0];
            document.inputs[0].non_witness_utxo = nullptr;
            document.inputs[0].witness_utxo = prevout;
            request.document = std::make_shared<const PartiallySignedTransaction>(std::move(document));
        }
        const auto source = request.document;
        const auto original = Bytes(*source);
        WorkWallet wallet;
        wallet.inspect_effect = [mode](auto& candidate) {
            if (mode == 0) candidate.inputs[0].unknown[{0x7f, 0x01}] = {0xca, 0xfe};
            else candidate.inputs[0].witness_utxo.nValue = 1'999;
        };

        const auto result = psbtwork::Execute(&wallet, std::move(request), {});
        QCOMPARE(result.code, psbtwork::Error::None);
        QVERIFY(result.snapshot); QVERIFY(result.snapshot->document == source);
        QCOMPARE(Bytes(*source), original);
        QCOMPARE(wallet.inspect_calls, 1); QCOMPARE(wallet.sign_calls, 0);
        QVERIFY(result.snapshot->proof); QVERIFY(result.snapshot->proof->amount);
        QCOMPARE(*result.snapshot->proof->amount, FEE);
        if (mode == 0) {
            QVERIFY(result.snapshot->error.isEmpty()); QVERIFY(!result.snapshot->metadata_failed);
            QVERIFY(result.snapshot->can_sign); QCOMPARE(result.snapshot->could_sign, 1);
        } else {
            QCOMPARE(result.snapshot->error, QString{"The transaction fee changed after review."});
            QVERIFY(result.snapshot->metadata_failed); QVERIFY(!result.snapshot->can_sign);
            QCOMPARE(result.snapshot->could_sign, 0);
        }
    }

    void inspectionSummaryReportsCoreAnalysisError()
    {
        auto document = Document();
        auto previous = CMutableTransaction{*document.inputs[0].non_witness_utxo};
        previous.vout[0].scriptPubKey = CScript{} << OP_RETURN;
        document.inputs[0].non_witness_utxo = MakeTransactionRef(previous);
        document.inputs[0].prev_txid = document.inputs[0].non_witness_utxo->GetHash();
        WorkWallet wallet;
        const auto result = psbtwork::Execute(&wallet, Request(psbtwork::Kind::Inspect, document), {});

        QCOMPARE(result.code, psbtwork::Error::None);
        QVERIFY(result.snapshot); QVERIFY(result.snapshot->error.isEmpty());
        QVERIFY(result.snapshot->proof->amount); QCOMPARE(*result.snapshot->proof->amount, FEE);
        QVERIFY(result.snapshot->initial_summary_page.rows.contains(
            QString{"Analysis: PSBT is not valid. Input 0 spends unspendable output"}));
        QCOMPARE(wallet.inspect_calls, 1);
    }

    void largeInspectionSummaryKeepsOnlyBoundedFormattedPages()
    {
        constexpr size_t OUTPUTS{4096};
        WorkWallet wallet;
        wallet.owned_outputs = true;
        const auto result = psbtwork::Execute(&wallet, Request(psbtwork::Kind::Inspect, ManyOutputDocument(OUTPUTS)), {});
        QCOMPARE(result.code, psbtwork::Error::None);
        QVERIFY(result.snapshot);
        const auto& snapshot = *result.snapshot;
        QVERIFY(snapshot.summary_source);
        QCOMPARE(snapshot.summary_count, snapshot.summary_source->row_count);
        QCOMPARE(snapshot.summary_source->owned_outputs.size(), static_cast<int>(OUTPUTS));
        QCOMPARE(snapshot.summary_source->owned_outputs.count(true), static_cast<int>(OUTPUTS));
        QCOMPARE(snapshot.initial_summary_page.total, snapshot.summary_count);
        QCOMPARE(snapshot.initial_summary_page.offset, 0);
        QCOMPARE(snapshot.initial_summary_page.rows.size(), 128);

        QStringList all_rows;
        for (int offset{0}; offset < snapshot.summary_count; offset += 128) {
            const auto page = psbtwork::ProjectSummaryPage(snapshot.summary_source, offset, 128, 17, static_cast<quint64>(offset / 128 + 1));
            QVERIFY2(page.error.isEmpty(), qPrintable(page.error));
            QCOMPARE(page.generation, quint64{17});
            QCOMPARE(page.request, static_cast<quint64>(offset / 128 + 1));
            QCOMPARE(page.total, snapshot.summary_count);
            QVERIFY(page.rows.size() <= 128);
            all_rows.append(page.rows);
        }
        QCOMPARE(all_rows.size(), snapshot.summary_count);
        QCOMPARE(all_rows.first(), snapshot.initial_summary_page.rows.first());
        QVERIFY(all_rows.first().startsWith(QStringLiteral("Sends ")));
        QVERIFY(all_rows.first().contains(QStringLiteral("(own address)")));
        QVERIFY(std::any_of(all_rows.begin(), all_rows.end(), [](const QString& row) {
            return row.startsWith(QStringLiteral("Total output amount: "));
        }));
    }

    void translatedMetadataFailureCannotGrantBroadcastAuthority()
    {
        EmptyPsbtErrorTranslator translator;
        QVERIFY(!translator.isEmpty());
        QVERIFY(QCoreApplication::installTranslator(&translator));
        struct RestoreTranslator {
            QTranslator& translator;
            ~RestoreTranslator() { QCoreApplication::removeTranslator(&translator); }
        } restore{translator};
        const auto translation = QCoreApplication::translate(
            "PsbtQmlModel", "The wallet changed the reviewed transaction.");
        QVERIFY(translation.isEmpty());
        QVERIFY(!translation.isNull());

        const auto document = Document(true);
        auto request = Request(psbtwork::Kind::Inspect, document);
        const auto source = request.document;
        const auto original = Bytes(*source);
        WorkWallet wallet;
        wallet.inspect_effect = [](auto& candidate) { candidate.outputs[0].amount = 1'499; };

        const auto result = psbtwork::Execute(&wallet, std::move(request), {});
        QCOMPARE(result.code, psbtwork::Error::None);
        QVERIFY(result.snapshot); QVERIFY(result.snapshot->complete);
        QVERIFY(result.snapshot->metadata_failed); QVERIFY(result.snapshot->error.isEmpty());
        QVERIFY(result.snapshot->proof); QVERIFY(result.snapshot->proof->amount);
        QCOMPARE(*result.snapshot->proof->amount, FEE);
        QVERIFY(result.snapshot->document == source); QCOMPARE(Bytes(*source), original);
        QCOMPARE(wallet.inspect_calls, 1); QCOMPARE(wallet.sign_calls, 0);
        const bool ready_status = result.snapshot->status ==
            QStringLiteral("Transaction is fully signed and ready for broadcast.");
        QVERIFY2(!result.snapshot->can_broadcast && !ready_status,
            qPrintable(QStringLiteral("typed metadata failure was presented as authorized: can_broadcast=%1, status=%2")
                .arg(result.snapshot->can_broadcast).arg(result.snapshot->status)));
    }

    void metadataFailureOwnsStatusOverCallerSuccess_data()
    {
        QTest::addColumn<bool>("complete");
        QTest::newRow("incomplete") << false;
        QTest::newRow("complete") << true;
    }
    void metadataFailureOwnsStatusOverCallerSuccess()
    {
        QFETCH(bool, complete);
        auto request = Request(psbtwork::Kind::Inspect, Document(complete));
        request.status = QStringLiteral("Previously reported success");
        const auto source = request.document;
        const auto original = Bytes(*source);
        WorkWallet wallet;
        wallet.inspect_effect = [](auto& candidate) { candidate.outputs[0].amount = 1'499; };
        const auto result = psbtwork::Execute(&wallet, std::move(request), {});
        QCOMPARE(result.code, psbtwork::Error::None);
        QVERIFY(result.snapshot); QVERIFY(result.snapshot->metadata_failed);
        QCOMPARE(result.snapshot->complete, complete);
        QCOMPARE(result.snapshot->error, QStringLiteral("The wallet changed the reviewed transaction."));
        QCOMPARE(result.snapshot->status, result.snapshot->error);
        QVERIFY(!result.snapshot->can_sign); QVERIFY(!result.snapshot->can_broadcast);
        QCOMPARE(result.snapshot->could_sign, 0);
        QVERIFY(result.snapshot->document == source); QCOMPARE(Bytes(*source), original);
        QVERIFY(result.snapshot->proof); QVERIFY(result.snapshot->proof->amount);
        QCOMPARE(*result.snapshot->proof->amount, FEE);
        QCOMPARE(wallet.inspect_calls, 1); QCOMPARE(wallet.sign_calls, 0);
    }

    void translatedMissingFeeCannotExposeCallerSuccess()
    {
        EmptyPsbtErrorTranslator translator{"The transaction fee is missing or invalid."};
        QVERIFY(QCoreApplication::installTranslator(&translator));
        const auto restore = qScopeGuard([&] { QCoreApplication::removeTranslator(&translator); });
        const auto translation = QCoreApplication::translate("PsbtQmlModel", "The transaction fee is missing or invalid.");
        QVERIFY(translation.isEmpty()); QVERIFY(!translation.isNull());
        auto document = Document();
        document.inputs[0].non_witness_utxo = nullptr;
        auto request = Request(psbtwork::Kind::Inspect, document);
        request.status = QStringLiteral("Previously reported success");
        const auto source = request.document;
        const auto original = Bytes(*source);
        WorkWallet wallet;
        const auto result = psbtwork::Execute(&wallet, std::move(request), {});
        QCOMPARE(result.code, psbtwork::Error::None);
        QVERIFY(result.snapshot); QVERIFY(!result.snapshot->metadata_failed);
        QVERIFY(!result.snapshot->complete); QCOMPARE(result.snapshot->could_sign, 1);
        QVERIFY(result.snapshot->error.isEmpty()); QVERIFY(result.snapshot->status.isEmpty());
        QVERIFY(result.snapshot->proof); QVERIFY(!result.snapshot->proof->amount);
        QVERIFY(!result.snapshot->can_sign); QVERIFY(!result.snapshot->can_broadcast);
        QVERIFY(result.snapshot->document == source); QCOMPARE(Bytes(*source), original);
        QCOMPARE(wallet.inspect_calls, 1); QCOMPARE(wallet.sign_calls, 0);
    }

    void malformedMetadataCannotConferAuthority_data()
    {
        QTest::addColumn<bool>("invalid_time_locks");
        QTest::newRow("incompatible-time-locks") << true;
        QTest::newRow("negative-output") << false;
    }
    void malformedMetadataCannotConferAuthority()
    {
        QFETCH(bool, invalid_time_locks);
        auto request = Request(psbtwork::Kind::Inspect, Document(true));
        const auto source = request.document;
        const auto original = Bytes(*source);
        WorkWallet wallet;
        // A public metadata provider may return a malformed candidate without
        // finalizing it. Exercise the consumer's own validation and authority.
        wallet.finalize_inspection = false;
        wallet.inspect_effect = [=](auto& candidate) {
            if (invalid_time_locks) IncompatibleTimeLocks(candidate);
            else candidate.outputs[0].amount = -1;
        };
        const auto result = psbtwork::Execute(&wallet, std::move(request), {});
        QCOMPARE(result.code, psbtwork::Error::None);
        QVERIFY(result.snapshot); QVERIFY(result.snapshot->metadata_failed);
        QVERIFY(result.snapshot->complete); QVERIFY(!result.snapshot->can_broadcast);
        QVERIFY(!result.snapshot->can_sign); QCOMPARE(result.snapshot->could_sign, 0);
        QCOMPARE(result.snapshot->error, QStringLiteral("The transaction fee is missing or invalid."));
        QCOMPARE(result.snapshot->status, result.snapshot->error);
        QVERIFY(result.snapshot->document == source); QCOMPARE(Bytes(*source), original);
        QVERIFY(result.snapshot->proof); QVERIFY(result.snapshot->proof->amount);
        QCOMPARE(*result.snapshot->proof->amount, FEE);
        QCOMPARE(wallet.inspect_calls, 1); QCOMPARE(wallet.sign_calls, 0);
    }

    void exportWithoutOptionalProofPreservesSource()
    {
        WorkWallet wallet;
        const auto source = std::make_shared<const PartiallySignedTransaction>(Document());
        const auto original = Bytes(*source);
        const auto result = psbtwork::ExportReview(wallet, source, MakeTransactionRef(*source->GetUnsignedTx()), false);
        QVERIFY(result.document); QVERIFY(result.error.isEmpty());
        QCOMPARE(Bytes(*result.document), original); QCOMPARE(Bytes(*source), original);
        QCOMPARE(wallet.inspect_calls, 0); QCOMPARE(wallet.sign_calls, 0);
    }

    void publicSignerCapabilityControlsIncompleteReview_data()
    {
        QTest::addColumn<bool>("external_signer");
        QTest::newRow("watch-only") << false;
        QTest::newRow("external-signer") << true;
    }
    void publicSignerCapabilityControlsIncompleteReview()
    {
        QFETCH(bool, external_signer);
        auto document = Document();
        document.outputs[0].script = GetScriptForDestination(DecodeDestination("1BoatSLRHtKNngkdXEeobR76b53LETtpyT"));
        const auto source = std::make_shared<const PartiallySignedTransaction>(document);
        const auto original = Bytes(*source);
        WorkWallet wallet;
        wallet.keys_disabled = true; wallet.external_signer = external_signer; wallet.owned_inputs = true;
        const auto result = psbtwork::PrepareReview(wallet, source, {});
        QCOMPARE(result.disposition, psbtwork::ImportResult::Disposition::Review);
        QVERIFY(result.review); QVERIFY(result.document);
        QCOMPARE(result.review->can_send, external_signer); QCOMPARE(result.key_can_send, external_signer);
        QVERIFY(result.can_commit_complete); QVERIFY(!result.key_can_broadcast);
        QCOMPARE(result.review->amount, CAmount{1'500}); QCOMPARE(result.review->fee, FEE);
        QCOMPARE(Bytes(*result.document), original); QCOMPARE(Bytes(*source), original);
        QCOMPARE(wallet.inspect_calls, 1); QCOMPARE(wallet.sign_calls, 0); QCOMPARE(wallet.unlock_calls, 0);
    }

    void savedDocumentAndOutcomeHaveIndependentOwnership_data()
    {
        QTest::addColumn<int>("outcome");
        QTest::newRow("confirmed") << 0;
        QTest::newRow("rejected-before-write") << 1;
        QTest::newRow("committed-before-reporting-exception") << 2;
    }
    void savedDocumentAndOutcomeHaveIndependentOwnership()
    {
        QFETCH(int, outcome);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = directory.filePath("owned-document.psbt");
        auto request = Request(psbtwork::Kind::Save);
        request.path = path;
        const auto original = Bytes(*request.document);
        psbtwork::Context context;
        int saves{0}; bool original_document{false}; QString observed_path;
        context.save = [&](const PartiallySignedTransaction& document, const QString& destination) -> QString {
            ++saves; original_document = Bytes(document) == original; observed_path = destination;
            if (outcome == 1) return QStringLiteral("storage unavailable before write");
            auto error = psbtwork::SaveFile(document, destination);
            if (!error.isEmpty()) return error;
            if (outcome == 2) throw std::runtime_error{"save result unavailable after commit"};
            return {};
        };
        WorkWallet wallet;
        const auto result = psbtwork::Execute(&wallet, std::move(request), context);
        QCOMPARE(saves, 1); QVERIFY(original_document); QCOMPARE(observed_path, path);
        QCOMPARE(result.code, outcome == 0 ? psbtwork::Error::None : outcome == 1 ? psbtwork::Error::Core : psbtwork::Error::Uncertain);
        QCOMPARE(result.saved, outcome == 0); QCOMPARE(result.path, path);
        QCOMPARE(wallet.sign_calls, 0); QCOMPARE(wallet.inspect_calls, 0); QCOMPARE(wallet.unlock_calls, 0);
        QVERIFY(!result.snapshot); QVERIFY(!result.submitted);
        if (outcome == 1) {
            QVERIFY(!QFileInfo::exists(path)); QCOMPARE(result.error, QString{"storage unavailable before write"});
        } else {
            QFile persisted{path}; QVERIFY(persisted.open(QIODevice::ReadOnly)); QCOMPARE(persisted.readAll(), original);
            if (outcome == 2) QCOMPARE(result.error, QString{"The PSBT action ended without a confirmed outcome. Reconcile before retrying."});
        }
    }

    void broadcastUsesOnlyCapturedAssessment_data()
    {
        QTest::addColumn<int>("mode"); QTest::addColumn<int>("expected");
        QTest::newRow("native-witness-assessment") << 0 << int(psbtwork::Error::None);
        QTest::newRow("nested-witness-assessment") << 1 << int(psbtwork::Error::None);
        QTest::newRow("missing-assessment") << 2 << int(psbtwork::Error::Changed);
        QTest::newRow("changed-output") << 3 << int(psbtwork::Error::Changed);
        QTest::newRow("incompatible-time-locks") << 4 << int(psbtwork::Error::Changed);
        QTest::newRow("conflicting-fee") << 5 << int(psbtwork::Error::Fee);
        QTest::newRow("incomplete-assessment") << 6 << int(psbtwork::Error::Invalid);
        QTest::newRow("invalid-final-signature") << 7 << int(psbtwork::Error::Invalid);
        QTest::newRow("complete-without-wallet") << 8 << int(psbtwork::Error::None);
    }
    void broadcastUsesOnlyCapturedAssessment()
    {
        QFETCH(int, mode); QFETCH(int, expected);
        const auto fixture = psbtfeefixture::SignedWithoutScripts(mode == 1);
        auto assessed = fixture.document;
        assessed.inputs[0].witness_script = fixture.witness;
        assessed.inputs[0].redeem_script = fixture.redeem;
        CMutableTransaction expected_transaction;
        QVERIFY(FinalizeAndExtractPSBT(assessed, expected_transaction));
        auto request = Request(psbtwork::Kind::Broadcast, fixture.document);
        const auto source = request.document; const auto original = Bytes(*source);
        if (mode == 3) assessed.outputs[0].amount = 1'499;
        if (mode == 4) IncompatibleTimeLocks(assessed);
        if (mode == 5) assessed.inputs[0].witness_utxo.nValue = 1'999;
        if (mode == 6) assessed = fixture.document;
        if (mode == 7) { QVERIFY(assessed.inputs[0].final_script_witness.stack.size() > 1); assessed.inputs[0].final_script_witness.stack[1][0] ^= 1; }
        request.assessment = mode == 2 ? nullptr : std::make_shared<const PartiallySignedTransaction>(std::move(assessed));
        WorkWallet wallet; wallet.throw_inspect = true;
        int broadcasts{0}; CTransactionRef submitted;
        const auto result = psbtwork::Execute(mode == 8 ? nullptr : &wallet, std::move(request), {{}, [&](auto transaction) {
            ++broadcasts; submitted = std::move(transaction); return psbtwork::BroadcastResult{true, {}};
        }});
        QCOMPARE(result.code, static_cast<psbtwork::Error>(expected));
        QCOMPARE(wallet.sign_calls, 0); QCOMPARE(wallet.inspect_calls, 0);
        QVERIFY(wallet.options.empty());
        const bool accepted = mode <= 1 || mode == 8;
        QCOMPARE(broadcasts, accepted ? 1 : 0); QCOMPARE(result.submitted, accepted);
        QCOMPARE(bool(result.transaction), accepted); QVERIFY(!result.snapshot);
        QCOMPARE(Bytes(*source), original);
        if (accepted) {
            QVERIFY(submitted); QCOMPARE(submitted->GetWitnessHash(), MakeTransactionRef(expected_transaction)->GetWitnessHash());
            QCOMPARE(result.txid, QString::fromStdString(expected_transaction.GetHash().ToString()));
        } else { QVERIFY(!submitted); QVERIFY(result.txid.isEmpty()); QVERIFY(!result.error.isEmpty()); }
    }

    void broadcastEffectOutcome_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("accepted") << 0; QTest::newRow("rejected") << 1;
        QTest::newRow("throws") << 2; QTest::newRow("node-unavailable") << 3;
    }
    void broadcastEffectOutcome()
    {
        QFETCH(int, mode);
        auto document = Document(true);
        auto copy = document; CMutableTransaction extracted;
        QVERIFY(FinalizeAndExtractPSBT(copy, extracted));
        psbtwork::Context context;
        int calls{0}; CTransactionRef observed;
        if (mode != 3) context.broadcast = [&](auto transaction) {
            ++calls; observed = std::move(transaction);
            if (mode == 2) throw std::runtime_error{"submission outcome unavailable"};
            return psbtwork::BroadcastResult{mode == 0, QString{"policy rejection"}};
        };
        const auto result = psbtwork::Execute(nullptr, Request(psbtwork::Kind::Broadcast, document), context);
        const auto expected = mode == 0 ? psbtwork::Error::None : mode == 1 ? psbtwork::Error::Core
            : mode == 2 ? psbtwork::Error::Uncertain : psbtwork::Error::Unavailable;
        QCOMPARE(result.code, expected); QCOMPARE(calls, mode == 3 ? 0 : 1);
        QCOMPARE(result.submitted, mode == 0); QCOMPARE(bool(result.transaction), mode == 0);
        QCOMPARE(result.txid, mode == 3 ? QString{} : QString::fromStdString(extracted.GetHash().ToString()));
        if (mode != 3) { QVERIFY(observed); QCOMPARE(observed->GetHash(), extracted.GetHash()); }
        if (mode == 1) QCOMPARE(result.error, QString{"Transaction broadcast failed: policy rejection"});
        if (mode == 2) QCOMPARE(result.error, QString{"The PSBT action ended without a confirmed outcome. Reconcile before retrying."});
        if (mode == 3) QCOMPARE(result.error, QString{"The node interface is unavailable."});
    }

    void inspectionExceptionHasNoActionOutcome()
    {
        WorkWallet wallet; wallet.throw_inspect = true;
        const auto result = psbtwork::Execute(&wallet, Request(psbtwork::Kind::Inspect), {});
        QCOMPARE(result.code, psbtwork::Error::Core);
        QCOMPARE(result.error, QString{"The PSBT could not be processed."});
        QVERIFY(!result.snapshot); QVERIFY(!result.transaction); QVERIFY(!result.submitted);
        QCOMPARE(wallet.sign_calls, 0); QCOMPARE(wallet.inspect_calls, 1);
    }

    void exportReviewOwnsOriginalDocument_data()
    {
        QTest::addColumn<int>("mode");
        const std::array names{"no-transaction", "source-only", "reviewed-only", "source-and-review",
            "malformed-source", "different-review", "fee-before-fill", "metadata-success",
            "metadata-error", "metadata-changes-output", "metadata-removes-unsigned",
            "metadata-changes-fee", "metadata-throws", "readonly-without-approved-fee"};
        for (int mode = 0; mode < int(names.size()); ++mode) QTest::newRow(names[mode]) << mode;
    }
    void exportReviewOwnsOriginalDocument()
    {
        QFETCH(int, mode);
        WorkWallet wallet;
        auto document = Document(true);
        const auto proof = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(document));
        const auto approved = *document.GetUnsignedTx();
        auto transaction = approved;
        if (mode == 4) IncompatibleTimeLocks(document);
        if (mode == 5) transaction.vout[0].nValue = 1'499;
        const auto source = mode == 0 || mode == 2 ? nullptr : std::make_shared<const PartiallySignedTransaction>(document);
        const auto original = source ? Bytes(*source) : QByteArray{};
        const auto reviewed = mode == 2 || mode == 3 || mode == 5 ? MakeTransactionRef(transaction) : CTransactionRef{};
        const bool fill = mode >= 6 && mode <= 12;
        if (mode == 8) wallet.inspect_error = common::PSBTError::EXTERNAL_SIGNER_FAILED;
        wallet.throw_inspect = mode == 12;
        wallet.inspect_effect = [mode](auto& candidate) {
            if (mode == 9) candidate.outputs[0].amount = 1'499;
            if (mode == 10) IncompatibleTimeLocks(candidate);
            if (mode == 11) candidate.inputs[0].witness_utxo = CTxOut{1'999,
                candidate.inputs[0].non_witness_utxo->vout[0].scriptPubKey};
        };
        const auto result = psbtwork::ExportReview(wallet, source, reviewed, fill, proof,
            mode == 13 ? std::optional<CAmount>{} : std::optional<CAmount>{mode == 6 ? FEE + 1 : FEE});
        const bool success = mode == 1 || mode == 2 || mode == 3 || mode == 7 || mode == 13;
        QCOMPARE(bool(result.document), success);
        QCOMPARE(wallet.inspect_calls, mode >= 7 && mode <= 12 ? 1 : 0);
        QCOMPARE(wallet.sign_calls, 0); QCOMPARE(wallet.unlock_calls, 0); QCOMPARE(wallet.lock_calls, 0);
        if (wallet.inspect_calls) QCOMPARE(wallet.options, (std::vector<std::pair<bool,bool>>{{false,true}}));
        if (source) QCOMPARE(Bytes(*source), original);
        if (success) {
            QVERIFY(result.error.isEmpty());
            QVERIFY(MatchesUnsignedTransaction(approved, *result.document->GetUnsignedTx()));
            if (mode == 2) {
                const PartiallySignedTransaction unsigned_document{approved, 2};
                QCOMPARE(Bytes(*result.document), Bytes(unsigned_document));
                QVERIFY(result.document->inputs[0].final_script_sig.empty());
            } else QCOMPARE(Bytes(*result.document), original);
        } else {
            QString expected;
            if (mode == 0) expected = QStringLiteral("No transaction is prepared.");
            else if (mode == 4 || mode == 5) expected = QStringLiteral("The reviewed transaction changed.");
            else if (mode == 6 || mode == 11) expected = QStringLiteral("The transaction fee changed after review.");
            else if (mode == 8) expected = QString::fromStdString(common::PSBTErrorString(common::PSBTError::EXTERNAL_SIGNER_FAILED).translated);
            else if (mode == 9 || mode == 10) expected = QStringLiteral("The wallet changed the reviewed transaction.");
            else expected = QStringLiteral("The PSBT could not be exported.");
            QCOMPARE(result.error, expected);
        }
    }

    void importedLegacyIdentityUsesFinalizedTransaction()
    {
        WorkWallet wallet;
        const auto source = std::make_shared<const PartiallySignedTransaction>(psbtfeefixture::Legacy(2, false));
        const auto original = Bytes(*source);
        auto extracted = *source;
        CMutableTransaction finalized;
        QVERIFY(FinalizeAndExtractPSBT(extracted, finalized));
        const auto unsigned_tx = source->GetUnsignedTx();
        QVERIFY(unsigned_tx);
        QVERIFY(finalized.GetHash() != unsigned_tx->GetHash());
        const auto recorded = MakeTransactionRef(finalized);
        wallet.transaction_read = [recorded](const Txid& txid) {
            return txid == recorded->GetHash() ? recorded : CTransactionRef{};
        };
        const auto result = psbtwork::PrepareReview(wallet, source, {});
        QCOMPARE(Bytes(*source), original);
        QCOMPARE(wallet.sign_calls, 0);
        QCOMPARE(wallet.unlock_calls, 0);
        QCOMPARE(result.disposition, psbtwork::ImportResult::Disposition::Known);
        QCOMPARE(result.txid, QString::fromStdString(recorded->GetHash().ToString()));
        QVERIFY(!result.review);
    }

    void ownedOutputReviewDoesNotDependOnSigning_data()
    {
        QTest::addColumn<bool>("owned_inputs");
        QTest::addColumn<bool>("keys_disabled");
        QTest::addColumn<bool>("complete");
        QTest::newRow("self-transfer-signable") << true << false << false;
        QTest::newRow("self-transfer-watch-only-incomplete") << true << true << false;
        QTest::newRow("self-transfer-watch-only-complete") << true << true << true;
        QTest::newRow("owned-recipient-foreign-input") << false << false << false;
    }
    void signatureCommitmentsDoNotReplaceImmutableReview_data()
    {
        QTest::addColumn<int>("sighash");
        QTest::addColumn<int>("changed_output");
        QTest::addColumn<bool>("signature_remains_valid");
        QTest::newRow("all-commits-every-output") << int(SIGHASH_ALL) << 1 << false;
        QTest::newRow("none-does-not-commit-outputs") << int(SIGHASH_NONE) << 1 << true;
        QTest::newRow("single-commits-corresponding-output") << int(SIGHASH_SINGLE) << 0 << false;
        QTest::newRow("single-does-not-commit-other-output") << int(SIGHASH_SINGLE) << 1 << true;
    }
    void signatureCommitmentsDoNotReplaceImmutableReview()
    {
        QFETCH(int, sighash);
        QFETCH(int, changed_output);
        QFETCH(bool, signature_remains_valid);
        CKey key; key.MakeNewKey(true);
        FillableSigningProvider provider;
        QVERIFY(provider.AddKey(key));
        CMutableTransaction funding;
        funding.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
        funding.vout.emplace_back(2'000, GetScriptForDestination(PKHash(key.GetPubKey())));
        const auto parent = MakeTransactionRef(funding);
        CMutableTransaction transaction;
        transaction.vin.emplace_back(COutPoint{parent->GetHash(), 0});
        transaction.vout.emplace_back(1'000, GetScriptForDestination(PKHash(key.GetPubKey())));
        transaction.vout.emplace_back(500, GetScriptForDestination(DecodeDestination("1BoatSLRHtKNngkdXEeobR76b53LETtpyT")));
        PartiallySignedTransaction document{transaction, 2};
        document.inputs[0].non_witness_utxo = parent;
        const auto data = PrecomputePSBTData(document); QVERIFY(data);
        QVERIFY(SignPSBTInput(provider, document, 0, &*data, {.sighash_type = sighash, .finalize = true}));
        const auto signed_data = PrecomputePSBTData(document); QVERIFY(signed_data);
        QVERIFY(PSBTInputSignedAndVerified(document, 0, &*signed_data));
        opcodetype opcode;
        std::vector<unsigned char> signature;
        const auto& script = document.inputs[0].final_script_sig;
        auto cursor = script.begin();
        QVERIFY(script.GetOp(cursor, opcode, signature));
        QVERIFY(!signature.empty());
        QCOMPARE(int(signature.back()), sighash); // Actual signature byte, not PSBT metadata.
        auto changed = document;
        --changed.outputs[size_t(changed_output)].amount;
        const auto changed_data = PrecomputePSBTData(changed); QVERIFY(changed_data);
        QCOMPARE(PSBTInputSignedAndVerified(changed, 0, &*changed_data), signature_remains_valid);
        const auto source = std::make_shared<const PartiallySignedTransaction>(std::move(document));
        const auto original = Bytes(*source);
        const auto proof = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(*source));
        QCOMPARE(proof->amount, std::optional<CAmount>{500});
        psbtwork::Request request;
        request.kind = psbtwork::Kind::Broadcast;
        request.document = source; request.assessment = source; request.proof = proof; request.approved_fee = 500;
        int submissions{0};
        CTransactionRef submitted;
        psbtwork::Context context;
        context.broadcast = [&](CTransactionRef tx) {
            ++submissions; submitted = std::move(tx);
            return psbtwork::BroadcastResult{true, {}};
        };
        request.document = std::make_shared<const PartiallySignedTransaction>(std::move(changed));
        const auto refused = psbtwork::Execute(nullptr, request, context);
        QCOMPARE(refused.code, psbtwork::Error::Fee);
        QCOMPARE(submissions, 0);
        request.document = source;
        const auto accepted = psbtwork::Execute(nullptr, request, context);
        QVERIFY(accepted.submitted); QCOMPARE(submissions, 1); QVERIFY(submitted);
        QCOMPARE(submitted->vout, transaction.vout);
        QCOMPARE(Bytes(*source), original);
    }

    void standaloneRejectsIncompatibleFinalizedSignatures_data()
    {
        QTest::addColumn<int>("kind");
        QTest::addColumn<bool>("compatible");
        for (const auto kind : {psbtwork::Kind::Inspect, psbtwork::Kind::Sign, psbtwork::Kind::Broadcast}) {
            for (const bool compatible : {false, true}) {
                const QByteArray name = QByteArray::number(int(kind)) + (compatible ? "-compatible-keys" : "-incompatible-keys");
                QTest::newRow(name.constData()) << int(kind) << compatible;
            }
        }
    }
    void standaloneRejectsIncompatibleFinalizedSignatures()
    {
        QFETCH(int, kind);
        QFETCH(bool, compatible);
        auto original = psbtfeefixture::Legacy(2, false);
        const auto signer_result = compatible ? original : psbtfeefixture::Legacy(2, false);
        auto candidate = original;
        for (size_t i = 0; i < candidate.inputs.size(); ++i) {
            candidate.inputs[i].final_script_sig = signer_result.inputs[i].final_script_sig;
        }
        const auto data = PrecomputePSBTData(candidate); QVERIFY(data);
        QCOMPARE(PSBTInputSignedAndVerified(candidate, 0, &*data), compatible);
        if (psbtwork::Kind(kind) == psbtwork::Kind::Sign) {
            for (auto& input : original.inputs) input.final_script_sig.clear();
        } else original = std::move(candidate);
        const auto source = std::make_shared<const PartiallySignedTransaction>(std::move(original));
        const auto bytes = Bytes(*source);
        psbtwork::Request request;
        request.kind = psbtwork::Kind(kind); request.document = source; request.assessment = source;
        request.proof = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(*source));
        request.approved_fee = request.proof->amount;
        WorkWallet wallet;
        wallet.keys_disabled = true; wallet.external_signer = true;
        wallet.signed_inputs = wallet.inspect_signable = 2;
        wallet.sign_effect = [signer_result](auto& document) {
            for (size_t i = 0; i < document.inputs.size(); ++i) document.inputs[i].final_script_sig = signer_result.inputs[i].final_script_sig;
        };
        int submissions{0};
        psbtwork::Context context;
        context.broadcast = [&](CTransactionRef) { ++submissions; return psbtwork::BroadcastResult{true, {}}; };
        const auto result = psbtwork::Execute(&wallet, request, context);
        QCOMPARE(Bytes(*source), bytes);
        QCOMPARE(wallet.sign_calls, psbtwork::Kind(kind) == psbtwork::Kind::Sign ? 1 : 0);
        QCOMPARE(submissions, psbtwork::Kind(kind) == psbtwork::Kind::Broadcast && compatible ? 1 : 0);
        if (psbtwork::Kind(kind) == psbtwork::Kind::Inspect) {
            QVERIFY(result.snapshot);
            QCOMPARE(result.snapshot->can_broadcast, compatible);
            QCOMPARE(result.snapshot->complete, compatible);
            QCOMPARE(result.snapshot->signature_state, compatible ? psbtwork::SignatureState::Verified : psbtwork::SignatureState::Invalid);
            if (!compatible) QVERIFY(!result.snapshot->error.isEmpty());
        } else {
            QCOMPARE(result.code, compatible ? psbtwork::Error::None : psbtwork::Error::Invalid);
            if (!compatible) { QVERIFY(!result.snapshot); QVERIFY(!result.error.isEmpty()); }
        }
    }

    void ownedOutputReviewDoesNotDependOnSigning()
    {
        QFETCH(bool, owned_inputs);
        QFETCH(bool, keys_disabled);
        QFETCH(bool, complete);
        WorkWallet wallet;
        wallet.owned_inputs = owned_inputs;
        wallet.owned_outputs = true;
        wallet.keys_disabled = keys_disabled;
        auto document = Document(complete);
        const auto destination = DecodeDestination("1BoatSLRHtKNngkdXEeobR76b53LETtpyT");
        document.outputs[0].script = GetScriptForDestination(destination);
        const auto source = std::make_shared<const PartiallySignedTransaction>(std::move(document));
        const auto original = Bytes(*source);
        const auto result = psbtwork::PrepareReview(wallet, source, {});
        QCOMPARE(Bytes(*source), original);
        QCOMPARE(wallet.sign_calls, 0);
        QCOMPARE(result.disposition, psbtwork::ImportResult::Disposition::Review);
        QVERIFY(result.review);
        QCOMPARE(result.review->recipients.size(), 1);
        QCOMPARE(result.review->recipients.front().amount, CAmount{1'500});
        QCOMPARE(result.review->amount, CAmount{1'500});
        QCOMPARE(result.review->destinations, QSet<QString>{QString::fromStdString(EncodeDestination(destination))});
        const auto outputs = result.review->flow.value(QStringLiteral("outputs")).toList();
        QCOMPARE(outputs.size(), 1);
        QCOMPARE(outputs.front().toMap().value(QStringLiteral("ownership")).toString(), QStringLiteral("wallet"));
        QVERIFY(!outputs.front().toMap().value(QStringLiteral("changeKnown")).toBool());
        QVERIFY(!outputs.front().toMap().value(QStringLiteral("isChange")).toBool());
    }

    void importReviewPreservesAuthorityAndOriginal_data()
    {
        QTest::addColumn<int>("mode");
        const std::array names{"no-document", "empty-transaction", "incompatible-time-locks", "already-known",
            "metadata-error", "metadata-changes-unsigned", "metadata-conflicts-with-fee", "invalid-amount",
            "nonstandard-output", "zero-data-output", "only-change", "too-many-recipients", "zero-recipient-amount",
            "external-incomplete", "owned-incomplete", "watch-only-incomplete", "watch-only-complete",
            "authenticated-node-lookup", "known-query-throws", "no-signable-input", "incomplete-multisig", "empty-outputs",
            "zero-nonstandard-output"};
        for (int mode = 0; mode < int(names.size()); ++mode) QTest::newRow(names[mode]) << mode;
    }
    void importReviewPreservesAuthorityAndOriginal()
    {
        QFETCH(int, mode);
        WorkWallet wallet;
        auto document = Document(mode != 13 && mode != 14 && mode != 15 && mode != 17 && mode != 19);
        const auto destination = DecodeDestination("1BoatSLRHtKNngkdXEeobR76b53LETtpyT");
        document.outputs[0].script = GetScriptForDestination(destination);
        const auto parent = document.inputs[0].non_witness_utxo;
        if (mode == 1) { CMutableTransaction empty; document = PartiallySignedTransaction{empty,2}; }
        if (mode == 2) IncompatibleTimeLocks(document);
        if (mode == 7) document.outputs[0].amount = -1;
        if (mode == 8) document.outputs[0].script = CScript{} << OP_TRUE;
        if (mode == 9) document.outputs.emplace_back(2,0,CScript{} << OP_RETURN);
        if (mode == 11) {
            document.outputs.clear();
            for (int i = 0; i < 26; ++i) document.outputs.emplace_back(2,50,GetScriptForDestination(destination));
        }
        if (mode == 12) document.outputs[0].amount = 0;
        if (mode == 21) document.outputs.clear();
        if (mode == 22) document.outputs.emplace_back(2,0,CScript{} << OP_TRUE);
        if (mode == 17) document.inputs[0].non_witness_utxo = nullptr;
        if (mode == 20) {
            const auto fixture = psbtfeefixture::SignedWithoutScripts(false);
            document = fixture.document;
            document.inputs[0].witness_script = fixture.witness;
            document.inputs[0].partial_sigs.erase(document.inputs[0].partial_sigs.begin());
        }
        wallet.owned_inputs = mode == 10 || mode == 14 || mode == 15 || mode == 16 || mode == 19 || mode == 20;
        wallet.owned_outputs = mode == 10;
        wallet.keys_disabled = mode == 15 || mode == 16;
        wallet.inspect_signable = mode == 19 ? 0 : 1;
        wallet.address_found = true; wallet.address_label = "Original review label";
        if (mode == 4) wallet.inspect_error = common::PSBTError::EXTERNAL_SIGNER_FAILED;
        wallet.inspect_effect = [mode](auto& candidate) {
            if (mode == 5) candidate.outputs[0].amount = 1'499;
            if (mode == 6) candidate.inputs[0].witness_utxo = CTxOut{1'999,
                candidate.inputs[0].non_witness_utxo->vout[0].scriptPubKey};
        };
        auto known_document = document;
        CMutableTransaction known_transaction;
        const auto known = FinalizeAndExtractPSBT(known_document, known_transaction)
            ? std::optional<CMutableTransaction>{std::move(known_transaction)} : document.GetUnsignedTx();
        wallet.transaction_read = [mode, known](const Txid& txid) -> CTransactionRef {
            if (mode == 18) throw std::runtime_error{"original transaction query failed"};
            return mode == 3 && known && txid == known->GetHash() ? MakeTransactionRef(*known) : CTransactionRef{};
        };
        const auto source = mode == 0 ? nullptr : std::make_shared<const PartiallySignedTransaction>(document);
        const auto original = source ? Bytes(*source) : QByteArray{};
        int lookups{0};
        psbtwork::Context context;
        context.lookup = [&](const COutPoint& outpoint) -> std::optional<CTxOut> {
            ++lookups;
            if (outpoint.hash != parent->GetHash() || outpoint.n != 0) return std::nullopt;
            return parent->vout[0];
        };
        const auto result = psbtwork::PrepareReview(wallet, source, mode == 17 ? context : psbtwork::Context{}, 0);
        if (source) QCOMPARE(Bytes(*source), original);
        QCOMPARE(wallet.sign_calls, 0); QCOMPARE(wallet.unlock_calls, 0); QCOMPARE(wallet.lock_calls, 0);
        QCOMPARE(lookups, mode == 17 ? 1 : 0);
        const bool review = mode == 6 || mode == 9 || mode == 10 || (mode >= 13 && mode <= 20 && mode != 18);
        const auto disposition = mode == 3 ? psbtwork::ImportResult::Disposition::Known
            : review ? psbtwork::ImportResult::Disposition::Review : psbtwork::ImportResult::Disposition::Unsupported;
        QCOMPARE(result.disposition, disposition);
        QCOMPARE(wallet.inspect_calls, mode == 0 || mode == 1 || mode == 2 || mode == 3 || mode == 7 || mode == 18 || mode == 21 ? 0 : 1);
        if (mode == 3) {
            QVERIFY(!result.review); QVERIFY(result.message.isEmpty()); QCOMPARE(wallet.inspect_calls,0);
            QCOMPARE(wallet.transaction_reads,1); QVERIFY(known);
            QCOMPARE(result.txid,QString::fromStdString(known->GetHash().ToString()));
        }
        else if (review) {
            QVERIFY(result.review); QVERIFY(result.document);
            QCOMPARE(Bytes(*result.document), original);
            QCOMPARE(result.review->recipients.size(), 1);
            QCOMPARE(result.review->recipients[0].amount, CAmount{1'500});
            QCOMPARE(result.review->recipients[0].label, QStringLiteral("Original review label"));
            QCOMPARE(result.review->amount, CAmount{1'500}); QCOMPARE(result.review->fee, mode == 6 ? CAmount{0} : FEE);
            QCOMPARE(result.review->can_send, mode == 10 || mode == 14 || mode == 16);
            QCOMPARE(result.key_can_send, mode == 14);
            QCOMPARE(result.can_commit_complete, mode == 10 || mode == 14 || mode == 15 || mode == 16 || mode == 19);
            QCOMPARE(result.key_can_broadcast, mode == 9 || mode == 10 || mode == 16);
            if (mode == 6) {
                QVERIFY(!result.review->can_send); QVERIFY(!result.key_can_send); QVERIFY(!result.can_commit_complete);
                QCOMPARE(result.review->proof->state, psbtfee::State::Invalid);
                QCOMPARE(result.message, QStringLiteral("The transaction fee is missing or invalid. Add valid input information before broadcasting."));
            } else if (mode == 20) QCOMPARE(result.message, QStringLiteral("This transaction requires 2 of 2 signatures."));
            else if (mode == 9 || mode == 10 || mode == 14 || mode == 16) QVERIFY(result.message.isEmpty());
            else QCOMPARE(result.message, QStringLiteral("This wallet does not have the keys to sign this transaction."));
            QVERIFY(!result.review->presentation.empty()); QVERIFY(!result.review->flow.empty());
        } else {
            QVERIFY(!result.review); QVERIFY(!result.document);
            QString expected;
            if (mode == 0) expected=QStringLiteral("No PSBT is loaded.");
            else if (mode == 1 || mode == 2 || mode == 21) expected=QStringLiteral("The PSBT is malformed.");
            else if (mode == 4) expected=QString::fromStdString(common::PSBTErrorString(common::PSBTError::EXTERNAL_SIGNER_FAILED).translated);
            else if (mode == 5) expected=QStringLiteral("The wallet changed the reviewed transaction.");
            else if (mode == 6 || mode == 7) expected=QStringLiteral("The transaction fee is missing or invalid.");
            else if (mode == 8 || mode == 22) expected=QStringLiteral("Only PSBTs with standard address outputs are supported right now.");
            else if (mode == 11) expected=QStringLiteral("The PSBT has more recipients than this send flow supports.");
            else if (mode == 12) expected=QStringLiteral("The PSBT does not send a positive amount.");
            else expected=QStringLiteral("The PSBT could not be reviewed.");
            QCOMPARE(result.message, expected);
        }
    }

    void reviewFlowUsesCapturedProofOrExplicitPublicLookup_data()
    {
        QTest::addColumn<int>("mode");
        const std::array names{"no-transaction", "verified-proof", "wallet-parent", "witness-fallback",
            "full-parent-fallback", "missing-parent", "prepared-fee-without-prevouts", "missing-fee-proof",
            "owned-change", "invalid-output", "wallet-parent-index-out-of-range", "no-optional-metadata",
            "short-optional-metadata", "found-empty-wallet-label"};
        for (int mode = 0; mode < int(names.size()); ++mode) QTest::newRow(names[mode]) << mode;
    }
    void reviewFlowUsesCapturedProofOrExplicitPublicLookup()
    {
        QFETCH(int, mode);
        WorkWallet wallet;
        wallet.owned_inputs = true; wallet.owned_outputs = mode == 1 || mode == 8;
        wallet.debit = 2'000; wallet.address_found = mode != 4; wallet.address_label = "Original wallet input";
        if (mode == 13) wallet.address_label.clear();
        const auto destination = DecodeDestination("1BoatSLRHtKNngkdXEeobR76b53LETtpyT");
        const auto address = QString::fromStdString(EncodeDestination(destination));
        CMutableTransaction previous;
        previous.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE),0});
        previous.vout.emplace_back(2'000, GetScriptForDestination(destination));
        const auto parent = MakeTransactionRef(previous);
        auto document = Document();
        document.inputs[0].prev_txid = parent->GetHash(); document.inputs[0].non_witness_utxo = parent;
        document.outputs[0].script = GetScriptForDestination(destination);
        if (mode == 3) { document.inputs[0].witness_utxo = parent->vout[0]; document.inputs[0].non_witness_utxo = nullptr; }
        if (mode == 5 || mode == 7) document.inputs[0].non_witness_utxo = nullptr;
        if (mode == 9) document.outputs[0].amount = -1;
        if (mode == 10) document.inputs[0].prev_out = 1;
        const auto unsigned_tx = *document.GetUnsignedTx();
        std::vector<Txid> requested_parents;
        wallet.transaction_read = [&](const Txid& txid) {
            requested_parents.push_back(txid);
            return (mode == 2 || mode == 10) && txid == parent->GetHash() ? parent : CTransactionRef{};
        };
        // The public flow helper accepts a transaction and optional metadata
        // independently. Absent/short metadata must not invent input amounts.
        if (mode == 12) document.inputs.clear();
        const auto source = mode == 2 || mode == 11 ? nullptr : std::make_shared<const PartiallySignedTransaction>(document);
        std::shared_ptr<const psbtfee::Analysis> proof;
        if (mode == 1 || mode == 7 || mode == 8 || mode == 9 || mode == 13) proof = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(document));
        if (mode == 6) proof = std::make_shared<const psbtfee::Analysis>(psbtfee::Prepared(CTransaction{unsigned_tx},FEE));
        const auto flow = psbtwork::CollectReviewFlow(wallet, mode == 0 ? CTransactionRef{} : MakeTransactionRef(unsigned_tx),
            source, proof, mode == 1 ? QSet<QString>{address} : QSet<QString>{}, 0,
            mode == 8 ? QMap<QString,QString>{} : QMap<QString,QString>{{address,QStringLiteral("Reviewed recipient")}});
        QCOMPARE(wallet.transaction_reads, mode == 2 || mode == 3 || mode == 4 || mode == 5 || mode == 10 || mode == 11 || mode == 12 ? 1 : 0);
        QCOMPARE(wallet.debit_reads, wallet.transaction_reads);
        for (const auto& txid : requested_parents) QCOMPARE(txid,parent->GetHash());
        if (mode == 0 || mode == 9) { QVERIFY(flow.isEmpty()); return; }
        QVERIFY(!flow.empty());
        const auto inputs = flow.value(QStringLiteral("inputs")).toList();
        const auto outputs = flow.value(QStringLiteral("outputs")).toList();
        QCOMPARE(inputs.size(),1); QCOMPARE(outputs.size(),1);
        const auto input = inputs[0].toMap(); const auto output = outputs[0].toMap();
        const bool input_known = mode != 5 && mode != 6 && mode != 7 && mode != 10 && mode != 11 && mode != 12;
        QCOMPARE(input.value(QStringLiteral("amountKnown")).toBool(),input_known);
        QCOMPARE(flow.value(QStringLiteral("totalInputKnown")).toBool(),input_known);
        QCOMPARE(input.value(QStringLiteral("previousTxid")).toString(),QString::fromStdString(parent->GetHash().ToString()));
        QCOMPARE(input.value(QStringLiteral("ownership")).toString(),QStringLiteral("wallet"));
        if (input_known) {
            QCOMPARE(input.value(QStringLiteral("amountSat")).toLongLong(),qint64{2'000});
            QCOMPARE(input.value(QStringLiteral("address")).toString(),address);
            QCOMPARE(input.value(QStringLiteral("label")).toString(),mode == 4 || mode == 13 ? QString{} : QStringLiteral("Original wallet input"));
        } else QVERIFY(!input.value(QStringLiteral("amountSat")).isValid());
        QCOMPARE(output.value(QStringLiteral("amountSat")).toLongLong(),qint64{1'500});
        QCOMPARE(output.value(QStringLiteral("address")).toString(),address);
        QCOMPARE(output.value(QStringLiteral("isChange")).toBool(),mode == 8);
        QCOMPARE(output.value(QStringLiteral("label")).toString(),mode == 8 ? QString{} : QStringLiteral("Reviewed recipient"));
        const bool fee_known = mode == 1 || mode == 2 || mode == 6 || mode == 8 || mode == 11 || mode == 13;
        QCOMPARE(flow.value(QStringLiteral("feeKnown")).toBool(),fee_known);
        if (fee_known) QCOMPARE(flow.value(QStringLiteral("feeSat")).toLongLong(),qint64{FEE});
        else QVERIFY(!flow.value(QStringLiteral("feeSat")).isValid());
        QVERIFY(!flow.value(QStringLiteral("presentation")).toMap().empty());
    }

    void uppercaseCanonicalRecipientIsNotClassifiedAsChange()
    {
        const CTxDestination destination{WitnessV0KeyHash{uint160{std::vector<unsigned char>(20, 1)}}};
        const QString address = QString::fromStdString(EncodeDestination(destination));
        const QString uppercase_address = address.toUpper();
        QVERIFY(address != uppercase_address);

        auto document = Document();
        document.outputs[0].script = GetScriptForDestination(destination);
        const auto transaction = MakeTransactionRef(*document.GetUnsignedTx());
        WorkWallet wallet;
        wallet.owned_outputs = true;
        const auto proof = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(document));

        const auto flow = psbtwork::CollectReviewFlow(wallet, transaction, {}, proof,
            QSet<QString>{uppercase_address}, 0, {});

        const auto outputs = flow.value(QStringLiteral("outputs")).toList();
        QCOMPARE(outputs.size(), 1);
        QCOMPARE(outputs.front().toMap().value(QStringLiteral("address")).toString(), address);
        QVERIFY(!outputs.front().toMap().value(QStringLiteral("isChange")).toBool());
    }

    void nodeBroadcastOwnsPublicOutcome_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("node-unavailable") << 0; QTest::newRow("accepted") << 1;
        QTest::newRow("rejected-without-detail") << 2; QTest::newRow("bounded-rejection-detail") << 3;
    }
    void nodeBroadcastOwnsPublicOutcome()
    {
        QFETCH(int, mode);
        MockNode node;
        const auto transaction = MakeTransactionRef(*Document(true).GetUnsignedTx());
        CTransactionRef observed;
        const QString detail(4'200,QLatin1Char('x'));
        node.broadcast_transaction_fn = [&](auto submitted,CAmount maximum,CFeeRate maximum_rate,std::string& error) {
            observed = std::move(submitted);
            if (maximum <= 0 || maximum_rate.GetFeePerK() <= 0) return node::TransactionError::MAX_FEE_EXCEEDED;
            if (mode == 3) error = detail.toStdString();
            return mode == 1 ? node::TransactionError::OK : node::TransactionError::MEMPOOL_REJECTED;
        };
        const auto broadcast = psbtwork::NodeBroadcast(mode == 0 ? nullptr : &node);
        if (mode == 0) { QVERIFY(!broadcast); QCOMPARE(node.calls.broadcastTransaction.load(),0); return; }
        QVERIFY(broadcast);
        const auto result = broadcast(transaction);
        QCOMPARE(node.calls.broadcastTransaction.load(),1); QVERIFY(observed);
        QCOMPARE(observed->GetHash(),transaction->GetHash());
        QCOMPARE(result.accepted,mode == 1);
        if (mode == 1) QVERIFY(result.error.isEmpty());
        else {
            auto expected = QString::fromStdString(common::TransactionErrorString(node::TransactionError::MEMPOOL_REJECTED).translated);
            if (mode == 3) expected += QStringLiteral(": ") + detail.left(4'096);
            QCOMPARE(result.error,expected);
        }
    }

    void multisigClassificationUsesOriginalPublicScripts_data()
    {
        QTest::addColumn<int>("mode");
        const std::array names{"missing-input", "missing-prevout", "redeem-script", "witness-script",
            "bare-parent-script", "non-multisig-parent", "multi-a-script-metadata"};
        for (int mode = 0; mode < int(names.size()); ++mode) QTest::newRow(names[mode]) << mode;
    }
    void multisigClassificationUsesOriginalPublicScripts()
    {
        QFETCH(int, mode);
        CKey first,second,third; first.MakeNewKey(true); second.MakeNewKey(true); third.MakeNewKey(true);
        const auto script = GetScriptForMultisig(2,{first.GetPubKey(),second.GetPubKey(),third.GetPubKey()});
        auto document = Document();
        if (mode == 1) document.inputs[0].non_witness_utxo = nullptr;
        if (mode == 2) document.inputs[0].redeem_script = script;
        if (mode == 3) document.inputs[0].witness_script = script;
        if (mode == 4) {
            auto previous = CMutableTransaction{*document.inputs[0].non_witness_utxo};
            previous.vout[0].scriptPubKey = script;
            document.inputs[0].non_witness_utxo = MakeTransactionRef(previous);
            document.inputs[0].prev_txid = document.inputs[0].non_witness_utxo->GetHash();
        }
        if (mode == 6) {
            const auto first_pubkey = first.GetPubKey();
            const auto second_pubkey = second.GetPubKey();
            const std::vector<unsigned char> first_xonly_key(first_pubkey.begin() + 1, first_pubkey.end());
            const std::vector<unsigned char> second_xonly_key(second_pubkey.begin() + 1, second_pubkey.end());
            document.inputs[0].witness_script = CScript{} << first_xonly_key << OP_CHECKSIG
                << second_xonly_key << OP_CHECKSIGADD << OP_2 << OP_NUMEQUAL;
        }
        const auto original = Bytes(document);
        const auto classified = psbtwork::Multisig(document,mode == 0 ? 1 : 0);
        QCOMPARE(bool(classified),(mode >= 2 && mode <= 4) || mode == 6);
        if (classified) {
            QCOMPARE(classified->first,2);
            QCOMPARE(classified->second,mode == 6 ? 2 : 3);
        }
        QCOMPARE(Bytes(document),original);
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(PsbtWorkTests)
#else
QTEST_MAIN(PsbtWorkTests)
#endif
#include <test_psbtwork.moc>
