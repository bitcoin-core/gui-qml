// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bumptransactionmodel.h>
#include <qml/bitcoinunits.h>
#include <qml/guipresentation.h>
#include <qml/secretinput.h>
#include <QPointer>

namespace {
QString ErrorMessage(walletbump::Error error)
{
    switch (error) {
    case walletbump::Error::None: return {};
    case walletbump::Error::Invalid: return BumpTransactionModel::tr("Invalid fee-bump request.");
    case walletbump::Error::InvalidId: return BumpTransactionModel::tr("Invalid transaction ID.");
    case walletbump::Error::Ineligible: return BumpTransactionModel::tr("Transaction can no longer be bumped.");
    case walletbump::Error::Unavailable: return BumpTransactionModel::tr("The transaction state could not be checked. Prepare it again before confirming.");
    case walletbump::Error::Create: return BumpTransactionModel::tr("Failed to create bump transaction.");
    case walletbump::Error::NeedsUnlock: return BumpTransactionModel::tr("Enter your wallet password to update this transaction.");
    case walletbump::Error::IncorrectPassphrase: return BumpTransactionModel::tr("The wallet password you entered was incorrect.");
    case walletbump::Error::Sign: return BumpTransactionModel::tr("Failed to sign transaction.");
    case walletbump::Error::Changed: return BumpTransactionModel::tr("Signer changed the reviewed replacement transaction.");
    case walletbump::Error::InvalidSignature: return BumpTransactionModel::tr("The replacement transaction has invalid or unverified signatures.");
    case walletbump::Error::Policy: return BumpTransactionModel::tr("The reviewed replacement no longer meets current node policy. Prepare it again before confirming.");
    case walletbump::Error::RelockFailed: return BumpTransactionModel::tr("The wallet lock could not be restored. Check its security state before retrying.");
    case walletbump::Error::Commit: return BumpTransactionModel::tr("The replacement may not have been sent. Check the transaction before trying again.");
    case walletbump::Error::Uncertain: return BumpTransactionModel::tr("The fee may not have been increased. Check Activity before trying again.");
    }
    return {};
}
} // namespace

