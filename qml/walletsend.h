// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETSEND_H
#define BITCOIN_QML_WALLETSEND_H

#include <qml/models/psbtfee.h>
#include <qml/walletrequests.h>
#include <qml/secretinput.h>
#include <qml/psbtpolicy.h>
#include <qml/psbtsignatures.h>
#include <atomic>

namespace walletsend {
// Native-only: authenticate prevouts through the original public wallet and
// verify every finalized input before accepting an external signer's result.
bool VerifyExternalTransaction(interfaces::Wallet& wallet, const CTransaction& transaction);
enum class Submission { Unknown, Mempool, Confirmed, NotInMempool, Conflicted, Abandoned };
enum class Intent { Submit, SignOnly };
enum class Notice { SweepConsentRequired, CredentialsRequired, Finished, Notes };
enum class Stage { Idle, AwaitingSweepConsent, AwaitingCredentials, SigningOnly, Submitting, Finished };
enum class Record { Unknown, AbsentAtObservation, Recorded };
enum class Relay { Unknown };
enum class Error { None, Rejected, InvalidStructure, CannotSign, NeedsUnlock, IncorrectPassphrase, InvalidFee, ChangedTransaction, Incomplete, Signer, Uncertain, Cancelled, NeedsSignerApproval, InvalidSignature, NodePolicy, UnverifiedSignatures, UncommittedOutputs };
/** Explicit review withdrawal may win before the irreversible commit boundary.
 * Observer destruction does not withdraw an already admitted action. */
class CommitConsent {
public:
    bool withdrawn() const { return m_state.load() == Withdrawn; }
    bool claim() { State expected{Awaiting}; return m_state.compare_exchange_strong(expected, Committing); }
    void withdraw() { State expected{Awaiting}; m_state.compare_exchange_strong(expected, Withdrawn); }
private:
    enum State { Awaiting, Committing, Withdrawn };
    std::atomic<State> m_state{Awaiting};
};
struct Request {
    Intent intent{Intent::Submit};
    // Opaque facade command and exact reviewed-candidate identity. User
    // continuations must present both values; they cannot retarget a later
    // review or a replacement opening.
    uint64_t attempt_id{0}, review_id{0};
    CTransactionRef reviewed;
    std::shared_ptr<const PartiallySignedTransaction> psbt;
    std::shared_ptr<const PartiallySignedTransaction> assessment;
    // Completion belongs to the captured assessment, never a fresh metadata
    // read or the export document's potentially incomplete script fields.
    bool assessed_complete{false};
    std::shared_ptr<const psbtfee::Analysis> proof;
    std::optional<CAmount> approved_fee;
    std::optional<SecureString> passphrase;
    secretinput::Captured captured_passphrase;
    std::shared_ptr<CommitConsent> consent;
    psbtpolicy::Read policy;
    bool sweeps_wallet{false}, full_wallet_consent{false};
    QMap<QString, QString> notes;
};
struct Result {
    Error error{Error::None};
    // Immutable outcome identity is independent of a borrowed review value.
    // A failed operation with no new Native artifact retains these facts,
    // without keeping the original presentation's cleanup lease alive.
    Txid candidate_txid{};
    Wtxid candidate_wtxid{};
    CTransactionRef committed{};
    CTransactionRef candidate{};
    CTransactionRef signed_transaction{};
    // SignOnly returns the captured document even when the wallet can add only
    // a subset of signatures. It never returns a committed transaction.
    std::shared_ptr<const PartiallySignedTransaction> document;
    std::shared_ptr<const PartiallySignedTransaction> quarantined_document;
    psbtsignatures::Status signature_status{psbtsignatures::Status::Unknown};
    size_t signed_inputs{0};
    bool partial{false};
    Record record{Record::Unknown};
    Submission submission{Submission::Unknown};
    Relay relay{Relay::Unknown};
    std::optional<common::PSBTError> signer_error{};
    bool relock_failed{false}, notes_requested{false}, publication_failed{false};
    QString policy_error;
    std::shared_ptr<walletrequests::Operation> notes{};
    execution::Rejection notes_rejection{execution::Rejection::None};
};
/** One accepted reviewed transaction, with its original opening and result.
 * Neither the effect nor consuming its outcome depends on GUI delivery. */
class SendTask : public std::enable_shared_from_this<SendTask>
{
public:
    using Notify = std::function<void(Notice)>;
    SendTask(std::shared_ptr<walletopening::Session> opening,
              std::shared_ptr<walletrequests::Store> notes, Notify notify)
        : m_opening(std::move(opening)), m_notes(std::move(notes)), m_notify(std::move(notify)) {}
    execution::SubmitResult start(Request request, std::shared_ptr<const walletopening::Session::Action> action = {});
    execution::SubmitResult confirmSweep(uint64_t attempt_id, uint64_t review_id);
    execution::SubmitResult providePassphrase(uint64_t attempt_id, uint64_t review_id, secretinput::Captured passphrase);
    execution::SubmitResult cancelPrompt(uint64_t attempt_id, uint64_t review_id);
    Stage stage() const;
    Error promptError() const;
    std::optional<Result> result() const;
private:
    execution::SubmitResult plan(std::function<void()> work);
    void beginOnDomain();
    void dispatchNative(secretinput::Captured passphrase = {});
    void nativeCompleted(Result result);
    void complete(Result result);
    void publicationFailed();
    const std::shared_ptr<walletopening::Session> m_opening;
    const std::shared_ptr<walletrequests::Store> m_notes;
    const Notify m_notify;
    mutable std::mutex m_mutex;
    bool m_started{false}, m_publication_failed{false}, m_continuation_pending{false};
    Stage m_stage{Stage::Idle};
    Error m_prompt_error{Error::None};
    uint64_t m_attempt_id{0}, m_review_id{0};
    Request m_request;
    std::shared_ptr<const walletopening::Session::Action> m_action;
    std::shared_ptr<CTransactionRef> m_native_result;
    std::shared_ptr<PartiallySignedTransaction> m_native_document_result;
    std::optional<Result> m_result;
};
} // namespace walletsend

