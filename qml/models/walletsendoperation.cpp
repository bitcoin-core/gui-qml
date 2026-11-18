// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletsendoperation.h>

#include <qml/models/coinslistmodel.h>
#include <qml/models/psbtqmlmodel.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/walletpsbtoperation.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/models/walletqmlmodeltransaction.h>
#include <qml/secretinput.h>
#include <qml/walletsend.h>

#include <QScopeGuard>

#include <limits>
#include <utility>

using State = SendOperation::State;

SendResultText DescribeSendResult(const walletsend::Result& result, bool sign_only)
{
    if (result.committed) return {State::Committed, {}};
    if (sign_only && result.document && !result.quarantined_document &&
        result.error == walletsend::Error::None && !result.relock_failed) {
        return result.partial
            ? SendResultText{State::PartiallySigned, WalletQmlModel::tr("Some PSBT inputs were signed. The transaction was not sent.")}
            : SendResultText{State::SignedOnly, WalletQmlModel::tr("The transaction was signed. It was not sent.")};
    }
    using Error = walletsend::Error;
    State state = result.error == Error::NeedsUnlock || result.error == Error::IncorrectPassphrase ? State::NeedsUnlock
        : result.error == Error::Uncertain ? State::Uncertain
        : result.error == Error::Cancelled ? State::Cancelled : State::Failed;
    QString message;
    if (sign_only && result.error == Error::Uncertain) {
        message = result.relock_failed
            ? WalletQmlModel::tr("The signed PSBT was not sent, but the wallet could not be relocked. Check wallet status before continuing.")
            : WalletQmlModel::tr("Signing may not have finished. The transaction was not sent; check the PSBT before trying again.");
    } else if (sign_only && result.error == Error::Signer) {
        message = WalletQmlModel::tr("Signing did not finish. Signatures that were verified are kept. The transaction was not sent.");
    } else switch (result.error) {
    case Error::InvalidStructure: message = WalletQmlModel::tr("This PSBT has duplicate inputs and cannot be signed or sent."); break;
    case Error::NeedsUnlock: message = sign_only ? WalletQmlModel::tr("Enter your wallet password to sign this PSBT. The transaction will not be sent.") : WalletQmlModel::tr("Enter your wallet password to send this transaction."); break;
    case Error::IncorrectPassphrase: message = WalletQmlModel::tr("The wallet password is incorrect."); break;
    case Error::CannotSign: message = WalletQmlModel::tr("This wallet cannot sign transactions."); break;
    case Error::InvalidFee: message = WalletQmlModel::tr("The transaction fee is missing or changed after review."); break;
    case Error::ChangedTransaction: message = WalletQmlModel::tr("External signer changed the reviewed transaction."); break;
    case Error::Incomplete: message = WalletQmlModel::tr("Only PSBTs this wallet can fully sign are supported right now."); break;
    case Error::NeedsSignerApproval: message = WalletQmlModel::tr("Approve this transaction on the signing device before sending it."); break;
    case Error::InvalidSignature: message = WalletQmlModel::tr("The returned signatures could not be verified for the reviewed transaction. Check the signing device and try again."); break;
    case Error::UnverifiedSignatures: message = WalletQmlModel::tr("The signer returned a PSBT that cannot be verified yet. The reviewed transaction is unchanged. Export the result to check it elsewhere."); break;
    case Error::UncommittedOutputs: message = WalletQmlModel::tr("The signatures do not cover every recipient of the reviewed transaction. Nothing was sent. Sign again with the default signature type."); break;
    case Error::Signer: message = result.signer_error ? PsbtQmlModel::PsbtErrorText(*result.signer_error) : WalletQmlModel::tr("Signing failed."); break;
    case Error::Uncertain: message = WalletQmlModel::tr("The transaction result could not be confirmed. Check wallet activity before retrying."); break;
    case Error::Cancelled: message = WalletQmlModel::tr("The transaction confirmation was cancelled."); break;
    case Error::Rejected: message = WalletQmlModel::tr("The transaction could not be sent. Try again."); break;
    case Error::NodePolicy: message = result.policy_error; break;
    case Error::None: message = WalletQmlModel::tr("The transaction was not committed."); break;
    }
    if (result.relock_failed) {
        state = State::Uncertain;
        message = WalletQmlModel::tr("The wallet could not be relocked. Check wallet status before retrying.");
    }
    if (result.quarantined_document && state != State::Uncertain) state = State::Unverified;
    return {state, message};
}

