// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/psbtwork.h>
#include <qml/psbtdocument.h>
#include <qml/psbtsignatures.h>
#include <qml/models/signertransaction.h>
#include <qml/models/transactionflow.h>
#include <qml/flowpresentation.h>
#include <qml/bitcoinunits.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <node/psbt.h>
#include <node/types.h>
#include <node/transaction.h>
#include <script/solver.h>
#include <streams.h>
#include <util/result.h>
#include <util/strencodings.h>
#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
#include <QSaveFile>
#include <QUrl>
#include <algorithm>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
QString Tr(const char* text, const char* comment = nullptr, int n = -1) { return QCoreApplication::translate("PsbtQmlModel", text, comment, n); }

QString FormatBtc(CAmount amount)
{
    return QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, amount) + QStringLiteral(" BTC");
}

QString SerializePsbtBase64(const PartiallySignedTransaction& psbt)
{
    DataStream stream{};
    stream << psbt;
    return QString::fromStdString(EncodeBase64(stream.str()));
}

QString TransactionErrorText(node::TransactionError error)
{
    return QString::fromStdString(common::TransactionErrorString(error).translated);
}

std::optional<std::pair<int, int>> ExtractMultisigSigInfo(const CScript& script)
{
    if (script.empty()) {
        return std::nullopt;
    }

    std::vector<std::vector<unsigned char>> solutions;
    if (Solver(script, solutions) == TxoutType::MULTISIG
        && solutions.size() >= 2
        && !solutions.front().empty()
        && !solutions.back().empty()) {
        const int required{solutions.front()[0]};
        const int total{solutions.back()[0]};
        if (required > 0 && total >= required) {
            return std::pair{required, total};
        }
    }
    if (const auto multi_a{MatchMultiA(script)}) {
        return std::pair{multi_a->first, static_cast<int>(multi_a->second.size())};
    }
    return std::nullopt;
}

util::Result<PartiallySignedTransaction> DecodePsbtFromBytes(const QByteArray& bytes)
{
    std::vector<std::byte> raw;
    raw.reserve(bytes.size());
    for (const char ch : bytes) {
        raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return DecodeRawPSBT(std::span<const std::byte>{raw.data(), raw.size()});
}

class QtFileReader final : public psbtwork::FileReader
{
public:
    explicit QtFileReader(QFile& file) : m_file{file} {}

    bool open() override { return m_file.open(QIODevice::ReadOnly); }
    qint64 initialSize() override { return m_file.size(); }
    QByteArray read(qint64 maximum) override { return m_file.read(maximum); }
    bool hasError() override { return m_file.error() != QFile::NoError; }
    QString errorString() override { return m_file.errorString(); }

private:
    QFile& m_file;
};

class QtFileWriter final : public psbtwork::FileWriter
{
public:
    explicit QtFileWriter(QSaveFile& file) : m_file{file} {}

    bool open() override { return m_file.open(QIODevice::WriteOnly); }
    qint64 write(const QByteArray& bytes) override { return m_file.write(bytes); }
    void cancel() override { m_file.cancelWriting(); }
    bool commit() override { return m_file.commit(); }
    QString errorString() override { return m_file.errorString(); }

private:
    QSaveFile& m_file;
};

} // namespace

QString psbtwork::LocalFilePath(const QString& path)
{
    const QUrl url(path);
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    return path;
}

QString psbtwork::ReadFile(psbtwork::FileReader& reader, PartiallySignedTransaction& psbt)
{
    if (!reader.open()) {
        return Tr("Could not open PSBT file: %1").arg(reader.errorString());
    }
    if (reader.initialSize() > MAX_FILE_SIZE_PSBT) {
        return Tr("PSBT file must be smaller than 100 MiB");
    }

    const QByteArray bytes{reader.read(static_cast<qint64>(MAX_FILE_SIZE_PSBT + 1))};
    if (bytes.size() > MAX_FILE_SIZE_PSBT) return Tr("PSBT file must be smaller than 100 MiB");
    if (reader.hasError()) return Tr("Could not read PSBT file: %1").arg(reader.errorString());
    const auto raw_result{DecodePsbtFromBytes(bytes)};
    if (raw_result) {
        psbt = *raw_result;
        return {};
    }

    const auto base64_result{DecodeBase64PSBT(QString::fromUtf8(bytes).trimmed().toStdString())};
    if (base64_result) {
        psbt = *base64_result;
        return {};
    }

    const bilingual_str raw_error{util::ErrorString(raw_result)};
    const bilingual_str base64_error{util::ErrorString(base64_result)};
    const bilingual_str& decode_error{base64_error.original == "invalid base64" ? raw_error : base64_error};
    return Tr("Could not decode PSBT: %1").arg(QString::fromStdString(decode_error.translated));
}