BumpTransactionModel::BumpTransactionModel(QObject* parent) : FeeBumpOperation(parent) {}
void BumpTransactionModel::setOpening(std::shared_ptr<walletopening::Session> opening, execution::SnapshotServices services,
                                      walletbump::Services bump_services)
{
    if (m_closed || busy() || (m_opening && m_opening != opening)) return;
    m_opening = std::move(opening); m_services = std::move(services);
    m_bump_services = std::move(bump_services);
    if (!m_services.scheduler && m_opening) m_services.scheduler = m_opening->scheduler();
    if (!m_services.post) m_services.post = presentation::QueuedPost();
}
transactionattempt::CurrentFacts BumpTransactionModel::attemptFacts() const
{
    return {m_opening ? m_opening->identity() : transactionattempt::SessionId{},
            m_attempt_snapshot.candidate_review,
            m_opening && m_opening->available()};
}
BumpTransactionModel::State BumpTransactionModel::ProjectState(transactionattempt::Phase phase)
{
    switch (phase) {
    case transactionattempt::Phase::Idle: return Idle;
    case transactionattempt::Phase::Preparing: return Preparing;
    case transactionattempt::Phase::NeedsUnlock: return NeedsConfirmation;
    case transactionattempt::Phase::Reviewing: return NeedsConfirmation;
    case transactionattempt::Phase::NeedsRevalidation: return NeedsRevalidation;
    case transactionattempt::Phase::Submitting: return Committing;
    case transactionattempt::Phase::Settled: return Succeeded;
    case transactionattempt::Phase::Failed: return Failed;
    case transactionattempt::Phase::Closed: return Closed;
    case transactionattempt::Phase::Uncertain: return Uncertain;
    }
    return Uncertain;
}
void BumpTransactionModel::close()
{
    m_closed = true; m_state = Closed;
    // Accepted Native commands own their original effects and receipts. The
    // quiet observer must release idle prepared-review cleanup leases now.
    if (m_attempt_owner) m_attempt_owner->close({});
    m_attempt_owner.reset(); m_prepare_operation.reset(); m_commit_operation.reset(); m_review.reset();
    m_attempt_snapshot = {};
    m_observation_operation.reset(); m_observed_outcome.reset();
    m_preparing = m_committing = m_rechecking = false;
}
void BumpTransactionModel::retryPublications()
{
    if (m_closed) return;
    const QPointer<BumpTransactionModel> guard{this};
    if (m_preparing) publishPreparation(m_attempt_id, m_preparation_revision);
    if (!guard || m_closed) return;
    if (m_committing) publishCommit(m_attempt_id, m_commit_id);
    if (!guard || m_closed) return;
    if (m_rechecking) publishObservation(m_attempt_id, m_commit_id, m_observation_id);
}
void BumpTransactionModel::setSecurityStateChangedFn(std::function<void()> fn) { m_security_state_changed = std::move(fn); }
bool BumpTransactionModel::publish()
{
    const QPointer<BumpTransactionModel> observer{this};
    Q_EMIT needsUnlockChanged(); if (!observer) return false;
    Q_EMIT resultChanged(); if (!observer) return false;
    Q_EMIT stateChanged(); return bool(observer);
}
void BumpTransactionModel::clearPresentation()
{
    m_old_fee.clear(); m_new_fee.clear(); m_increase.clear(); m_old_txid.clear(); m_new_txid.clear(); m_error.clear(); m_unlock_error.clear(); m_needs_unlock = false;
}
void BumpTransactionModel::setDisplayUnit(int unit)
{
    if (m_closed || unit < 0 || unit > 3 || m_display_unit == unit) return;
    m_display_unit = unit;
    refreshFeePresentation();
    Q_EMIT resultChanged();
}
void BumpTransactionModel::refreshFeePresentation()
{
    if (!m_review) return;
    const auto unit = QmlBitcoinUnits::fromDisplayUnit(m_display_unit);
    const auto format = [unit](CAmount amount) -> QString {
        return QmlBitcoinUnits::formatForDisplay(unit, amount) + QStringLiteral(" ") + QmlBitcoinUnits::label(unit, amount);
    };
    // Three bounded scalar presentations. No wallet, Core, disk or draft work.
    m_old_fee = format(m_review->old_fee);
    m_new_fee = format(m_review->new_fee);
    m_increase = format(m_review->new_fee - m_review->old_fee);
}
quint64 BumpTransactionModel::requestReviewFeeBump(const QString& txid, unsigned int target_blocks)
{
    if (m_closed || !m_opening || !m_opening->available()) return 0;
    if (m_state == Uncertain) return txid == m_old_txid ? m_attempt_id : 0;
    return requestPrepareFeeBump(txid, target_blocks);
}
quint64 BumpTransactionModel::requestPrepareFeeBump(const QString& txid, unsigned int target_blocks)
{
    if (m_closed || busy()) return 0;
    if (m_state == Uncertain || m_state == Committing) return 0;
    if (!m_opening || !m_opening->available() || !m_services.scheduler) {
        m_error = tr("No wallet available."); m_state = Failed; publish(); return 0;
    }
    const auto action = m_opening->startAction();
    if (!action) return 0;
    const auto attempt = m_attempt_id + 1, revision = m_view_revision + 1;
    if (m_attempt_owner) m_attempt_owner->close({});
    auto owner = std::make_shared<transactionattempt::Owner>(m_services.scheduler);
    const transactionattempt::CurrentFacts facts{m_opening->identity(), 0, m_opening->available()};
    const transactionattempt::Ticket ticket{attempt, revision, facts.session, facts.review};
    m_attempt_owner = owner;
    m_attempt_ticket = ticket;
    m_attempt_snapshot = {};
    m_attempt_snapshot.kind = transactionattempt::Kind::Replacement;
    m_attempt_snapshot.ticket = ticket;
    const QPointer<BumpTransactionModel> observer{this};
    auto operation = std::make_shared<execution::OwnedCommand<walletbump::Preparation>>(m_services.scheduler,
        [post = m_services.post, observer, attempt, revision] { post([observer, attempt, revision] { if (observer) observer->publishPreparation(attempt, revision); }); });
    m_attempt_id = attempt; m_view_revision = revision; m_preparation_revision = revision;
    m_prepare_operation = std::move(operation); m_commit_operation.reset(); m_review.reset();
    m_observation_operation.reset(); m_observed_outcome.reset();
    clearPresentation(); m_preparing = true; m_state = Preparing;
    if (!publish()) return 0;
    const transactionattempt::ReplacementStart input{attempt, revision, facts, txid};
    const auto begin = owner->beginReplacement(input,
        [owner, opening = m_opening, action, operation = m_prepare_operation, ticket, facts, txid, target_blocks,
         observer, post = m_services.post, attempt, revision](transactionattempt::Result result) mutable {
            if (result.transition != transactionattempt::Transition::Accepted
                || result.admission_error != transactionattempt::StartError::None) {
                post([observer, snapshot = std::move(result.snapshot), attempt, revision] () mutable {
                    if (observer) observer->applyAttempt(std::move(snapshot), attempt, revision, true);
                });
                return;
            }
            const auto submitted = operation->startWith([opening, action, txid, target_blocks](auto done, auto failed) {
                return opening->perform(action,
                    [opening, txid, target_blocks](interfaces::Wallet& wallet) {
                        return walletbump::Prepare(wallet, txid, target_blocks, opening);
                    }, std::move(done), std::move(failed));
            });
            if (submitted) return;
            const auto failed = owner->fail(ticket, facts, transactionattempt::Phase::Failed,
                [observer, post, attempt, revision](transactionattempt::Result failure) mutable {
                    post([observer, snapshot = std::move(failure.snapshot), attempt, revision] () mutable {
                        if (observer) observer->applyAttempt(std::move(snapshot), attempt, revision, true);
                    });
                });
            if (!failed) post([observer, attempt, revision] {
                if (observer && observer->m_attempt_id == attempt && observer->m_view_revision == revision) {
                    observer->m_preparing = false;
                    observer->m_error = BumpTransactionModel::tr("The fee increase could not be prepared. Try again.");
                    observer->m_state = Failed;
                    observer->publish();
                }
            });
        });
    if (!begin) {
        m_preparing = false; m_error = tr("The fee increase could not be prepared. Try again."); m_state = Failed; publish(); return 0;
    }
    return attempt;
}
void BumpTransactionModel::publishPreparation(quint64 attempt, quint64 revision)
{
    if (!m_preparing || attempt != m_attempt_id || !m_prepare_operation) return;
    const auto result = m_prepare_operation->result();
    if (!result) return;
    if (m_closed) return;
    if (revision != m_view_revision) { m_preparing = false; m_state = Idle; publish(); return; }
    const auto value = result->value.value_or(walletbump::Preparation{});
    if (result->error || value.error != walletbump::Error::None || !value.review) {
        const auto error = result->error ? walletbump::Error::Uncertain : value.error;
        const auto outcome = error == walletbump::Error::Uncertain ? transactionattempt::Phase::Uncertain : transactionattempt::Phase::Failed;
        const auto message = value.message.isEmpty() ? ErrorMessage(error) : value.message;
        const auto owner = m_attempt_owner;
        const auto ticket = m_attempt_ticket;
        const auto facts = transactionattempt::CurrentFacts{ticket.session, ticket.prior_review, m_opening && m_opening->available()};
        if (!owner || !owner->fail(ticket, facts, outcome,
                [observer = QPointer<BumpTransactionModel>{this}, post = m_services.post, attempt, revision, message](transactionattempt::Result failure) mutable {
                    post([observer, snapshot = std::move(failure.snapshot), attempt, revision, message] () mutable {
                        if (!observer) return;
                        observer->m_error = message;
                        observer->applyAttempt(std::move(snapshot), attempt, revision, true);
                    });
                })) {
            m_preparing = false;
            m_error = message; m_state = error == walletbump::Error::Uncertain ? Uncertain : Failed; publish();
        }
        return;
    }
    auto candidate = std::make_shared<walletreview::Snapshot>();
    candidate->replacement = value.review;
    candidate->fee = value.review->new_fee;
    const auto owner = m_attempt_owner;
    const auto ticket = m_attempt_ticket;
    const auto facts = transactionattempt::CurrentFacts{ticket.session, ticket.prior_review, m_opening && m_opening->available()};
    if (!owner || !owner->install(ticket, facts, std::move(candidate), attempt,
            [observer = QPointer<BumpTransactionModel>{this}, post = m_services.post, attempt, revision](transactionattempt::Result installed) mutable {
                post([observer, snapshot = std::move(installed.snapshot), attempt, revision] () mutable {
                    if (observer) observer->applyAttempt(std::move(snapshot), attempt, revision, true);
                });
            })) {
        m_preparing = false;
        m_error = tr("The replacement attempt changed before its review was ready.");
        m_state = NeedsRevalidation; publish();
    }
}