namespace {
int SubmissionOf(walletsend::Submission submission)
{
    using S = WalletQmlModel::SendSubmission;
    switch (submission) {
    case walletsend::Submission::Unknown: return int(S::SubmissionUnknown);
    case walletsend::Submission::Mempool: return int(S::SubmissionMempool);
    case walletsend::Submission::Confirmed: return int(S::SubmissionConfirmed);
    case walletsend::Submission::NotInMempool: return int(S::SubmissionNotInMempool);
    case walletsend::Submission::Conflicted: return int(S::SubmissionConflicted);
    case walletsend::Submission::Abandoned: return int(S::SubmissionAbandoned);
    }
    return int(S::SubmissionUnknown);
}
int SendRecordOf(walletsend::Record record)
{
    using R = WalletQmlModel::SendRecord;
    switch (record) {
    case walletsend::Record::Unknown: return int(R::SendRecordUnknown);
    case walletsend::Record::AbsentAtObservation: return int(R::SendRecordAbsentAtObservation);
    case walletsend::Record::Recorded: return int(R::SendRecordRecorded);
    }
    return int(R::SendRecordUnknown);
}
} // namespace

WalletSendOperation::WalletSendOperation(WalletQmlModel& wallet, bool sign_only, QObject* parent)
    : SendOperation(sign_only, parent), m_wallet{&wallet}
{
    // Without its wallet nobody can report on the work any more; the work
    // itself finishes on its own.
    connect(&wallet, &QObject::destroyed, this, [this] {
        m_work.reset();
        publish(State::Cancelled);
    });
}

WalletSendOperation::~WalletSendOperation()
{
    // Its owner went away: an open prompt is answered with no.
    if (!finished()) cancel();
}