QString psbtwork::LoadFile(const QString& path, PartiallySignedTransaction& psbt)
{
    QFile file{LocalFilePath(path)};
    QtFileReader reader{file};
    return psbtwork::ReadFile(reader, psbt);
}

QString psbtwork::ErrorText(common::PSBTError error)
{
    return QString::fromStdString(common::PSBTErrorString(error).translated);
}

QString psbtwork::WriteFile(psbtwork::FileWriter& writer, const PartiallySignedTransaction& psbt)
{
    if (!writer.open()) {
        return Tr("Could not save PSBT: %1").arg(writer.errorString());
    }

    const QByteArray raw{SerializeRaw(psbt)};
    if (writer.write(raw) != raw.size()) {
        writer.cancel();
        return Tr("Could not write the complete PSBT file.");
    }
    if (!writer.commit()) {
        return Tr("Could not save PSBT: %1").arg(writer.errorString());
    }
    return {};
}

QString psbtwork::SaveFile(const PartiallySignedTransaction& psbt, const QString& path)
{
    QSaveFile file{LocalFilePath(path)};
    QtFileWriter writer{file};
    return psbtwork::WriteFile(writer, psbt);
}

std::optional<std::pair<int, int>> psbtwork::Multisig(const PartiallySignedTransaction& psbt, std::size_t index)
{
    if (index >= psbt.inputs.size()) return std::nullopt;
    const PSBTInput& input{psbt.inputs[index]};
    if (auto info{ExtractMultisigSigInfo(input.redeem_script)}) return info;
    if (auto info{ExtractMultisigSigInfo(input.witness_script)}) return info;

    CTxOut utxo;
    if (input.GetUTXO(utxo)) {
        if (auto info{ExtractMultisigSigInfo(utxo.scriptPubKey)}) return info;
    }
    return std::nullopt;
}

