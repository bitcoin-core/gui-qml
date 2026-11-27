// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletsend.h>
#include <qml/retainedvalue.h>
#include <qml/models/signertransaction.h>
#include <qml/psbtsignatures.h>

#include <QScopeGuard>

#include <set>
#include <exception>

namespace walletsend {
namespace {
Result FailureResult(Error error)
{
    Result result;
    result.error = error;
    return result;
}
bool SameTransaction(const CTransaction& reviewed, const CMutableTransaction& candidate)
{
    CMutableTransaction original{reviewed}, returned{candidate};
    for (auto* transaction : {&original, &returned}) for (auto& input : transaction->vin) {
        input.scriptSig.clear(); input.scriptWitness.SetNull();
    }
    return original.GetHash() == returned.GetHash();
}
bool HasDuplicateInputs(const CTransaction& transaction)
{
    std::set<COutPoint> inputs;
    for (const auto& input : transaction.vin) {
        if (!inputs.insert(input.prevout).second) return true;
    }
    return false;
}
bool KnownOutputs(interfaces::Wallet& wallet, const std::vector<COutPoint>& outputs)
{
    // Pinned getCoins does not bounds-check an index in a known transaction.
    // Keep malformed/stale identifiers outside that public call.
    for (const auto& output : outputs) {
        const auto transaction = wallet.getTx(output.hash);
        if (!transaction || output.n >= transaction->vout.size()) return false;
    }
    return true;
}
bool VerifiedSignatures(const PartiallySignedTransaction& document)
{
    const auto data = PrecomputePSBTData(document);
    if (!data || document.inputs.empty()) return false;
    for (size_t i = 0; i < document.inputs.size(); ++i) {
        if (!PSBTInputSignedAndVerified(document, static_cast<unsigned int>(i), &*data)) return false;
    }
    return true;
}
std::optional<psbtfee::Analysis> AuthenticatedInputs(interfaces::Wallet& wallet,
    const PartiallySignedTransaction& document, const psbtfee::Analysis& proof)
{
    auto verified = proof.verified;
    if (proof.wallet_prepared) {
        const auto transaction = document.GetUnsignedTx();
        if (!transaction) return {};
        std::vector<COutPoint> outputs;
        outputs.reserve(transaction->vin.size());
        for (const auto& input : transaction->vin) outputs.push_back(input.prevout);
        try {
            if (!KnownOutputs(wallet, outputs)) return {};
            const auto coins = wallet.getCoins(outputs);
            if (coins.size() != outputs.size()) return {};
            for (size_t i = 0; i < outputs.size(); ++i) {
                if (coins[i].txout.IsNull()) return {};
                verified.insert_or_assign(outputs[i], coins[i].txout);
            }
        } catch (...) { return {}; }
    }
    auto result = psbtfee::Analyze(document, verified);
    if (result.state != psbtfee::State::Known || result.amount != proof.amount) return {};
    return result;
}
bool VerifiedExternalTransaction(interfaces::Wallet& wallet, const CTransaction& transaction)
{
    try {
        std::vector<COutPoint> outputs;
        outputs.reserve(transaction.vin.size());
        for (const auto& input : transaction.vin) outputs.push_back(input.prevout);
        if (!KnownOutputs(wallet, outputs)) return false;
        const auto coins = wallet.getCoins(outputs);
        if (outputs.empty() || coins.size() != outputs.size()) return false;
        PartiallySignedTransaction document{CMutableTransaction{transaction}, 2};
        for (size_t i = 0; i < outputs.size(); ++i) {
            if (coins[i].txout.IsNull()) return false;
            document.inputs[i].witness_utxo = coins[i].txout;
            document.inputs[i].final_script_sig = transaction.vin[i].scriptSig;
            document.inputs[i].final_script_witness = transaction.vin[i].scriptWitness;
        }
        return VerifiedSignatures(document) && psbtsignatures::CommitsToAllOutputs(document);
    } catch (...) { return false; }
}
struct Observation {
    Record record{Record::Unknown};
    Submission submission{Submission::Unknown};
};
Observation ObserveSubmission(interfaces::Wallet& wallet, const CTransaction& transaction)
{
    try {
        interfaces::WalletTxStatus status{}; bool in_mempool{false}; int num_blocks{0};
        std::vector<std::string> messages, payment_requests;
        const auto record = wallet.getWalletTxDetails(transaction.GetHash(), status, messages, payment_requests, in_mempool, num_blocks);
        if (!record.tx || record.tx->GetHash() != transaction.GetHash()) return {Record::AbsentAtObservation, Submission::Unknown};
        if (status.depth_in_main_chain > 0) return {Record::Recorded, Submission::Confirmed};
        if (status.depth_in_main_chain < 0) return {Record::Recorded, Submission::Conflicted};
        if (status.is_abandoned) return {Record::Recorded, Submission::Abandoned};
        return {Record::Recorded, in_mempool ? Submission::Mempool : Submission::NotInMempool};
    } catch (...) { return {}; }
}
Result Execute(interfaces::Wallet& wallet, Request request)
{
    Result result;
    result.candidate = request.reviewed;
    bool relock_required{false};
    auto restoreWalletLock = [&] {
        if (!std::exchange(relock_required, false)) return;
        try {
            if (!wallet.lock()) result.relock_failed = true;
        } catch (...) {
            result.relock_failed = true;
        }
    };
    const auto restore_lock_on_exit = qScopeGuard([&] { restoreWalletLock(); });
    try {
        if (request.consent && request.consent->withdrawn()) { result.error = Error::Cancelled; return result; }
        if (request.captured_passphrase) {
            request.passphrase = secretinput::Materialize(request.captured_passphrase);
            request.captured_passphrase.reset();
        }
        CTransactionRef transaction = request.reviewed;
        if (request.psbt) {
            if (!request.reviewed || HasDuplicateInputs(*request.reviewed)) {
                result.error = Error::InvalidStructure; return result;
            }
            if (!request.proof || !psbtfee::MatchesReview(*request.psbt, *request.proof, request.approved_fee)) {
                result.error = Error::InvalidFee; return result;
            }
            const auto authenticated = AuthenticatedInputs(wallet, *request.psbt, *request.proof);
            if (!authenticated) { result.error = Error::InvalidFee; return result; }
            if (request.assessed_complete && !request.assessment) { result.error = Error::ChangedTransaction; return result; }
            if (request.assessed_complete) {
                const auto terms = request.assessment->GetUnsignedTx();
                if (!terms || !SameTransaction(*request.reviewed, *terms)) { result.error = Error::ChangedTransaction; return result; }
                if (!psbtfee::MatchesReview(*request.assessment, *authenticated, request.approved_fee)) { result.error = Error::InvalidFee; return result; }
            }
            PartiallySignedTransaction psbt{request.assessed_complete ? *request.assessment : *request.psbt};
            psbtfee::FillMissing(psbt, *authenticated);
            CMutableTransaction extracted;
            auto finalized = psbt;
            bool extracted_complete = FinalizeAndExtractPSBT(finalized, extracted);
            if (request.assessed_complete && !extracted_complete) { result.error = Error::Incomplete; return result; }
            if (!extracted_complete) {
                if (wallet.privateKeysDisabled() && !wallet.hasExternalSigner()) { result.error = Error::CannotSign; return result; }
                if (!wallet.privateKeysDisabled() && wallet.isCrypted() && wallet.isLocked()) {
                    if (!request.passphrase) { result.error = Error::NeedsUnlock; return result; }
                    relock_required = true;
                    if (!wallet.unlock(*request.passphrase)) { relock_required = false; result.error = Error::IncorrectPassphrase; return result; }
                }
                // Drop the submitted secret before a potentially blocking signer.
                request.passphrase.reset();
                bool complete{false}; size_t signed_inputs{0};
                result.signer_error = wallet.fillPSBT({.sign = true, .bip32_derivs = true}, &signed_inputs, psbt, complete);
                if (result.signer_error) { result.error = Error::Signer; restoreWalletLock(); return result; }
                const auto returned = psbt.GetUnsignedTx();
                if (!returned || !SameTransaction(*request.reviewed, *returned)) { result.error = Error::ChangedTransaction; restoreWalletLock(); return result; }
                if (!complete || !FinalizeAndExtractPSBT(psbt, extracted)) { result.error = Error::Incomplete; restoreWalletLock(); return result; }
                restoreWalletLock();
            }
            if (!SameTransaction(*request.reviewed, extracted)) { result.error = Error::ChangedTransaction; return result; }
            if (request.assessed_complete && request.reviewed->GetWitnessHash() != MakeTransactionRef(extracted)->GetWitnessHash()) { result.error = Error::ChangedTransaction; return result; }
            if (!psbtfee::MatchesReview(psbt, *authenticated, request.approved_fee)) { result.error = Error::InvalidFee; return result; }
            if (!VerifiedSignatures(psbt)) { result.error = Error::InvalidSignature; return result; }
            if (!psbtsignatures::CommitsToAllOutputs(psbt)) { result.error = Error::UncommittedOutputs; return result; }
            transaction = MakeTransactionRef(std::move(extracted));
        } else if (wallet.privateKeysDisabled()) {
            if (!wallet.hasExternalSigner()) { result.error = Error::CannotSign; return result; }
            // A prepared external-wallet transaction is unsigned until an
            // explicit approval supplies valid signatures. A failed approval
            // must not make a later direct Send commit that original value.
            if (!VerifiedExternalTransaction(wallet, *transaction)) {
                result.error = Error::NeedsSignerApproval; return result;
            }
        }
        request.passphrase.reset();
        if (result.relock_failed) { result.error = Error::Uncertain; return result; }
        if (request.sweeps_wallet && !request.full_wallet_consent) { result.error = Error::Cancelled; return result; }
        if (request.policy && request.approved_fee) {
            const auto decision = psbtpolicy::Evaluate(request.policy(transaction, *request.approved_fee));
            if (decision.state == psbtpolicy::State::Blocked) {
                result.error = Error::NodePolicy; result.policy_error = decision.explanation; return result;
            }
        }
        // Core retains the supplied shared_ptr in wallet history. Its owner
        // must not be an app cleanup lease: that would make session retirement
        // wait for a transaction whose destruction waits for wallet retirement.
        // Copy only on Native, before the original public commit boundary.
        transaction = MakeTransactionRef(*transaction);
        if (request.consent && !request.consent->claim()) { result.error = Error::Cancelled; return result; }
        wallet.commitTransaction(transaction, {});
        result.committed = std::move(transaction);
        // The public commit API has no result value. Preserve the exact effect
        // candidate, then report only what a subsequent wallet read proves.
        const auto observed = ObserveSubmission(wallet, *result.committed);
        result.record = observed.record;
        result.submission = observed.submission;
    } catch (const secretinput::Failure&) { restoreWalletLock(); result.error = Error::Rejected; }
    catch (...) { restoreWalletLock(); result.error = Error::Uncertain; }
    return result;
}
Result ExecuteSignOnly(interfaces::Wallet& wallet, Request request,
                       const std::shared_ptr<PartiallySignedTransaction>& document_owner)
{
    Result result;
    result.candidate = request.reviewed;
    bool relock_required{false};
    const auto restoreWalletLock = [&] {
        if (!std::exchange(relock_required, false)) return;
        try {
            if (!wallet.lock()) result.relock_failed = true;
        } catch (...) {
            result.relock_failed = true;
        }
    };
    const auto restore_lock_on_exit = qScopeGuard([&] { restoreWalletLock(); });
    try {
        if (!request.reviewed || !document_owner || HasDuplicateInputs(*request.reviewed)) {
            result.error = request.reviewed ? Error::InvalidStructure : Error::Rejected;
            return result;
        }
        if (!request.proof || !request.approved_fee ||
            (request.consent && request.consent->withdrawn())) {
            result.error = request.consent && request.consent->withdrawn() ? Error::Cancelled : Error::InvalidFee;
            return result;
        }
        if (request.captured_passphrase) {
            request.passphrase = secretinput::Materialize(request.captured_passphrase);
            request.captured_passphrase.reset();
        }
        // Hardware signing has its own explicit review action. This command is
        // reserved for locally held signing keys and never schedules commit.
        if (wallet.privateKeysDisabled()) { result.error = Error::CannotSign; return result; }

        CMutableTransaction unsigned_tx{*request.reviewed};
        for (auto& input : unsigned_tx.vin) { input.scriptSig.clear(); input.scriptWitness.SetNull(); }
        PartiallySignedTransaction document = request.psbt ? *request.psbt : PartiallySignedTransaction{unsigned_tx, 2};
        const auto document_tx = document.GetUnsignedTx();
        if (!document_tx || !SameTransaction(*request.reviewed, *document_tx)) {
            result.error = Error::ChangedTransaction; return result;
        }
        if (!psbtfee::MatchesReview(document, *request.proof, request.approved_fee)) {
            result.error = Error::InvalidFee; return result;
        }
        const auto authenticated = AuthenticatedInputs(wallet, document, *request.proof);
        if (!authenticated) { result.error = Error::InvalidFee; return result; }
        psbtfee::FillMissing(document, *authenticated);
        const auto approved = document;

        if (wallet.isCrypted() && wallet.isLocked()) {
            if (!request.passphrase) { result.error = Error::NeedsUnlock; return result; }
            relock_required = true;
            if (!wallet.unlock(*request.passphrase)) {
                relock_required = false;
                result.error = Error::IncorrectPassphrase;
                return result;
            }
        }
        // The one-use secret is gone before entering the potentially blocking
        // wallet signer. The captured unsigned transaction remains unchanged.
        request.passphrase.reset();
        size_t signed_inputs{0};
        bool complete{false}, signer_threw{false};
        std::optional<common::PSBTError> signer_error;
        try { signer_error = wallet.fillPSBT({.sign = true, .bip32_derivs = true}, &signed_inputs, document, complete); }
        catch (...) { signer_threw = true; }

        const auto returned = document.GetUnsignedTx();
        if (!returned || !SameTransaction(*request.reviewed, *returned)) {
            restoreWalletLock(); result.error = Error::ChangedTransaction; return result;
        }
        if (!psbtfee::MatchesReview(document, *authenticated, request.approved_fee)) {
            restoreWalletLock(); result.error = Error::InvalidFee; return result;
        }
        complete = FinalizePSBT(document);
        if (!psbtsignatures::VerifyFinalizedInputs(document, complete)) {
            restoreWalletLock(); result.error = Error::InvalidSignature; return result;
        }
        result.signature_status = complete ? psbtsignatures::Status::Valid
            : psbtsignatures::VerifyContributions(approved, document);
        if (result.signature_status != psbtsignatures::Status::Valid) {
            if (result.signature_status == psbtsignatures::Status::Unknown) {
                *document_owner = std::move(document);
                result.quarantined_document = std::shared_ptr<const PartiallySignedTransaction>{document_owner, document_owner.get()};
            }
            restoreWalletLock();
            result.error = result.relock_failed || signer_threw ? Error::Uncertain
                : result.signature_status == psbtsignatures::Status::Invalid ? Error::InvalidSignature
                : Error::UnverifiedSignatures;
            result.signer_error = signer_error;
            return result;
        }
        if (complete) {
            auto analyzed = document;
            CMutableTransaction extracted;
            if (!FinalizeAndExtractPSBT(analyzed, extracted)) { restoreWalletLock(); result.error = Error::Incomplete; return result; }
            result.signed_transaction = MakeTransactionRef(std::move(extracted));
        }
        *document_owner = std::move(document);
        result.document = std::shared_ptr<const PartiallySignedTransaction>{document_owner, document_owner.get()};
        result.partial = !complete;
        result.signed_inputs = signed_inputs;
        restoreWalletLock();
        if (result.relock_failed || signer_threw) result.error = Error::Uncertain;
        else if (signer_error) { result.error = Error::Signer; result.signer_error = signer_error; }
    } catch (const secretinput::Failure&) {
        restoreWalletLock(); result.error = Error::Rejected;
    } catch (...) {
        restoreWalletLock(); result.error = Error::Uncertain;
    }
    return result;
}
} // namespace

bool VerifyExternalTransaction(interfaces::Wallet& wallet, const CTransaction& transaction)
{
    return VerifiedExternalTransaction(wallet, transaction);
}

execution::SubmitResult SendTask::start(Request request, std::shared_ptr<const walletopening::Session::Action> action)
{
    {
        std::lock_guard lock{m_mutex};
        if (std::exchange(m_started, true) || !request.reviewed) return {{}, execution::Rejection::Invalid};
    }
    if (action && action->identity() != m_opening->identity()) return {{}, execution::Rejection::Invalid};
    if (!action) action = m_opening->startAction();
    if (!action) { complete(FailureResult(Error::Rejected)); return {{}, execution::Rejection::Closed}; }
    auto native_result = walletopening::RetainValues<CTransactionRef>(m_opening, [] { return std::make_unique<CTransactionRef>(); });
    if (!native_result) { complete(FailureResult(Error::Rejected)); return {{}, execution::Rejection::Closed}; }
    std::shared_ptr<PartiallySignedTransaction> native_document_result;
    if (request.intent == Intent::SignOnly) {
        native_document_result = walletopening::RetainValues<PartiallySignedTransaction>(m_opening, [] {
            return std::make_unique<PartiallySignedTransaction>(CMutableTransaction{}, 2);
        });
        if (!native_document_result) { complete(FailureResult(Error::Rejected)); return {{}, execution::Rejection::Closed}; }
    }
    {
        std::lock_guard lock{m_mutex};
        m_request = std::move(request);
        m_attempt_id = m_request.attempt_id;
        m_review_id = m_request.review_id;
        m_action = std::move(action);
        m_native_result = std::move(native_result);
        m_native_document_result = std::move(native_document_result);
    }
    const auto self = shared_from_this();
    return plan([self] { self->beginOnDomain(); });
}

execution::SubmitResult SendTask::plan(std::function<void()> work)
{
    std::shared_ptr<const walletopening::Session::Action> action;
    { std::lock_guard lock{m_mutex}; action = m_action; }
    if (!action) return {{}, execution::Rejection::Closed};
    return m_opening->planAction(action, std::move(work));
}

void SendTask::beginOnDomain()
{
    Notice notice{Notice::SweepConsentRequired};
    bool wait_for_sweep{false}, cancelled{false};
    {
        std::lock_guard lock{m_mutex};
        if (m_stage != Stage::Idle || m_result) return;
        cancelled = m_request.consent && m_request.consent->withdrawn();
        if (cancelled) {
            // Completion happens after releasing the operation lock.
        } else if (m_request.intent == Intent::SignOnly) {
            m_stage = Stage::SigningOnly;
        } else if (m_request.sweeps_wallet && !m_request.full_wallet_consent) {
            m_stage = Stage::AwaitingSweepConsent;
            wait_for_sweep = true;
        } else {
            m_stage = Stage::Submitting;
        }
    }
    if (cancelled) { complete(FailureResult(Error::Cancelled)); return; }
    if (wait_for_sweep) {
        try { m_notify(notice); } catch (...) { publicationFailed(); }
        return;
    }
    dispatchNative();
}

void SendTask::dispatchNative(secretinput::Captured passphrase)
{
    Request request;
    std::shared_ptr<const walletopening::Session::Action> action;
    std::shared_ptr<CTransactionRef> native_result;
    std::shared_ptr<PartiallySignedTransaction> native_document_result;
    {
        std::lock_guard lock{m_mutex};
        if ((m_stage != Stage::Submitting && m_stage != Stage::SigningOnly) || m_result) return;
        // Preserve the reviewed values across a NeedsUnlock outcome. Only the
        // one-use credential is moved into this Native invocation.
        request.attempt_id = m_request.attempt_id;
        request.review_id = m_request.review_id;
        request.intent = m_request.intent;
        request.reviewed = m_request.reviewed;
        request.psbt = m_request.psbt;
        request.assessment = m_request.assessment;
        request.assessed_complete = m_request.assessed_complete;
        request.proof = m_request.proof;
        request.approved_fee = m_request.approved_fee;
        request.passphrase = std::move(m_request.passphrase);
        request.captured_passphrase = passphrase ? std::move(passphrase) : std::move(m_request.captured_passphrase);
        request.consent = m_request.consent;
        request.policy = m_request.policy;
        request.sweeps_wallet = m_request.sweeps_wallet;
        request.full_wallet_consent = m_request.full_wallet_consent;
        request.notes = m_request.notes;
        action = m_action;
        native_result = m_native_result;
        native_document_result = m_native_document_result;
    }
    const auto self = shared_from_this();
    const auto admitted = m_opening->perform(action,
        [request = std::move(request), native_result, native_document_result](interfaces::Wallet& wallet) mutable {
            auto result = request.intent == Intent::SignOnly
                ? ExecuteSignOnly(wallet, std::move(request), native_document_result)
                : Execute(wallet, std::move(request));
            if (result.committed) {
                *native_result = result.committed;
                result.committed = CTransactionRef{native_result, native_result->get()};
            } else if (result.signed_transaction) {
                *native_result = result.signed_transaction;
                result.signed_transaction = CTransactionRef{native_result, native_result->get()};
            }
            return result;
        }, [self](Result result) { self->nativeCompleted(std::move(result)); },
        [self](std::exception_ptr error) {
            try { std::rethrow_exception(error); }
            catch (const execution::NotStarted&) { self->nativeCompleted(FailureResult(Error::Rejected)); }
            catch (...) { self->nativeCompleted(FailureResult(Error::Uncertain)); }
        });
    if (!admitted) nativeCompleted(FailureResult(Error::Rejected));
}

void SendTask::nativeCompleted(Result result)
{
    if (!result.candidate) {
        std::lock_guard lock{m_mutex};
        result.candidate = m_request.reviewed;
    }
    if (result.error == Error::NeedsUnlock || result.error == Error::IncorrectPassphrase) {
        {
            std::lock_guard lock{m_mutex};
            if (m_result || (m_stage != Stage::Submitting && m_stage != Stage::SigningOnly)) return;
            m_prompt_error = result.error;
            m_stage = Stage::AwaitingCredentials;
        }
        try { m_notify(Notice::CredentialsRequired); } catch (...) { publicationFailed(); }
        return;
    }
    bool sign_only{false};
    std::shared_ptr<const walletopening::Session::Action> action;
    QMap<QString, QString> notes;
    {
        std::lock_guard lock{m_mutex};
        sign_only = m_request.intent == Intent::SignOnly;
        action = m_action;
        notes = m_request.notes;
    }
    if (sign_only) {
        complete(std::move(result));
        return;
    }
    result.notes_requested = !notes.isEmpty();
    if (result.committed && result.notes_requested) {
        if (result.record != Record::Recorded) {
            result.notes_rejection = execution::Rejection::Transport;
        } else if (m_notes) {
            try {
                auto accepted = m_notes->submitSentLabels(action, result.committed->GetHash(), notes,
                    [notify = m_notify, weak = std::weak_ptr<SendTask>{shared_from_this()}] {
                        try { notify(Notice::Notes); }
                        catch (...) { if (const auto owner = weak.lock()) owner->publicationFailed(); }
                    });
                result.notes = std::move(accepted.operation); result.notes_rejection = accepted.rejection;
            } catch (...) { result.notes_rejection = execution::Rejection::Transport; }
        } else result.notes_rejection = execution::Rejection::Closed;
    }
    complete(std::move(result));
}

execution::SubmitResult SendTask::confirmSweep(uint64_t attempt_id, uint64_t review_id)
{
    std::shared_ptr<const walletopening::Session::Action> action;
    {
        std::lock_guard lock{m_mutex};
        if (attempt_id == 0 || review_id == 0 || attempt_id != m_attempt_id || review_id != m_review_id ||
            m_stage != Stage::AwaitingSweepConsent || m_result || m_continuation_pending) return {{}, execution::Rejection::Invalid};
        m_continuation_pending = true;
        action = m_action;
    }
    const auto self = shared_from_this();
    const auto started = m_opening->planAction(action, [self, attempt_id, review_id] {
        bool accepted{false}, cancelled{false};
        {
            std::lock_guard lock{self->m_mutex};
            if (self->m_stage != Stage::AwaitingSweepConsent || self->m_result ||
                self->m_attempt_id != attempt_id || self->m_review_id != review_id) { self->m_continuation_pending = false; return; }
            self->m_continuation_pending = false;
            cancelled = self->m_request.consent && self->m_request.consent->withdrawn();
            if (!cancelled) {
                self->m_request.full_wallet_consent = true;
                self->m_stage = self->m_request.intent == Intent::SignOnly ? Stage::SigningOnly : Stage::Submitting;
                accepted = true;
            }
        }
        if (cancelled) self->complete(FailureResult(Error::Cancelled));
        else if (accepted) self->dispatchNative();
    });
    if (!started) { std::lock_guard lock{m_mutex}; m_continuation_pending = false; }
    return started;
}

execution::SubmitResult SendTask::providePassphrase(uint64_t attempt_id, uint64_t review_id, secretinput::Captured passphrase)
{
    std::shared_ptr<const walletopening::Session::Action> action;
    {
        std::lock_guard lock{m_mutex};
        if (!passphrase || attempt_id == 0 || review_id == 0 || attempt_id != m_attempt_id || review_id != m_review_id ||
            m_stage != Stage::AwaitingCredentials || m_result || m_continuation_pending) return {{}, execution::Rejection::Invalid};
        m_continuation_pending = true;
        action = m_action;
    }
    const auto self = shared_from_this();
    const auto started = m_opening->planAction(action, [self, attempt_id, review_id, passphrase = std::move(passphrase)]() mutable {
        bool accepted{false}, cancelled{false};
        {
            std::lock_guard lock{self->m_mutex};
            if (self->m_stage != Stage::AwaitingCredentials || self->m_result ||
                self->m_attempt_id != attempt_id || self->m_review_id != review_id) { self->m_continuation_pending = false; return; }
            self->m_continuation_pending = false;
            cancelled = self->m_request.consent && self->m_request.consent->withdrawn();
            if (!cancelled) {
                self->m_prompt_error = Error::None;
                self->m_stage = self->m_request.intent == Intent::SignOnly ? Stage::SigningOnly : Stage::Submitting;
                accepted = true;
            }
        }
        if (cancelled) self->complete(FailureResult(Error::Cancelled));
        else if (accepted) self->dispatchNative(std::move(passphrase));
    });
    if (!started) { std::lock_guard lock{m_mutex}; m_continuation_pending = false; }
    return started;
}

execution::SubmitResult SendTask::cancelPrompt(uint64_t attempt_id, uint64_t review_id)
{
    std::shared_ptr<const walletopening::Session::Action> action;
    {
        std::lock_guard lock{m_mutex};
        if (attempt_id == 0 || review_id == 0 || attempt_id != m_attempt_id || review_id != m_review_id ||
            (m_stage != Stage::AwaitingSweepConsent && m_stage != Stage::AwaitingCredentials) || m_result || m_continuation_pending)
            return {{}, execution::Rejection::Invalid};
        m_continuation_pending = true;
        action = m_action;
    }
    const auto self = shared_from_this();
    const auto started = m_opening->planAction(action, [self, attempt_id, review_id] {
        {
            std::lock_guard lock{self->m_mutex};
            if (self->m_result || self->m_attempt_id != attempt_id || self->m_review_id != review_id ||
                (self->m_stage != Stage::AwaitingSweepConsent && self->m_stage != Stage::AwaitingCredentials)) { self->m_continuation_pending = false; return; }
            self->m_continuation_pending = false;
            if (self->m_request.consent) self->m_request.consent->withdraw();
            self->m_stage = Stage::Finished;
        }
        self->complete(FailureResult(Error::Cancelled));
    });
    if (!started) { std::lock_guard lock{m_mutex}; m_continuation_pending = false; }
    return started;
}

Stage SendTask::stage() const { std::lock_guard lock{m_mutex}; return m_stage; }
Error SendTask::promptError() const { std::lock_guard lock{m_mutex}; return m_prompt_error; }

std::optional<Result> SendTask::result() const
{
    std::lock_guard lock{m_mutex}; return m_result;
}
void SendTask::complete(Result result)
{
    Request discarded;
    std::shared_ptr<const walletopening::Session::Action> retired_action;
    std::shared_ptr<CTransactionRef> retired_native_result;
    {
        std::lock_guard lock{m_mutex};
        if (m_result) return;
        if (!result.candidate) result.candidate = m_request.reviewed;
        if (result.candidate) {
            result.candidate_txid = result.candidate->GetHash();
            result.candidate_wtxid = result.candidate->GetWitnessHash();
        }
        if (!result.committed && !result.signed_transaction && !result.document && !result.quarantined_document) {
            // No returned artifact needs Native retention. The original
            // review remains with its view until retirement; retaining this
            // bounded failure receipt cannot retain that view's lease.
            result.candidate.reset();
        }
        m_stage = Stage::Finished;
        result.publication_failed |= m_publication_failed;
        m_result = std::move(result);
        discarded = std::move(m_request);
        retired_action = std::move(m_action);
        retired_native_result = std::move(m_native_result);
    }
    try { m_notify(Notice::Finished); }
    catch (...) { publicationFailed(); }
}
void SendTask::publicationFailed()
{
    std::lock_guard lock{m_mutex}; m_publication_failed = true;
    if (m_result) m_result->publication_failed = true;
}
} // namespace walletsend