bool WalletSendOperation::start(std::optional<QString> passphrase)
{
    WalletQmlModel* wallet = m_wallet.data();
    if (!wallet || !wallet->observationsAllowed()) return false;
    const bool sign_only = signOnly();
    const auto intent = sign_only ? walletsend::Intent::SignOnly : walletsend::Intent::Submit;
    if (wallet->psbtActionBusy() || wallet->signerApprovalBusy() || wallet->preparationBusy()) return false;
    // A review whose send was committed, or may have been, is not sent again.
    if (wallet->m_send_review && wallet->m_send_review == wallet->m_current_transaction && wallet->m_send_operation) {
        const auto last = wallet->m_send_operation->result();
        if (last) {
            const auto outcome = DescribeSendResult(*last, wallet->m_send_sign_only).state;
            if (outcome == State::Committed || outcome == State::Uncertain || outcome == State::Unverified) return false;
        }
    }
    if (wallet->sendBusy() || wallet->m_sending_commit->exchange(true)) return false;
    if (wallet->m_send_request_id == std::numeric_limits<quint64>::max()) {
        wallet->m_sending_commit->store(false);
        return false;
    }
    m_attempt_id = ++wallet->m_send_request_id;
    const auto gate = wallet->m_sending_commit;
    auto rejected = qScopeGuard([gate] { gate->store(false); });
    const QPointer<WalletQmlModel> self{wallet};
    const QPointer<WalletQmlModelTransaction> approved{wallet->m_current_transaction};
    const auto approved_revision = wallet->m_preparation_projection.intent_revision;
    m_review = approved;
    wallet->clearTransactionStatus();
    // The status change reached another handler that replaced the review.
    if (!self || !wallet->observationsAllowed() || approved != wallet->m_current_transaction ||
        approved_revision != wallet->m_preparation_projection.intent_revision || m_attempt_id != wallet->m_send_request_id) {
        publish(State::Cancelled);
        return true;
    }
    // Publish before the shared status: its handlers may replace the review
    // or delete the wallet.
    const auto fail = [&](State state, const QString& message) {
        publish(state, message);
        wallet->setTransactionStatus(message);
    };
    if (wallet->signerApprovalState() == WalletQmlModel::ApprovalState::ApprovalUncertain ||
        wallet->signerApprovalState() == WalletQmlModel::ApprovalState::ApprovalUnverified) {
        wallet->m_send_review = wallet->m_current_transaction;
        fail(State::Uncertain, WalletQmlModel::tr("It is not known what the signer did. Check the PSBT before trying again."));
        return true;
    }
    auto* review = wallet->m_current_transaction;
    if (!wallet->available() || !review || !review->getWtx()) {
        if (wallet->available()) fail(State::Failed, WalletQmlModel::tr("Review a transaction before sending it."));
        else publish(State::Cancelled);
        return true;
    }
    if (wallet->currentTransactionNeedsRevalidation()) {
        fail(State::Failed, WalletQmlModel::tr("The reviewed transaction no longer matches the current draft. Prepare it again."));
        return true;
    }
    const bool can_start = sign_only ? wallet->currentTransactionCanSignOnly() : review->canSend();
    if (!can_start) {
        const QString reason = sign_only && (!review->psbt() || !wallet->m_metadata_known || wallet->m_private_keys_disabled)
            ? WalletQmlModel::tr("This wallet cannot sign this PSBT locally.")
            : !review->reviewMessage().isEmpty() ? review->reviewMessage() :
            (wallet->m_metadata_known && wallet->m_private_keys_disabled && !wallet->m_external_signer
                ? WalletQmlModel::tr("This wallet cannot sign transactions.") : WalletQmlModel::tr("This transaction is not ready to send."));
        fail(State::Failed, reason);
        return true;
    }
    const auto action = wallet->m_opening->startAction();
    if (!action) {
        fail(State::Failed, WalletQmlModel::tr("The transaction could not be sent. Try again."));
        return true;
    }
    auto notes = intent == walletsend::Intent::Submit ? wallet->captureSentRecipientLabels(action) : WalletQmlModel::SentLabelCapture{};
    if (notes.ticket && !notes.valid) {
        fail(State::Failed, WalletQmlModel::tr("Outgoing notes could not be saved. Try again."));
        return true;
    }
    if (wallet->m_current_review_identity == std::numeric_limits<quint64>::max()) {
        fail(State::Failed, WalletQmlModel::tr("This review can no longer be used. Prepare the transaction again before sending."));
        return true;
    }
    walletsend::Request request;
    request.intent = intent;
    request.attempt_id = m_attempt_id;
    request.review_id = m_review_id = wallet->m_current_review_identity;
    request.reviewed = review->getWtx(); request.psbt = review->psbt();
    request.assessment = review->assessedPsbt();
    request.policy = wallet->m_psbt_fee_services.policy;
    request.sweeps_wallet = intent == walletsend::Intent::Submit && wallet->currentTransactionSweepsWallet();
    request.assessed_complete = review->canBroadcast();
    request.proof = review->feeProof(); request.approved_fee = review->reviewedFee();
    if (passphrase) {
        auto captured = secretinput::Capture(wallet->m_opening->scheduler(), *passphrase);
        if (!captured) {
            fail(State::Failed, WalletQmlModel::tr("The password could not be used. The transaction was not sent."));
            return true;
        }
        request.captured_passphrase = std::move(captured.input);
    }
    if (intent == walletsend::Intent::Submit) {
        request.consent = std::make_shared<walletsend::CommitConsent>();
        wallet->m_send_consent = m_consent = request.consent;
    } else {
        wallet->m_send_consent.reset();
    }
    if (notes.labels) request.notes = *notes.labels;
    // The work reports back to the wallet, which applies its effects, and to
    // this operation while it exists.
    const QPointer<WalletSendOperation> operation{this};
    const QPointer<WalletQmlModelTransaction> reviewed{review};
    auto work_handle = std::make_shared<std::weak_ptr<walletsend::Operation>>();
    auto notify = [gate, post = wallet->m_fee_read_services.post, self, operation, reviewed, sign_only, work_handle](walletsend::Notice notice) {
        if (notice == walletsend::Notice::Finished) gate->store(false);
        post([self, operation, reviewed, sign_only, work_handle, notice] {
            if (!self || self->m_stopping) return;
            const auto work = work_handle->lock();
            if (!work) return;
            if (notice == walletsend::Notice::Finished) {
                self->finishSendWork(work, reviewed, sign_only, operation);
            } else if (notice == walletsend::Notice::SweepConsentRequired || notice == walletsend::Notice::CredentialsRequired) {
                if (operation && operation->m_work == work) operation->promptChanged();
                else self->declineSendPrompt(work);
            } else if (notice == walletsend::Notice::Notes && self->m_sent_labels_work.lock() == work) {
                self->publishSentRecipientLabels();
            }
        });
    };
    wallet->m_send_review = review; wallet->m_send_review_id = m_review_id;
    // The draft this send was reviewed from; a commit clears it if unchanged.
    wallet->m_send_draft_revision = wallet->m_send_recipients->revision();
    wallet->m_send_sign_only = sign_only;
    m_work = std::make_shared<walletsend::Operation>(wallet->m_opening, wallet->m_request_store, std::move(notify));
    *work_handle = m_work;
    wallet->m_send_operation = m_work;
    const auto submitted = m_work->start(std::move(request), action);
    if (!submitted) {
        m_work.reset();
        fail(State::Failed, WalletQmlModel::tr("The transaction could not be sent. Try again."));
        return true;
    }
    rejected.dismiss();
    publish(State::Pending);
    wallet->setSendRunning(true);
    return true;
}