namespace psbtwork {
namespace {
int SummaryTailCount(const SummarySource& source)
{
    return 2 + (source.analysis_error.isEmpty() ? 0 : 1) + (source.unsigned_inputs ? 1 : 0);
}
std::shared_ptr<const Snapshot> Inspect(interfaces::Wallet* wallet, std::shared_ptr<const PartiallySignedTransaction> source,
                                      std::shared_ptr<const psbtfee::Analysis> seed, const Context& context, const QString& status = {},
                                      std::shared_ptr<const PartiallySignedTransaction>* assessment = nullptr)
{
    auto snapshot = std::make_shared<Snapshot>();
    if (!source) { snapshot->error = Tr("No PSBT is loaded."); return snapshot; }
    auto candidate = *source;
    auto proof = psbtfee::Analyze(candidate, seed ? seed->verified : psbtfee::Outputs{});
    const auto original = candidate.GetUnsignedTx();
    size_t could_sign{0}; bool complete{false};
    std::optional<common::PSBTError> fill_error;
    if (proof.state != psbtfee::State::Invalid) {
        if (seed) psbtfee::FillMissing(candidate, *seed);
        complete = FinalizePSBT(candidate);
        if (wallet) fill_error = wallet->fillPSBT({.sign = false, .bip32_derivs = false}, &could_sign, candidate, complete);
        snapshot->metadata_failed = fill_error.has_value();
        const auto returned = candidate.GetUnsignedTx();
        if (!original || !returned || !MatchesUnsignedTransaction(*original, *returned)) {
            const bool valid_outputs = psbtfee::Analyze(candidate).outputs_valid;
            candidate = *source; could_sign = 0;
            snapshot->error = valid_outputs ? Tr("The wallet changed the reviewed transaction.") : Tr("The transaction fee is missing or invalid.");
            snapshot->metadata_failed = true;
        }
        if (seed && seed->amount && !psbtfee::MatchesReview(candidate, *seed, seed->amount)) {
            candidate = *source;
            could_sign = 0;
            snapshot->error = Tr("The transaction fee changed after review.");
            snapshot->metadata_failed = true;
        }
        proof = psbtfee::Analyze(candidate, seed ? seed->verified : psbtfee::Outputs{});
        if (proof.state == psbtfee::State::Missing && context.lookup) {
            try {
                const auto resolved = psbtfee::Resolve(candidate, proof, context.lookup);
                if (resolved.proof) proof = *resolved.proof;
            } catch (...) {
                // A fee lookup failure must not discard a valid document or
                // turn its unverified amounts into an authenticated review.
            }
        }
        if (proof.state == psbtfee::State::Known) {
            psbtfee::FillMissing(candidate, proof);
            complete = FinalizePSBT(candidate);
        }
    }
    // Inspection may fill metadata or finalize its temporary copy. Read-only
    // review, copy and export continue to own the exact imported document.
    snapshot->document = std::move(source);
    snapshot->proof = std::make_shared<const psbtfee::Analysis>(std::move(proof));
    snapshot->complete = complete;
    snapshot->could_sign = could_sign;
    snapshot->unsigned_inputs = CountPSBTUnsignedInputs(candidate);
    snapshot->facts = {wallet != nullptr, wallet && (!wallet->privateKeysDisabled() || wallet->hasExternalSigner()), status};
    if (snapshot->error.isEmpty()) snapshot->error = fill_error ? ErrorText(*fill_error)
        : !snapshot->proof->amount ? Tr("The transaction fee is missing or invalid.") : QString{};
    snapshot->assessment = std::make_shared<const PartiallySignedTransaction>(std::move(candidate));
    snapshot->summary_source = CaptureSummarySource(snapshot->assessment, snapshot->proof, wallet);
    snapshot->summary_count = snapshot->summary_source->row_count;
    snapshot->initial_summary_page = ProjectSummaryPage(snapshot->summary_source, 0, 128);
    if (context.policy && complete && snapshot->proof->amount && !snapshot->metadata_failed) {
        auto analyzed = *snapshot->assessment;
        CMutableTransaction extracted;
        if (FinalizeAndExtractPSBT(analyzed, extracted)) snapshot->node_facts = context.policy(MakeTransactionRef(std::move(extracted)), *snapshot->proof->amount);
    }
    psbtdocument::Qualify(*snapshot);
    if (assessment) *assessment = snapshot->assessment;
    return snapshot;
}
}

std::shared_ptr<const SummarySource> CaptureSummarySource(
    std::shared_ptr<const PartiallySignedTransaction> assessment,
    std::shared_ptr<const psbtfee::Analysis> proof,
    interfaces::Wallet* wallet)
{
    auto source = std::make_shared<SummarySource>();
    source->assessment = std::move(assessment);
    source->proof = std::move(proof);
    const auto transaction = source->assessment ? source->assessment->GetUnsignedTx() : std::nullopt;
    if (!transaction) {
        source->kind = SummarySource::Kind::MissingTransaction;
        source->row_count = 1;
        return source;
    }
    if (!source->proof || source->proof->state == psbtfee::State::Invalid) {
        source->kind = SummarySource::Kind::InvalidFee;
        source->row_count = 2;
        return source;
    }
    if (transaction->vout.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::length_error{"PSBT output count exceeds summary indexing range"};
    }
    source->kind = SummarySource::Kind::Transaction;
    source->owned_outputs.resize(static_cast<int>(transaction->vout.size()));
    for (int index{0}; index < source->owned_outputs.size(); ++index) {
        source->owned_outputs.setBit(index, wallet && wallet->txoutIsMine(transaction->vout[static_cast<size_t>(index)]));
    }
    const auto analysis = node::AnalyzePSBT(*source->assessment);
    if (!analysis.error.empty()) source->analysis_error = QString::fromStdString(analysis.error);
    source->unsigned_inputs = CountPSBTUnsignedInputs(*source->assessment);
    source->row_count = source->owned_outputs.size() + SummaryTailCount(*source);
    return source;
}

SummaryPage ProjectSummaryPage(std::shared_ptr<const SummarySource> source, int offset, int count,
                               quint64 generation, quint64 request)
{
    SummaryPage page;
    page.generation = generation;
    page.request = request;
    page.offset = offset;
    if (!source || offset < 0 || count < 1) {
        page.error = Tr("The PSBT summary page request is invalid.");
        return page;
    }
    page.total = source->row_count;
    const int bounded_count = std::min(count, 128);
    const int end = static_cast<int>(std::min<int64_t>(source->row_count, int64_t{offset} + bounded_count));
    page.rows.reserve(std::max(0, end - offset));
    if (source->kind == SummarySource::Kind::MissingTransaction) {
        for (int row{offset}; row < end; ++row) page.rows << Tr("PSBT does not contain an unsigned transaction.");
        return page;
    }
    if (source->kind == SummarySource::Kind::InvalidFee) {
        for (int row{offset}; row < end; ++row) {
            if (row == 0) page.rows << Tr("The transaction fee is missing or invalid.");
            else if (row == 1) page.rows << Tr("Transaction fee is not available.");
        }
        return page;
    }

    const auto transaction = source->assessment ? source->assessment->GetUnsignedTx() : std::nullopt;
    if (!transaction || !source->proof) {
        page.error = Tr("The PSBT summary source is incomplete.");
        return page;
    }
    const auto output_count = static_cast<int>(transaction->vout.size());
    for (int row{offset}; row < end; ++row) {
        if (row < output_count) {
            const auto& output = transaction->vout[static_cast<size_t>(row)];
            CTxDestination destination;
            const auto address = ExtractDestination(output.scriptPubKey, destination)
                ? QString::fromStdString(EncodeDestination(destination)) : Tr("unknown destination");
            const auto owned = source->owned_outputs.testBit(row)
                ? QStringLiteral(" ") + Tr("(own address)") : QString{};
            page.rows << Tr("Sends %1 to %2%3").arg(FormatBtc(output.nValue), address, owned);
            continue;
        }
        int tail = row - output_count;
        if (!source->analysis_error.isEmpty()) {
            if (tail == 0) {
                page.rows << Tr("Analysis: %1").arg(source->analysis_error);
                continue;
            }
            --tail;
        }
        if (tail == 0) page.rows << (source->proof->amount
            ? Tr("Pays transaction fee: %1").arg(FormatBtc(*source->proof->amount))
            : Tr("Transaction fee is not available."));
        else if (tail == 1) page.rows << Tr("Total output amount: %1").arg(FormatBtc(source->proof->output_total));
        else if (tail == 2 && source->unsigned_inputs) page.rows << Tr("Unsigned inputs: %1").arg(source->unsigned_inputs);
    }
    return page;
}

Broadcast NodeBroadcast(interfaces::Node* node)
{
    if (!node) return {};
    return [node](CTransactionRef transaction) {
        std::string detail;
        const auto code = node->broadcastTransaction(transaction, node::DEFAULT_MAX_RAW_TX_FEE_RATE.GetFeePerK(), node::DEFAULT_MAX_RAW_TX_FEE_RATE, detail);
        QString error;
        if (code != node::TransactionError::OK) {
            error = TransactionErrorText(code);
            if (!detail.empty()) error += QStringLiteral(": ") + QString::fromStdString(detail).left(4096);
        }
        return BroadcastResult{code == node::TransactionError::OK, error};
    };
}

Result Execute(interfaces::Wallet* wallet, Request request, const Context& context)
{
    Result result; result.path = request.path;
    bool relock_required{false};
    const auto finishAfterRelock = [&] {
        if (std::exchange(relock_required, false)) {
            try {
                result.relock_failed = !wallet->lock();
            } catch (...) {
                result.relock_failed = true;
            }
            if (result.relock_failed) {
                result.code = Error::Relock;
                result.error = Tr("The wallet lock could not be restored. Check its security state before retrying.");
            }
        }
        request.passphrase.reset();
        request.captured_passphrase.reset();
        return result;
    };
    try {
        if (request.kind == Kind::Load) {
            CMutableTransaction empty;
            PartiallySignedTransaction document{empty, 2};
            result.error = LoadFile(request.path, document);
            if (!result.error.isEmpty()) { result.code = Error::Invalid; return finishAfterRelock(); }
            request.document = std::make_shared<const PartiallySignedTransaction>(std::move(document));
            request.proof.reset();
        }
        if (!request.document) { result.code = Error::Invalid; result.error = Tr("No PSBT is loaded."); return finishAfterRelock(); }
        if (request.kind == Kind::Sign || request.kind == Kind::Broadcast) {
            if (!request.proof || !psbtfee::MatchesReview(*request.document, *request.proof, request.approved_fee)) {
                result.code = Error::Fee; result.error = Tr("The transaction fee is missing or invalid."); return finishAfterRelock();
            }
        }
        if (request.kind == Kind::Save) {
            result.error = context.save(*request.document, request.path);
            result.saved = result.error.isEmpty();
            result.code = result.saved ? Error::None : Error::Core;
            return finishAfterRelock();
        }
        if (request.kind == Kind::Clipboard) {
            result.clipboard = SerializePsbtBase64(*request.document);
            return finishAfterRelock();
        }
        if (request.kind == Kind::Sign) {
            // Capture is an immutable Native-cleaned QString owner. Conversion,
            // secure allocation and cleansing never run in the GUI wrapper.
            if(request.captured_passphrase){
                request.passphrase=context.materialize(request.captured_passphrase);
                request.captured_passphrase.reset();
            }
            if (!wallet || (wallet->privateKeysDisabled() && !wallet->hasExternalSigner())) {
                result.code = Error::Unavailable; result.error = Tr("This wallet cannot sign transactions because private keys are disabled."); return finishAfterRelock();
            }
            if (wallet->isCrypted() && wallet->isLocked()) {
                if (!request.passphrase) { result.code = Error::NeedsUnlock; result.error = Tr("Enter your wallet password to sign this transaction."); return finishAfterRelock(); }
                relock_required = true;
                if (!wallet->unlock(*request.passphrase)) { result.code = Error::IncorrectPassphrase; result.error = Tr("The wallet password is incorrect."); return finishAfterRelock(); }
            }
            auto candidate = *request.document;
            psbtfee::FillMissing(candidate, *request.proof);
            bool complete{false}; size_t signed_inputs{0};
            std::optional<common::PSBTError> sign_error;
            bool signer_threw{false};
            try { sign_error = wallet->fillPSBT({.sign = true, .bip32_derivs = true}, &signed_inputs, candidate, complete); }
            catch (...) { signer_threw = true; }
            const auto original = request.document->GetUnsignedTx(), returned = candidate.GetUnsignedTx();
            if (!original || !returned || !MatchesUnsignedTransaction(*original, *returned)) {
                result.code = Error::Changed; result.error = Tr("The reviewed transaction changed after review."); return finishAfterRelock();
            }
            if (!psbtfee::MatchesReview(candidate, *request.proof, request.approved_fee)) {
                result.code = Error::Fee; result.error = Tr("The transaction fee changed after review."); return finishAfterRelock();
            }
            complete = FinalizePSBT(candidate);
            if (!psbtsignatures::VerifyFinalizedInputs(candidate, complete)) {
                result.code = Error::Invalid;
                result.error = Tr("The signer returned invalid signatures for the reviewed transaction.");
                return finishAfterRelock();
            }
            result.signature_status = complete ? psbtsignatures::Status::Valid
                : psbtsignatures::VerifyContributions(*request.document, candidate);
            if (result.signature_status != psbtsignatures::Status::Valid) {
                result.code = result.signature_status == psbtsignatures::Status::Invalid ? Error::Invalid
                    : signer_threw ? Error::Uncertain : Error::UnverifiedSignatures;
                result.error = result.signature_status == psbtsignatures::Status::Invalid
                    ? Tr("The signer returned invalid signatures for the reviewed transaction.")
                    : Tr("The returned PSBT needs more verification information. The original document is unchanged; export the unverified result to reconcile it.");
                if (result.signature_status == psbtsignatures::Status::Unknown)
                    result.quarantined_document = std::make_shared<const PartiallySignedTransaction>(std::move(candidate));
                return finishAfterRelock();
            }
            request.document = std::make_shared<const PartiallySignedTransaction>(std::move(candidate));
            // Retain legitimate partial signatures even when the signer reports
            // an error or a later presentation read fails. Never adopt changed
            // unsigned terms or changed authenticated fee metadata.
            auto retained = std::make_shared<Snapshot>();
            retained->document = request.document;
            retained->proof = request.proof;
            retained->complete = complete;
            result.snapshot = std::move(retained);
            if (sign_error) { result.code = Error::Core; result.error = Tr("Could not sign PSBT: %1").arg(ErrorText(*sign_error)); }
            if (signer_threw) { result.code = Error::Uncertain; result.error = Tr("The signer ended without a confirmed outcome. The retained PSBT must be reconciled before retrying."); }
            request.status = complete ? Tr("PSBT signed. Transaction is ready for broadcast.")
                : signed_inputs ? Tr("Signed %n input(s). More signatures are still required.", nullptr, signed_inputs)
                : Tr("This wallet could not add any signatures to the PSBT.");
        }
        if (request.kind == Kind::Broadcast) {
            if (!context.broadcast) { result.code = Error::Unavailable; result.error = Tr("The node interface is unavailable."); return finishAfterRelock(); }
            if (!request.assessment) {
                result.code = Error::Changed; result.error = Tr("The analyzed transaction is unavailable. Review the PSBT again."); return finishAfterRelock();
            }
            auto candidate = *request.assessment;
            const auto source_terms = request.document->GetUnsignedTx(), analyzed_terms = candidate.GetUnsignedTx();
            if (!source_terms || !analyzed_terms || !MatchesUnsignedTransaction(*source_terms, *analyzed_terms)) {
                result.code = Error::Changed; result.error = Tr("The analyzed transaction differs from the reviewed transaction."); return finishAfterRelock();
            }
            if (!psbtfee::MatchesReview(candidate, *request.proof, request.approved_fee)) {
                result.code = Error::Fee; result.error = Tr("The analyzed transaction fee differs from the approved fee."); return finishAfterRelock();
            }
            CMutableTransaction tx;
            if (!FinalizeAndExtractPSBT(candidate, tx)) {
                result.code = Error::Invalid; result.error = Tr("The analyzed PSBT is not complete and cannot be broadcast."); return finishAfterRelock();
            }
            const auto reviewed = request.document->GetUnsignedTx();
            if (!reviewed || !MatchesUnsignedTransaction(*reviewed, tx) || !psbtfee::MatchesReview(candidate, *request.proof, request.approved_fee)) {
                result.code = Error::Changed; result.error = Tr("The transaction fee or reviewed transaction changed after review."); return finishAfterRelock();
            }
            const auto transaction = MakeTransactionRef(std::move(tx));
            if (!psbtsignatures::VerifyFinalizedInputs(candidate, true)) {
                result.code = Error::Invalid; result.error = Tr("The PSBT contains invalid signatures."); return finishAfterRelock();
            }
            if (context.policy) {
                const auto decision = psbtpolicy::Evaluate(context.policy(transaction, *request.approved_fee));
                if (decision.state == psbtpolicy::State::Blocked) {
                    result.code = Error::Changed; result.error = decision.explanation; return finishAfterRelock();
                }
            }
            result.txid = QString::fromStdString(transaction->GetHash().ToString());
            const auto submitted = context.broadcast(transaction);
            result.submitted = submitted.accepted;
            if (result.submitted) result.transaction = transaction;
            result.txid = QString::fromStdString(transaction->GetHash().ToString());
            if (!submitted.accepted) { result.code = Error::Core; result.error = Tr("Transaction broadcast failed: %1").arg(submitted.error); }
            return finishAfterRelock();
        }
        result.snapshot = Inspect(wallet, request.document, request.proof, context, request.status);
    } catch (const secretinput::Failure&) {
        result.code=Error::Core;
        result.error=Tr("The signing password could not be prepared. Signing did not begin.");
    } catch (...) {
        result.code = request.kind == Kind::Broadcast || request.kind == Kind::Sign || request.kind == Kind::Save ? Error::Uncertain : Error::Core;
        result.error = result.code == Error::Uncertain ? Tr("The PSBT action ended without a confirmed outcome. Reconcile before retrying.") : Tr("The PSBT could not be processed.");
    }
    return finishAfterRelock();
}

DocumentResult ExportReview(interfaces::Wallet& wallet, std::shared_ptr<const PartiallySignedTransaction> source,
                            CTransactionRef reviewed, bool fill_metadata,
                            std::shared_ptr<const psbtfee::Analysis> proof, std::optional<CAmount> approved_fee)
{
    try {
        if (!source && !reviewed) return {{}, Tr("No transaction is prepared.")};
        CMutableTransaction tx;
        if (reviewed) {
            tx = CMutableTransaction{*reviewed};
            for (auto& input : tx.vin) { input.scriptSig.clear(); input.scriptWitness.SetNull(); }
        }
        // A file this app creates is version 0, as Bitcoin-Qt writes since
        // bitcoin/bitcoin#36539: Core before v32 cannot read version 2 (#1112).
        // An imported PSBT is saved back in the version it arrived with.
        PartiallySignedTransaction document = source ? *source : PartiallySignedTransaction{tx, /*version=*/0};
        const auto approved = document.GetUnsignedTx();
        if (!approved || (reviewed && !MatchesUnsignedTransaction(tx, *approved))) return {{}, Tr("The reviewed transaction changed.")};
        if (proof && approved_fee && !psbtfee::MatchesReview(document, *proof, approved_fee)) return {{}, Tr("The transaction fee changed after review.")};
        if (fill_metadata) {
            bool complete{false};
            if (const auto error = wallet.fillPSBT({.sign = false, .bip32_derivs = true}, nullptr, document, complete)) return {{}, ErrorText(*error)};
        }
        const auto returned = document.GetUnsignedTx();
        if (!returned || !MatchesUnsignedTransaction(*approved, *returned)) return {{}, Tr("The wallet changed the reviewed transaction.")};
        if (proof && approved_fee && !psbtfee::MatchesReview(document, *proof, approved_fee)) return {{}, Tr("The transaction fee changed after review.")};
        return {std::make_shared<const PartiallySignedTransaction>(std::move(document)), {}};
    } catch (...) { return {{}, Tr("The PSBT could not be exported.")}; }
}

QVariantMap CollectReviewFlow(interfaces::Wallet& wallet, CTransactionRef transaction,
    std::shared_ptr<const PartiallySignedTransaction> document,
    std::shared_ptr<const psbtfee::Analysis> proof, const QSet<QString>& recipients, int display_unit,
    const QMap<QString, QString>& reviewed_notes)
{
    if (!transaction) return {};
    std::vector<CScript> recipient_scripts;
    recipient_scripts.reserve(static_cast<size_t>(recipients.size()));
    for (const auto& address : recipients) {
        const auto destination = DecodeDestination(address.toStdString());
        if (IsValidDestination(destination)) recipient_scripts.push_back(GetScriptForDestination(destination));
    }
    interfaces::WalletTx preview{}; preview.tx = transaction;
    std::vector<std::optional<CTxOut>> prevouts;
    prevouts.reserve(transaction->vin.size());
    for (size_t i = 0; i < transaction->vin.size(); ++i) {
        const auto& input = transaction->vin[i];
        preview.txin_is_mine.push_back(wallet.txinIsMine(input));
        if (document && proof) {
            prevouts.push_back(i < proof->prevouts.size() ? proof->prevouts[i] : std::nullopt);
        } else {
            preview.debit += wallet.getDebit(input);
            const auto parent = wallet.getTx(input.prevout.hash);
            if (parent && input.prevout.n < parent->vout.size()) prevouts.emplace_back(parent->vout[input.prevout.n]);
            else if (document && i < document->inputs.size() && !document->inputs[i].witness_utxo.IsNull()) prevouts.emplace_back(document->inputs[i].witness_utxo);
            else if (document && i < document->inputs.size() && document->inputs[i].non_witness_utxo &&
                input.prevout.n < document->inputs[i].non_witness_utxo->vout.size()) prevouts.emplace_back(document->inputs[i].non_witness_utxo->vout[input.prevout.n]);
            else prevouts.emplace_back(std::nullopt);
        }
    }
    for (const auto& output : transaction->vout) {
        const bool owned = wallet.txoutIsMine(output);
        preview.txout_is_mine.push_back(owned);
        const bool recipient_output = std::find(recipient_scripts.begin(), recipient_scripts.end(), output.scriptPubKey) != recipient_scripts.end();
        preview.txout_is_change.push_back(owned && !recipient_output);
    }
    auto flow = BuildTransactionFlow(preview, prevouts);
    if (flow.isEmpty()) return flow;
    if (document) {
        const bool known = proof && proof->amount.has_value();
        flow.insert(QStringLiteral("feeKnown"), known);
        flow.insert(QStringLiteral("feeSat"), known ? QVariant{qint64(*proof->amount)} : QVariant{});
    }
    const auto unit = QmlBitcoinUnits::fromDisplayUnit(display_unit);
    const auto format = [unit](CAmount amount) -> QString { return QmlBitcoinUnits::formatForDisplay(unit, amount) + QLatin1Char(' ') + QmlBitcoinUnits::label(unit, amount); };
    for (const auto& side : {QStringLiteral("inputs"), QStringLiteral("outputs")}) {
        auto rows = flow.value(side).toList();
        for (auto& value : rows) {
            auto row = value.toMap();
            if (row.value(QStringLiteral("amountKnown")).toBool()) row.insert(QStringLiteral("amount"), format(row.value(QStringLiteral("amountSat")).toLongLong()));
            if (side == QStringLiteral("inputs") && row.value(QStringLiteral("ownership")).toString() == QStringLiteral("wallet")) {
                const auto destination = DecodeDestination(row.value(QStringLiteral("address")).toString().toStdString());
                std::string label;
                if (IsValidDestination(destination) && wallet.getAddress(destination, &label, nullptr) && !label.empty()) row.insert(QStringLiteral("label"), QString::fromStdString(label));
            }
            if (side == QStringLiteral("outputs")) {
                const auto label = reviewed_notes.value(row.value(QStringLiteral("address")).toString());
                if (!label.isEmpty()) row.insert(QStringLiteral("label"), label);
            }
            value = row;
        }
        flow.insert(side, rows);
    }
    if (flow.value(QStringLiteral("feeKnown")).toBool()) flow.insert(QStringLiteral("feeAmount"), format(flow.value(QStringLiteral("feeSat")).toLongLong()));
    flow.insert(QStringLiteral("presentation"), PrepareTransactionFlowPresentation(flow));
    return flow;
}

ImportResult PrepareReview(interfaces::Wallet& wallet, std::shared_ptr<const PartiallySignedTransaction> document,
                          const Context& context, int display_unit)
{
    ImportResult result;
    try {
        if (!document) { result.message = Tr("No PSBT is loaded."); return result; }
        const auto original = document->GetUnsignedTx();
        if (!original || original->vin.empty() || original->vout.empty() || document->inputs.size() != original->vin.size() || document->outputs.size() != original->vout.size()) {
            result.message = Tr("The PSBT is malformed."); return result;
        }
        // Legacy scriptSig data changes the final transaction identity. Inspect
        // a copy so the known-status query cannot finalize the imported value.
        auto identity_document = *document;
        CMutableTransaction finalized;
        const auto identity = FinalizeAndExtractPSBT(identity_document, finalized)
            ? finalized.GetHash() : original->GetHash();
        result.txid = QString::fromStdString(identity.ToString());
        if (wallet.getTx(identity)) { result.disposition = ImportResult::Disposition::Known; return result; }
        std::shared_ptr<const PartiallySignedTransaction> assessment;
        const auto snapshot = Inspect(&wallet, document, {}, context, {}, &assessment);
        if (!snapshot->document || !assessment || !snapshot->proof || !snapshot->proof->outputs_valid) {
            result.message = Tr("The transaction fee is missing or invalid."); return result;
        }
        // A failed metadata fill is not signing authority. Preserve the exact
        // imported document for a later explicit retry rather than adopting it.
        if (snapshot->metadata_failed) { result.message = snapshot->error; return result; }
        const auto tx = assessment->GetUnsignedTx();
        if (!tx || !MatchesUnsignedTransaction(*original, *tx)) { result.message = Tr("The wallet changed the reviewed transaction."); return result; }
        auto completed_assessment = *assessment;
        const bool extracted = FinalizeAndExtractPSBT(completed_assessment, finalized);
        if (snapshot->signature_state == SignatureState::Verified && !extracted) { result.message = Tr("The analyzed transaction could not be extracted."); return result; }
        if (extracted && finalized.GetHash() != identity) {
            result.txid = QString::fromStdString(finalized.GetHash().ToString());
            if (wallet.getTx(finalized.GetHash())) { result.disposition = ImportResult::Disposition::Known; return result; }
        }
        const bool owned_inputs = std::all_of(tx->vin.begin(), tx->vin.end(), [&](const CTxIn& input) { return wallet.txinIsMine(input); });
        std::optional<std::pair<int, int>> multisig;
        if (!snapshot->complete) for (size_t i = 0; i < assessment->inputs.size(); ++i) if ((multisig = Multisig(*assessment, i))) break;
        const bool signer = !wallet.privateKeysDisabled() || wallet.hasExternalSigner();
        result.key_can_send = !multisig && owned_inputs && signer && snapshot->unsigned_inputs > 0 && snapshot->could_sign >= snapshot->unsigned_inputs;
        result.can_commit_complete = !multisig && owned_inputs;
        result.key_can_broadcast = snapshot->can_broadcast;
        const bool may_send = result.can_commit_complete && (snapshot->complete || result.key_can_send);
        auto review = std::make_shared<walletreview::Snapshot>();
        review->transaction = snapshot->signature_state == SignatureState::Verified ? MakeTransactionRef(finalized) : MakeTransactionRef(*tx);
        review->assessment = assessment;
        review->node_policy_blocked = snapshot->node_state == psbtpolicy::State::Blocked;
        if (snapshot->signature_state == SignatureState::Verified) review->node_policy_status = psbtpolicy::Evaluate(snapshot->node_facts).explanation;
        review->proof = snapshot->proof;
        review->fee = snapshot->proof->amount.value_or(0);
        review->display_unit = display_unit;
        review->can_send = snapshot->proof->amount.has_value() && may_send
            && psbtpolicy::Evaluate(snapshot->node_facts).state != psbtpolicy::State::Blocked;
        for (const auto& output : tx->vout) {
            CTxDestination destination;
            if (!ExtractDestination(output.scriptPubKey, destination)) {
                if (output.nValue == 0 && output.scriptPubKey.IsUnspendable()) continue;
                result.message = Tr("Only PSBTs with standard address outputs are supported right now."); return result;
            }
            // Imported documents do not provide authoritative change intent.
            // Ownership alone cannot distinguish a self-transfer recipient
            // from change, and signing capability must not alter the review.
            const auto address = QString::fromStdString(EncodeDestination(destination));
            std::string label;
            wallet.getAddress(destination, &label, nullptr);
            if (!MoneyRange(output.nValue) || output.nValue > MAX_MONEY - review->amount) { result.message = Tr("The PSBT contains invalid amounts."); return result; }
            review->amount += output.nValue;
            review->recipients.append({address, QString::fromStdString(label), output.nValue, false});
            review->destinations.insert(address);
            review->notes.insert(address, QString::fromStdString(label));
        }
        if (review->recipients.empty()) { result.message = Tr("The PSBT does not have any recipient outputs to review."); return result; }
        if (review->recipients.size() > 25) { result.message = Tr("The PSBT has more recipients than this send flow supports."); return result; }
        if (review->amount <= 0) { result.message = Tr("The PSBT does not send a positive amount."); return result; }
        review->presentation = walletreview::FormatRecipients(review->recipients, display_unit);
        review->flow = CollectReviewFlow(wallet, review->transaction, assessment, snapshot->proof, review->destinations, display_unit, review->notes);
        auto output_rows = review->flow.value(QStringLiteral("outputs")).toList();
        for (auto& value : output_rows) {
            auto row = value.toMap();
            row.insert(QStringLiteral("changeKnown"), false);
            value = row;
        }
        review->flow.insert(QStringLiteral("outputs"), output_rows);
        review->flow.insert(QStringLiteral("presentation"), PrepareTransactionFlowPresentation(review->flow));
        if (!snapshot->proof->amount) result.message = Tr("The transaction fee is missing or invalid. Add valid input information before broadcasting.");
        else if (multisig) result.message = Tr("This transaction requires %1 of %2 signatures.").arg(multisig->first).arg(multisig->second);
        else if (!review->can_send && !snapshot->complete) result.message = Tr("This wallet does not have the keys to sign this transaction.");
        result.document = snapshot->document;
        result.review = std::move(review);
        result.disposition = ImportResult::Disposition::Review;
    } catch (...) { result.message = Tr("The PSBT could not be reviewed."); }
    return result;
}
} // namespace psbtwork