void BumpTransactionModel::applyAttempt(transactionattempt::Snapshot snapshot, quint64 attempt, quint64 revision, bool preparation)
{
    if (m_closed || attempt != m_attempt_id) return;
    if (revision != m_view_revision) {
        if (preparation) { m_preparing = false; m_state = Idle; publish(); }
        return;
    }
    m_attempt_snapshot = std::move(snapshot);
    m_attempt_ticket = m_attempt_snapshot.ticket;
    m_state = ProjectState(m_attempt_snapshot.phase);
    if (!preparation) m_committing = false;
    if (preparation) {
        m_preparing = false;
        m_review = m_attempt_snapshot.candidate ? m_attempt_snapshot.candidate->replacement : nullptr;
        if (m_review) {
            refreshFeePresentation();
            m_old_txid = m_review->old_txid;
        } else if (m_state == Failed && m_error.isEmpty()) {
            m_error = tr("The replacement transaction could not be prepared.");
        }
    }
    publish();
}
quint64 BumpTransactionModel::requestConfirmFeeBump() { return confirm({}); }
quint64 BumpTransactionModel::requestConfirmFeeBumpWithPassphrase(const QString& passphrase) {
    if (m_closed || busy() || m_state != NeedsConfirmation) return 0;
    const auto captured = secretinput::Capture(m_opening ? m_opening->scheduler() : nullptr, passphrase);
    if (!captured) { m_error = tr("The password could not be used. Try again."); publish(); return 0; }
    return confirm({}, captured.input);
}
quint64 BumpTransactionModel::confirm(std::optional<SecureString> passphrase, secretinput::Captured captured)
{
    if (m_closed || busy() || m_state != NeedsConfirmation || !m_review || !m_opening || !m_opening->available()
        || !m_attempt_owner || m_attempt_ticket.id == 0) return 0;
    const auto action = m_opening->startAction();
    if (!action) return 0;
    const auto attempt = m_attempt_id, commit = m_commit_id + 1;
    const auto revision = m_view_revision;
    const auto expected = m_attempt_snapshot.phase;
    if (expected != transactionattempt::Phase::Reviewing && expected != transactionattempt::Phase::NeedsUnlock) return 0;
    const QPointer<BumpTransactionModel> observer{this};
    auto secret = std::make_shared<std::optional<SecureString>>(std::move(passphrase));
    auto operation = std::make_shared<execution::OwnedCommand<walletbump::Commit>>(m_services.scheduler,
        [post = m_services.post, observer, attempt, commit] { post([observer, attempt, commit] { if (observer) observer->publishCommit(attempt, commit); }); });
    const auto owner = m_attempt_owner;
    const auto ticket = m_attempt_ticket;
    const auto facts = attemptFacts();
    const auto review = m_review;
    const auto bump_services = m_bump_services;
    const auto submitted = owner->advance(ticket, facts, expected, transactionattempt::Phase::Submitting,
        [owner, opening = m_opening, action, operation, review, secret, captured, bump_services, ticket,
         facts, observer, post = m_services.post, attempt, revision](transactionattempt::Result transition) mutable {
            if (transition.transition != transactionattempt::Transition::Accepted) {
                post([observer, snapshot = std::move(transition.snapshot), attempt, revision] () mutable {
                    if (observer) observer->applyAttempt(std::move(snapshot), attempt, revision, false);
                });
                return;
            }
            const auto started = operation->startWith([opening, action, review, secret, captured, bump_services](auto done, auto failed) {
                return opening->perform(action,
                    [review, secret, captured, bump_services](interfaces::Wallet& wallet) {
                        return walletbump::Execute(wallet, *review, std::move(*secret), captured, bump_services);
                    }, std::move(done), std::move(failed));
            });
            if (started) return;
            const auto failed = owner->advance(ticket, {facts.session, facts.review, opening->available()},
                transactionattempt::Phase::Submitting, transactionattempt::Phase::Failed,
                [observer, post, attempt, revision](transactionattempt::Result failure) mutable {
                    post([observer, snapshot = std::move(failure.snapshot), attempt, revision] () mutable {
                        if (!observer) return;
                        observer->m_error = BumpTransactionModel::tr("The fee increase could not be sent. Try again.");
                        observer->applyAttempt(std::move(snapshot), attempt, revision, false);
                    });
                });
            if (!failed) post([observer, attempt, revision] {
                if (observer && observer->m_attempt_id == attempt && observer->m_view_revision == revision) {
                    observer->m_committing = false;
                    observer->m_error = BumpTransactionModel::tr("The fee increase could not be sent. Try again.");
                    observer->m_state = Failed;
                    observer->publish();
                }
            });
        });
    if (!submitted) { m_error = tr("The fee increase could not be sent. Try again."); publish(); return 0; }
    m_commit_id = commit;
    m_commit_operation = std::move(operation); m_committing = true;
    m_needs_unlock = false; m_error.clear(); m_unlock_error.clear(); m_state = Committing;
    publish(); return commit;
}
void BumpTransactionModel::publishCommit(quint64 attempt, quint64 commit)
{
    if (!m_committing || attempt != m_attempt_id || commit != m_commit_id || !m_commit_operation) return;
    const auto result = m_commit_operation->result();
    if (!result) return;
    if (m_closed) return;
    auto value = result->value.value_or(walletbump::Commit{});
    if (result->error) value.error = walletbump::Error::Uncertain;
    const auto outcome = value.committed ? transactionattempt::Phase::Settled
        : value.error == walletbump::Error::NeedsUnlock || value.error == walletbump::Error::IncorrectPassphrase ? transactionattempt::Phase::NeedsUnlock
        : value.error == walletbump::Error::Ineligible || value.error == walletbump::Error::Unavailable || value.error == walletbump::Error::Policy ? transactionattempt::Phase::NeedsRevalidation
        : value.error == walletbump::Error::Uncertain || value.error == walletbump::Error::Commit || value.error == walletbump::Error::RelockFailed || result->error ? transactionattempt::Phase::Uncertain
        : transactionattempt::Phase::Failed;
    const auto owner = m_attempt_owner;
    if (!owner) { m_state = Uncertain; m_committing = false; publish(); return; }
    const auto submitted = owner->advance(m_attempt_ticket, attemptFacts(), transactionattempt::Phase::Submitting, outcome,
        [observer = QPointer<BumpTransactionModel>{this}, post = m_services.post, value, attempt,
         revision = m_view_revision](transactionattempt::Result transition) mutable {
            post([observer, snapshot = std::move(transition.snapshot), value = std::move(value), attempt, revision] () mutable {
                if (!observer || observer->m_closed || observer->m_attempt_id != attempt || observer->m_view_revision != revision) return;
                observer->m_attempt_snapshot = std::move(snapshot);
                observer->m_state = ProjectState(observer->m_attempt_snapshot.phase);
                observer->m_committing = false;
                observer->m_needs_unlock = value.error == walletbump::Error::NeedsUnlock || value.error == walletbump::Error::IncorrectPassphrase;
                observer->m_unlock_error = value.error == walletbump::Error::IncorrectPassphrase ? ErrorMessage(value.error) : QString{};
                if (value.committed) {
                    observer->m_new_txid = QString::fromStdString(value.txid.GetHex());
                    observer->m_error.clear();
                } else {
                    observer->m_error = value.message.isEmpty() ? ErrorMessage(value.error) : value.message;
                    if (observer->m_state == NeedsRevalidation || observer->m_state == Failed) observer->m_review.reset();
                }
                const bool published = observer->publish();
                if (published && observer->m_opening && observer->m_opening->available() && observer->m_security_state_changed)
                    observer->m_security_state_changed();
            });
        });
    if (!submitted) {
        m_committing = false;
        m_state = outcome == transactionattempt::Phase::Settled ? Succeeded
            : outcome == transactionattempt::Phase::NeedsRevalidation ? NeedsRevalidation
            : outcome == transactionattempt::Phase::NeedsUnlock ? NeedsConfirmation
            : outcome == transactionattempt::Phase::Uncertain ? Uncertain : Failed;
        m_error = value.message.isEmpty() ? ErrorMessage(value.error) : value.message;
        publish();
    }
}
std::optional<walletbump::Commit> BumpTransactionModel::commitResult() const
{
    if (m_observed_outcome) return m_observed_outcome;
    if (!m_commit_operation) return {};
    const auto result = m_commit_operation->result();
    return result ? result->value : std::nullopt;
}
bool BumpTransactionModel::canRecheckOutcome() const
{
    const auto result = commitResult();
    return !m_closed && m_state == Uncertain && !busy() && m_opening && m_opening->available()
        && result && result->commit_attempted && result->candidate_txid != Txid{};
}
quint64 BumpTransactionModel::requestRecheckFeeBumpOutcome()
{
    if (!canRecheckOutcome()) return 0;
    const auto original = *commitResult();
    const auto opening = m_opening;
    const auto action = opening->startAction();
    if (!action) return 0;
    const auto attempt = m_attempt_id, commit = m_commit_id, observation = ++m_observation_id;
    const QPointer<BumpTransactionModel> observer{this};
    auto operation = std::make_shared<execution::OwnedCommand<walletbump::Commit>>(m_services.scheduler,
        [post = m_services.post, observer, attempt, commit, observation] {
            post([observer, attempt, commit, observation] { if (observer) observer->publishObservation(attempt, commit, observation); });
        });
    m_observation_operation = operation;
    m_rechecking = true;
    const auto submitted = operation->startWith([opening, action, original](auto done, auto failed) {
        return opening->perform(action, [original](interfaces::Wallet& wallet) { return walletbump::ObserveOutcome(wallet, original); },
            std::move(done), std::move(failed));
    });
    if (!observer || m_closed) return submitted ? observation : 0;
    if (!submitted) {
        m_rechecking = false;
        m_error = tr("The transaction status could not be checked, so it is not known yet.");
        publish(); return 0;
    }
    publish(); return observation;
}
void BumpTransactionModel::publishObservation(quint64 attempt, quint64 commit, quint64 observation)
{
    if (m_closed || !m_rechecking || m_attempt_id != attempt || m_commit_id != commit || m_observation_id != observation || !m_observation_operation) return;
    const auto result = m_observation_operation->result();
    if (!result) return;
    m_rechecking = false;
    if (result->error || !result->value) {
        m_error = tr("The transaction status could not be checked, so it is not known yet.");
    } else {
        m_observed_outcome = *result->value;
        if (result->value->record == walletbump::Record::Recorded) {
            m_new_txid = QString::fromStdString(result->value->txid.GetHex());
            m_error = tr("The replacement is recorded in the wallet. Check its status before taking further action.");
        } else {
            m_error = tr("The replacement could not be confirmed in the wallet. No transaction was resubmitted.");
        }
    }
    publish();
}
BumpTransactionModel::WalletRecordState BumpTransactionModel::walletRecordState() const
{
    const auto result = commitResult();
    if (!result) return WalletRecordUnknown;
    switch (result->record) {
    case walletbump::Record::Unknown: return WalletRecordUnknown;
    case walletbump::Record::AbsentAtObservation: return WalletRecordAbsentAtObservation;
    case walletbump::Record::Recorded: return WalletRecordRecorded;
    }
    return WalletRecordUnknown;
}
BumpTransactionModel::WalletSubmissionState BumpTransactionModel::walletSubmissionState() const
{
    const auto result = commitResult();
    if (!result) return WalletSubmissionUnknown;
    switch (result->submission) {
    case walletbump::Submission::Unknown: return WalletSubmissionUnknown;
    case walletbump::Submission::Mempool: return WalletSubmissionMempool;
    case walletbump::Submission::Confirmed: return WalletSubmissionConfirmed;
    case walletbump::Submission::NotInMempool: return WalletSubmissionNotInMempool;
    case walletbump::Submission::Conflicted: return WalletSubmissionConflicted;
    case walletbump::Submission::Abandoned: return WalletSubmissionAbandoned;
    }
    return WalletSubmissionUnknown;
}
BumpTransactionModel::RelayState BumpTransactionModel::relayState() const { return RelayUnknown; }
void BumpTransactionModel::reset()
{
    if (m_closed) return;
    ++m_view_revision;
    // A view close invalidates presentation; an accepted commit keeps its
    // original review and must finish exactly once before another attempt.
    if (m_committing || m_rechecking || m_state == Uncertain) return;
    if (m_attempt_owner) m_attempt_owner->close({});
    m_attempt_owner.reset();
    m_attempt_ticket = {};
    m_prepare_operation.reset();
    m_review.reset();
    m_attempt_snapshot = {};
    clearPresentation();
    m_state = m_preparing ? Discarding : Idle;
    publish();
}