void WalletSendOperation::promptChanged()
{
    WalletQmlModel* wallet = m_wallet.data();
    if (!wallet || !wallet->observationsAllowed() || !m_work || finished()) return;
    // A prompt answers for the review the send was started for.
    if (!m_review || m_review != wallet->m_current_transaction || m_review_id != wallet->m_current_review_identity) {
        cancel();
        return;
    }
    switch (m_work->stage()) {
    case walletsend::Stage::AwaitingSweepConsent:
        publish(State::NeedsConfirmation);
        wallet->setSendRunning(true);
        wallet->clearTransactionStatus();
        return;
    case walletsend::Stage::AwaitingCredentials: {
        const QString message = m_work->promptError() == walletsend::Error::IncorrectPassphrase
            ? WalletQmlModel::tr("The wallet password is incorrect.") : WalletQmlModel::tr("Enter your wallet password to send this transaction.");
        publish(State::NeedsUnlock, message);
        wallet->setSendRunning(false);
        wallet->setTransactionStatus(message, true);
        return;
    }
    case walletsend::Stage::Submitting:
    case walletsend::Stage::SigningOnly:
        wallet->setSendRunning(true);
        publish(State::Pending);
        return;
    case walletsend::Stage::Idle:
    case walletsend::Stage::Finished:
        return;
    }
}

void WalletSendOperation::confirm()
{
    WalletQmlModel* wallet = m_wallet.data();
    if (!wallet || state() != State::NeedsConfirmation || !m_work || !m_review ||
        m_review != wallet->m_current_transaction || m_review_id != wallet->m_current_review_identity) return;
    if (!m_work->confirmSweep(m_attempt_id, m_review_id)) return;
    publish(State::Pending);
    wallet->setSendRunning(true);
    wallet->clearTransactionStatus();
}