namespace walletsigning {
static Result ExecuteApproval(interfaces::Wallet& wallet, const Request& request)
{
    Result result;
    if (!request.proof || !request.proof->amount || !request.approved_fee) {
        result.error = Error::FeeUnavailable; return result;
    }
    if (!request.reviewed) {
        result.error = Error::InvalidFee; return result;
    }
    if (!wallet.hasExternalSigner()) { result.error = Error::SignerNotFound; return result; }
    CMutableTransaction unsigned_tx{*request.reviewed};
    for (auto& input : unsigned_tx.vin) { input.scriptSig.clear(); input.scriptWitness.SetNull(); }
    PartiallySignedTransaction document = request.document ? *request.document
        : PartiallySignedTransaction{unsigned_tx, 2};
    if (!psbtfee::MatchesReview(document, *request.proof, request.approved_fee)) {
        result.error = Error::InvalidFee; return result;
    }
    // Authenticate input keys before the device can replace PSBT metadata.
    const auto authenticated = walletsend::AuthenticatedInputs(wallet, document, *request.proof);
    if (!authenticated) { result.error = Error::UnverifiedInputs; return result; }
    psbtfee::FillMissing(document, *authenticated);
    const auto approved = std::make_shared<const PartiallySignedTransaction>(document);
    bool complete{false};
    // An error can still carry useful partial work. Finalized fields
    // must verify against the captured inputs before retaining them.
    const auto retain_verified_partial = [&] {
        const auto returned = document.GetUnsignedTx();
        if (returned && MatchesUnsignedTransaction(unsigned_tx, *returned) &&
            psbtfee::MatchesReview(document, *authenticated, request.approved_fee) &&
            psbtsignatures::VerifyFinalizedInputs(document, /*require_all=*/false) &&
            psbtsignatures::CommitsToAllOutputs(document)) {
            result.signature_status = psbtsignatures::VerifyContributions(*approved, document);
            if (result.signature_status == psbtsignatures::Status::Valid) {
                result.document = std::make_shared<const PartiallySignedTransaction>(document);
                result.partial = true;
            } else if (result.signature_status == psbtsignatures::Status::Unknown) {
                result.quarantined_document = std::make_shared<const PartiallySignedTransaction>(document);
            }
        }
    };
    try {
        auto error = wallet.fillPSBT({.sign = false, .bip32_derivs = true}, nullptr, document, complete);
        if (!error && !complete) error = wallet.fillPSBT({.sign = true, .bip32_derivs = true}, nullptr, document, complete);
        if (error) {
            retain_verified_partial();
            result.fill_error = error;
            result.error = *error == common::PSBTError::EXTERNAL_SIGNER_NOT_FOUND ? Error::SignerNotFound
                : *error == common::PSBTError::EXTERNAL_SIGNER_FAILED ? Error::SignerFailed : Error::Fill;
            return result;
        }
        const auto returned = document.GetUnsignedTx();
        if (!returned || !MatchesUnsignedTransaction(unsigned_tx, *returned)) {
            result.document = approved;
            result.error = Error::ChangedTransaction; return result;
        }
        CMutableTransaction signed_tx;
        const bool extracted = FinalizeAndExtractPSBT(document, signed_tx);
        if (!psbtfee::MatchesReview(document, *authenticated, request.approved_fee)) {
            result.document = approved;
            result.error = Error::InvalidFee; return result;
        }
        if (!psbtsignatures::CommitsToAllOutputs(document)) {
            result.document = approved;
            result.error = Error::UncommittedOutputs; return result;
        }
        if (extracted) {
            if (!walletsend::VerifiedSignatures(document)) {
                // Finalization and a signer's completion flag do not prove
                // that the replacement can service the original input keys.
                result.document = approved;
                result.error = Error::InvalidSignature; return result;
            }
            result.signature_status = psbtsignatures::Status::Valid;
        } else {
            result.signature_status = psbtsignatures::VerifyContributions(*approved, document);
            if (result.signature_status != psbtsignatures::Status::Valid) {
                result.document = approved;
                result.error = result.signature_status == psbtsignatures::Status::Invalid
                    ? Error::InvalidSignature : Error::UnverifiedSignatures;
                if (result.signature_status == psbtsignatures::Status::Unknown)
                    result.quarantined_document = std::make_shared<const PartiallySignedTransaction>(std::move(document));
                return result;
            }
        }
        result.document = std::make_shared<const PartiallySignedTransaction>(std::move(document));
        result.partial = !extracted;
        if (extracted) result.signed_transaction = MakeTransactionRef(std::move(signed_tx));
    } catch (...) {
        result.error = Error::Uncertain;
        try {
            retain_verified_partial();
        } catch (...) {}
    }
    return result;
}
Result Approve(interfaces::Wallet& wallet, const Request& request, std::shared_ptr<walletopening::Session> opening)
{
    // Standalone backend tests may inspect native values entirely on their
    // controlled worker. Production reserves ownership before device execution.
    if (!opening) return ExecuteApproval(wallet, request);
    auto owner = walletopening::RetainValues<Result>(opening, [] { return std::make_unique<Result>(); });
    if (!owner) return Result{.error = Error::Failed};
    return ApproveInto(wallet, request, std::move(owner));
}
Result ApproveInto(interfaces::Wallet& wallet, const Request& request, std::shared_ptr<Result> owner)
{
    if (!owner) return Result{.error = Error::Failed};
    *owner = ExecuteApproval(wallet, request);
    Result result = *owner;
    result.lifetime = owner;
    if (result.document) result.document = std::shared_ptr<const PartiallySignedTransaction>{owner, result.document.get()};
    if (result.quarantined_document) result.quarantined_document = std::shared_ptr<const PartiallySignedTransaction>{owner, result.quarantined_document.get()};
    if (result.signed_transaction) result.signed_transaction = CTransactionRef{owner, result.signed_transaction.get()};
    return result;
}

struct ApprovalService::State {
    uint64_t sequence{0};
    uint64_t current_review{0}, current_view{0};
    uint64_t attempt_id{0}, attempt_review{0}, attempt_view{0};
    ApprovalPhase phase{ApprovalPhase::Idle};
    Error error{Error::None};
    bool signer_not_found{false}, in_flight{false}, publishable{false}, closed{false};
    std::shared_ptr<Request> request;
    std::shared_ptr<const walletopening::Session::Action> action;
    std::shared_ptr<Result> output;
    std::shared_ptr<const Result> result;
};

namespace {
bool IsStaleApproval(uint64_t attempt_id, uint64_t attempt_review, uint64_t attempt_view,
                     uint64_t current_review, uint64_t current_view)
{
    return attempt_id && (attempt_review != current_review || attempt_view != current_view);
}

Error ApprovalExecutionError(std::exception_ptr error)
{
    if (!error) return Error::Uncertain;
    try { std::rethrow_exception(error); }
    catch (const execution::NotStarted&) { return Error::Failed; }
    catch (...) { return Error::Uncertain; }
}
}

ApprovalService::ApprovalService(std::shared_ptr<walletopening::Session> opening, Notify notify)
    : m_opening(std::move(opening)), m_notify(std::move(notify)), m_state(std::make_shared<State>())
{
}

ApprovalSnapshot ApprovalService::project(const State& state)
{
    ApprovalSnapshot result;
    result.sequence = state.sequence;
    result.attempt_id = state.attempt_id;
    result.attempt_review = state.attempt_review;
    result.attempt_view = state.attempt_view;
    result.current_review = state.current_review;
    result.current_view = state.current_view;
    result.phase = state.phase;
    result.error = state.error;
    result.signer_not_found = state.signer_not_found;
    result.in_flight = state.in_flight;
    result.publishable = state.publishable && !state.closed && !IsStaleApproval(state.attempt_id,
        state.attempt_review, state.attempt_view, state.current_review, state.current_view);
    result.result = state.result;
    return result;
}

void ApprovalService::publish(const std::shared_ptr<State>& state, const Notify& notify)
{
    if (notify) notify(project(*state));
}

execution::SubmitResult ApprovalService::reviewChanged(uint64_t review_id, uint64_t view_revision)
{
    if (!m_opening || m_close_requested.load()) return {{}, execution::Rejection::Closed};
    const auto state = m_state;
    const auto notify = m_notify;
    return m_opening->postDomain([state, notify, review_id, view_revision] {
        if (state->closed || (review_id < state->current_review) ||
            (review_id == state->current_review && view_revision < state->current_view)) return;
        if (state->current_review == review_id && state->current_view == view_revision) return;
        state->current_review = review_id;
        state->current_view = view_revision;
        if (state->in_flight) {
            if (IsStaleApproval(state->attempt_id, state->attempt_review, state->attempt_view,
                                state->current_review, state->current_view)) {
                state->phase = ApprovalPhase::Cancelling;
                state->publishable = false;
            }
        } else if (IsStaleApproval(state->attempt_id, state->attempt_review, state->attempt_view,
                                   state->current_review, state->current_view)) {
            state->attempt_id = 0;
            state->attempt_review = review_id;
            state->attempt_view = view_revision;
            state->phase = state->closed ? ApprovalPhase::Closed : ApprovalPhase::Idle;
            state->error = Error::None;
            state->signer_not_found = false;
            state->publishable = false;
            state->result.reset();
            state->request.reset();
            state->action.reset();
            state->output.reset();
        }
        ++state->sequence;
        publish(state, notify);
    });
}

execution::SubmitResult ApprovalService::request(uint64_t attempt_id, uint64_t review_id, uint64_t view_revision,
                                             std::shared_ptr<Request> retained_request)
{
    if (!m_opening || m_close_requested.load() || attempt_id == 0 || !retained_request)
        return {{}, execution::Rejection::Invalid};
    const auto opening = m_opening;
    const auto state = m_state;
    const auto notify = m_notify;
    return opening->postDomain([opening, state, notify, attempt_id, review_id, view_revision,
                                retained_request = std::move(retained_request)]() mutable {
        const auto reject = [&](Error error, ApprovalPhase phase = ApprovalPhase::Failed) {
            state->attempt_id = attempt_id;
            state->attempt_review = review_id;
            state->attempt_view = view_revision;
            state->phase = phase;
            state->error = error;
            state->signer_not_found = error == Error::SignerNotFound;
            state->in_flight = false;
            state->publishable = review_id == state->current_review && view_revision == state->current_view && !state->closed;
            state->result.reset();
            state->request.reset();
            state->action.reset();
            state->output.reset();
            ++state->sequence;
            publish(state, notify);
        };
        if (state->closed || !opening->available()) {
            reject(Error::Failed, ApprovalPhase::Closed);
            return;
        }
        if (state->in_flight || attempt_id <= state->attempt_id || state->phase == ApprovalPhase::Uncertain) {
            retained_request.reset();
            ++state->sequence;
            publish(state, notify);
            return;
        }
        if (state->current_review != review_id || state->current_view != view_revision) {
            reject(Error::Failed);
            return;
        }
        if (!retained_request->proof || !retained_request->proof->amount || !retained_request->approved_fee) {
            reject(Error::FeeUnavailable);
            return;
        }
        if (!retained_request->reviewed) {
            reject(Error::InvalidFee);
            return;
        }

        state->attempt_id = attempt_id;
        state->attempt_review = review_id;
        state->attempt_view = view_revision;
        state->phase = ApprovalPhase::Preparing;
        state->error = Error::None;
        state->signer_not_found = false;
        state->in_flight = true;
        state->publishable = false;
        state->result.reset();
        state->request = std::move(retained_request);
        ++state->sequence;
        publish(state, notify);

        state->action = opening->startAction();
        if (!state->action) {
            reject(Error::Failed, opening->available() ? ApprovalPhase::Failed : ApprovalPhase::Closed);
            return;
        }
        state->output = walletopening::RetainValues<Result>(opening, [] { return std::make_unique<Result>(); });
        if (!state->output) {
            reject(Error::Failed);
            return;
        }

        state->phase = ApprovalPhase::WaitingDevice;
        ++state->sequence;
        publish(state, notify);

        const auto action = state->action;
        const auto request = state->request;
        const auto output = state->output;
        const auto finish = [opening, state, notify, attempt_id](Result value) {
            if (state->attempt_id != attempt_id) return;
            state->phase = ApprovalPhase::Verifying;
            ++state->sequence;
            publish(state, notify);
            if (state->closed || !opening->observationOpen() || IsStaleApproval(state->attempt_id,
                    state->attempt_review, state->attempt_view, state->current_review, state->current_view)) {
                state->phase = state->closed || !opening->observationOpen() ? ApprovalPhase::Closed : ApprovalPhase::Idle;
                state->error = Error::None;
                state->signer_not_found = false;
                state->in_flight = false;
                state->publishable = false;
                state->result.reset();
                state->request.reset();
                state->action.reset();
                state->output.reset();
                ++state->sequence;
                publish(state, notify);
                return;
            }
            state->result = std::make_shared<const Result>(std::move(value));
            state->error = state->result->error;
            state->signer_not_found = state->error == Error::SignerNotFound;
            state->in_flight = false;
            state->publishable = true;
            state->phase = state->error == Error::Uncertain ? ApprovalPhase::Uncertain
                : state->result->quarantined_document ? ApprovalPhase::Unverified
                : state->error != Error::None ? ApprovalPhase::Failed
                : state->result->partial ? ApprovalPhase::Partial : ApprovalPhase::Complete;
            state->request.reset();
            state->action.reset();
            state->output.reset();
            ++state->sequence;
            publish(state, notify);
        };
        const auto failed = [finish](std::exception_ptr error) {
            Result value;
            value.error = ApprovalExecutionError(std::move(error));
            finish(std::move(value));
        };
        const auto started = opening->perform(action, [request, output](interfaces::Wallet& wallet) {
            return ApproveInto(wallet, *request, output);
        }, finish, failed);
        if (!started) {
            Result value;
            value.error = Error::Failed;
            finish(std::move(value));
        }
    });
}

execution::SubmitResult ApprovalService::close()
{
    if (m_close_requested.exchange(true)) return {{}, execution::Rejection::Invalid};
    if (!m_opening) return {{}, execution::Rejection::Closed};
    const auto state = m_state;
    const auto notify = m_notify;
    return m_opening->postDomain([state, notify] {
        if (state->closed) return;
        state->closed = true;
        state->publishable = false;
        if (state->in_flight) state->phase = ApprovalPhase::Cancelling;
        else state->phase = ApprovalPhase::Closed;
        state->result.reset();
        if (!state->in_flight) {
            state->request.reset();
            state->action.reset();
            state->output.reset();
        }
        ++state->sequence;
        publish(state, notify);
    });
}
} // namespace walletsigning