namespace walletsigning {
enum class Error { None, InvalidFee, ChangedTransaction, SignerNotFound, SignerFailed, Fill, Failed, Uncertain, UnverifiedInputs, InvalidSignature, UnverifiedSignatures, FeeUnavailable, UncommittedOutputs };
struct Request {
    CTransactionRef reviewed;
    std::shared_ptr<const PartiallySignedTransaction> document;
    std::shared_ptr<const psbtfee::Analysis> proof;
    std::optional<CAmount> approved_fee;
};
struct Result {
    std::shared_ptr<const void> lifetime{};
    Error error{Error::None};
    std::optional<common::PSBTError> fill_error{};
    std::shared_ptr<const PartiallySignedTransaction> document{};
    // Recoverable bytes are separate from the approved review. Unknown
    // contributions may be exported explicitly, never installed or submitted.
    std::shared_ptr<const PartiallySignedTransaction> quarantined_document{};
    psbtsignatures::Status signature_status{psbtsignatures::Status::Unknown};
    CTransactionRef signed_transaction{};
    bool partial{false};
};
Result Approve(interfaces::Wallet& wallet, const Request& request, std::shared_ptr<walletopening::Session> opening = {});
// Production reserves this output before admitting the device action, so
// accepted work does not need new allocation admission after shutdown closes.
Result ApproveInto(interfaces::Wallet& wallet, const Request& request, std::shared_ptr<Result> output);

enum class ApprovalPhase { Idle, Preparing, WaitingDevice, Verifying, Complete, Partial, Failed, Cancelling, Closed, Uncertain, Unverified };
struct ApprovalSnapshot {
    uint64_t sequence{0}, attempt_id{0}, attempt_review{0}, attempt_view{0};
    uint64_t current_review{0}, current_view{0};
    ApprovalPhase phase{ApprovalPhase::Idle};
    Error error{Error::None};
    bool signer_not_found{false}, in_flight{false}, publishable{false};
    std::shared_ptr<const Result> result;
};

/** Domain-lane owner for one opening's external-signer approval lifecycle.
 * Inputs are immutable captured review values; only snapshots are delivered
 * to the GUI, and a stale/closed result cannot install into a later review.
 */
class ApprovalService final : public std::enable_shared_from_this<ApprovalService>
{
public:
    using Notify = std::function<void(ApprovalSnapshot)>; // enqueue-only GUI transport
    ApprovalService(std::shared_ptr<walletopening::Session> opening, Notify notify);
    execution::SubmitResult reviewChanged(uint64_t review_id, uint64_t view_revision);
    execution::SubmitResult request(uint64_t attempt_id, uint64_t review_id, uint64_t view_revision,
                                 std::shared_ptr<Request> retained_request);
    execution::SubmitResult close();

private:
    struct State;
    static ApprovalSnapshot project(const State& state);
    static void publish(const std::shared_ptr<State>& state, const Notify& notify);
    const std::shared_ptr<walletopening::Session> m_opening;
    const Notify m_notify;
    const std::shared_ptr<State> m_state;
    std::atomic_bool m_close_requested{false};
};
} // namespace walletsigning

#endif // BITCOIN_QML_WALLETSEND_H