void WalletSendOperation::unlock(const QString& passphrase)
{
    WalletQmlModel* wallet = m_wallet.data();
    if (!wallet || state() != State::NeedsUnlock || !m_review || m_review != wallet->m_current_transaction) return;
    if (!m_work || m_work->stage() != walletsend::Stage::AwaitingCredentials) {
        // The work finished needing the password: send again with it.
        if (!start(passphrase)) publish(State::Failed, WalletQmlModel::tr("The transaction could not be sent. Try again."));
        return;
    }
    const QString unusable = WalletQmlModel::tr("The password could not be used. The transaction was not sent.");
    auto captured = secretinput::Capture(wallet->m_opening ? wallet->m_opening->scheduler() : nullptr, passphrase);
    if (!captured || !m_work->providePassphrase(m_attempt_id, m_review_id, std::move(captured.input))) {
        publish(State::NeedsUnlock, unusable);
        wallet->setTransactionStatus(unusable, true);
        return;
    }
    publish(State::Pending);
    wallet->setSendRunning(true);
    wallet->clearTransactionStatus();
}

void WalletSendOperation::cancel()
{
    if (finished()) return;
    const bool prompt = state() == State::NeedsConfirmation || state() == State::NeedsUnlock;
    if (!prompt) {
        // Running work keeps running and reports its own outcome: Cancelled
        // if the withdrawal wins before the commit, the commit otherwise.
        if (m_consent) m_consent->withdraw();
        return;
    }
    // Finish first: the work's reply to the cancel is not this outcome.
    publish(State::Cancelled);
    // A finished operation lets go of its work, which keeps the wallet open;
    // the wallet keeps work that is still running.
    const auto work = std::exchange(m_work, nullptr);
    WalletQmlModel* wallet = m_wallet.data();
    if (!wallet || !prompt || !work) return;
    const auto stage = work->stage();
    if (stage != walletsend::Stage::AwaitingSweepConsent && stage != walletsend::Stage::AwaitingCredentials) return;
    if (work->cancelPrompt(m_attempt_id, m_review_id)) wallet->setSendRunning(true);
}

void WalletSendOperation::workFinished(const walletsend::Result& result, bool review_current)
{
    m_work.reset();
    if (!review_current) {
        publish(State::Cancelled);
        return;
    }
    const auto [state, message] = DescribeSendResult(result, signOnly());
    if (state == State::Committed) {
        publish(state, {}, QString::fromStdString(result.committed->GetHash().GetHex()),
                SubmissionOf(result.submission), SendRecordOf(result.record));
    } else {
        publish(state, message);
    }
}

WalletTransactionReviewInterface::WalletTransactionReviewInterface(WalletQmlModel& wallet)
    : TransactionReviewInterface(&wallet), m_wallet{&wallet}
{
    connect(&wallet, &WalletQmlModel::currentTransactionChanged, this, &TransactionReviewInterface::currentReviewChanged);
    connect(&wallet, &WalletQmlModel::availableChanged, this, &TransactionReviewInterface::availableChanged);
}

QObject* WalletTransactionReviewInterface::currentReview() const
{
    return m_wallet ? m_wallet->currentTransaction() : nullptr;
}

bool WalletTransactionReviewInterface::available() const
{
    return m_wallet && m_wallet->available();
}

SendOperation* WalletTransactionReviewInterface::startSend(bool sign_only)
{
    if (!m_wallet) return nullptr;
    auto operation = std::make_unique<WalletSendOperation>(*m_wallet, sign_only);
    if (!operation->start()) return nullptr;
    return operation.release();
}

PsbtOperation* WalletTransactionReviewInterface::startPsbt(PsbtOperation::Kind kind, const QString& path, const QString& signer_result)
{
    if (!m_wallet) return nullptr;
    auto operation = std::make_unique<WalletPsbtOperation>(*m_wallet, kind);
    if (!operation->start(path, signer_result)) return nullptr;
    return operation.release();
}

// WalletQmlModel members that only its sends use.

TransactionReviewInterface* WalletQmlModel::transactionReview() const
{
    if (!m_transaction_review) m_transaction_review = new WalletTransactionReviewInterface(*const_cast<WalletQmlModel*>(this));
    return m_transaction_review;
}

void WalletQmlModel::clearSentDraft()
{
    const QPointer<WalletQmlModel> self{this};
    m_send_recipients->clear();
    if (!self) return;
    m_coins_list_model->setManualInputSelection(false);
    m_coins_list_model->update();
    if (!self) return;
    Q_EMIT sentDraftCleared();
}

void WalletQmlModel::finishSendWork(const std::shared_ptr<walletsend::Operation>& work, const QPointer<WalletQmlModelTransaction>& review,
                                    bool sign_only, const QPointer<WalletSendOperation> &operation)
{
    if (!observationsAllowed() || m_closed || !work) return;
    const auto result = work->result();
    if (!result) return;
    const QPointer<WalletQmlModel> self{this};
    if (work == m_send_operation) setSendRunning(false);
    if (!self) return;
    const auto [outcome, message] = DescribeSendResult(*result, sign_only);
    const auto tell = [&](bool review_current) {
        if (operation) operation->workFinished(*result, review_current);
    };
    const bool review_current = available() && review && review == m_current_transaction;
    if (sign_only && result->document && !result->quarantined_document && review_current) {
        // A sign-only result may contain new valid partial signatures even
        // when the wallet reported a terminal signing error. Preserve that
        // captured document, while allowing broadcast only after a clean,
        // verified complete result.
        invalidateImportReview();
        review->setPsbt(result->document);
        review->setAssessedPsbt(result->document);
        if (result->error == walletsend::Error::None && !result->relock_failed) {
            if (result->signed_transaction) { review->setWtx(result->signed_transaction); if (!self) return; }
            review->setCanBroadcast(m_node && !result->partial);
            setTransactionStatus(message);
            Q_EMIT currentTransactionChanged();
            if (!self) return;
            tell(true);
            return;
        }
    }
    if (result->committed && result->notes_requested) {
        m_sent_labels_work = work;
        m_sent_labels_operation = result->notes;
        m_sent_labels_txid = QString::fromStdString(result->committed->GetHash().GetHex());
        m_sent_labels_pending = bool(result->notes);
        m_sent_labels_error = result->notes ? QString{}
            : result->record != walletsend::Record::Recorded
                ? tr("The wallet transaction record could not be confirmed, so its outgoing notes were not saved.")
            : result->notes_rejection == execution::Rejection::Transport
                ? tr("The transaction was committed, but its outgoing note results could not be confirmed.")
                : tr("The transaction was saved to the wallet, but its outgoing notes could not be saved.");
        publishSentRecipientLabels();
        if (!self) return;
    }
    m_opening->refreshMetadata();
    if (!self) return;
    // A consumed effect belongs to its original review. A successor gets no
    // old success, password prompt, mutation or transaction error.
    if (!available() || !review || review != m_current_transaction) {
        tell(false);
        return;
    }
    if (result->committed) {
        review->setCanSend(false); review->setCanBroadcast(false);
        review->setWtx(result->committed);
        if (!self || !review || review != m_current_transaction) return;
        review->setPsbt({}); m_psbt_document_lease.reset(); review->setRequiresRecheck(false);
        review->setSource(CurrentTransactionSource::None);
        review->setReviewMessage({}); clearTransactionStatus();
        if (!self || !review || review != m_current_transaction) return;
        clearSelectedCoins();
        if (!self) return;
        // The sent payment leaves the form, even when its page is hidden or
        // gone. A draft changed since the review (an import) is newer and stays.
        if (!sign_only && m_send_recipients->revision() == m_send_draft_revision) clearSentDraft();
        if (!self) return;
    } else {
        if (result->quarantined_document) {
            review->setCanSend(false);
            review->setCanBroadcast(false);
        }
        if (outcome == SendOperation::State::Uncertain) review->setCanSend(false);
        setTransactionStatus(message, outcome == SendOperation::State::NeedsUnlock);
        if (!self) return;
        if (sign_only && (result->document || result->quarantined_document)) {
            Q_EMIT currentTransactionChanged();
            if (!self) return;
            Q_EMIT signerApprovalChanged();
            if (!self) return;
        }
    }
    if (result->committed || outcome == SendOperation::State::Uncertain) {
        Q_EMIT currentTransactionChanged();
        if (!self) return;
    }
    tell(true);
}
