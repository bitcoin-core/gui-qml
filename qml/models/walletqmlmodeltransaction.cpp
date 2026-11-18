// Copyright (c) 2011-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletqmlmodeltransaction.h>
#include <qml/reviewowner.h>

#include <qml/bitcoinunits.h>
#include <qml/retainedvalue.h>
#include <qml/guipresentation.h>

#include <stdexcept>
#include <utility>
#include <QPointer>
#include <QVariantMap>

namespace {
std::shared_ptr<const walletreview::Snapshot> RetainReview(const walletreview::Snapshot& snapshot,
    const std::shared_ptr<walletopening::Session>& opening)
{
    auto captured = snapshot;
    auto owner = walletopening::RetainValues<walletreview::Snapshot>(opening, [captured = std::move(captured)]() mutable {
        return std::make_unique<walletreview::Snapshot>(std::move(captured));
    });
    if (!owner) throw std::runtime_error{"Transaction review cleanup is unavailable"};
    return owner;
}
} // namespace

WalletQmlModelTransaction::WalletQmlModelTransaction(const walletreview::Snapshot& snapshot,
    std::shared_ptr<walletopening::Session> opening, std::function<void(std::function<void()>)> post, QObject* parent)
    : WalletQmlModelTransaction(prepareConstruction(snapshot, std::move(opening), std::move(post), parent)) {}

WalletQmlModelTransaction::ConstructionInput WalletQmlModelTransaction::prepareConstruction(
    const walletreview::Snapshot& snapshot, std::shared_ptr<walletopening::Session> opening,
    std::function<void(std::function<void()>)> post, QObject* parent)
{
    ConstructionInput input{{}, std::move(opening), std::move(post), QPointer<QObject>{parent}};
    input.snapshot = RetainReview(snapshot, input.opening);
    if (parent && !input.parent) throw std::runtime_error{"The original review owner is no longer available"};
    return input;
}

WalletQmlModelTransaction::WalletQmlModelTransaction(ConstructionInput input)
    : WalletQmlModelTransaction(std::move(input.snapshot), std::move(input.opening), std::move(input.post), input.parent.data()) {}

WalletQmlModelTransaction::WalletQmlModelTransaction(std::shared_ptr<const walletreview::Snapshot> retained,
    std::shared_ptr<walletopening::Session> opening, std::function<void(std::function<void()>)> post, QObject* parent)
    : QObject(parent), m_review_lifetime(std::move(retained)),
      m_recipient_labels(m_review_lifetime->notes), m_reviewed_recipients(m_review_lifetime->recipients),
      m_reviewed_presentation(m_review_lifetime->presentation), m_reviewed_addresses(m_review_lifetime->destinations),
      m_opening(std::move(opening)), m_post(presentation::ReliablePost(std::move(post))), m_review_target_blocks(m_review_lifetime->target_blocks),
      m_review_fee_rate(m_review_lifetime->fee_rate), m_review_fee_source(static_cast<int>(m_review_lifetime->fee_source)), m_amount(m_review_lifetime->amount), m_fee(m_review_lifetime->fee),
      m_fee_known(m_review_lifetime->proof && m_review_lifetime->proof->state == psbtfee::State::Known && m_review_lifetime->proof->amount.has_value()),
      m_amount_amount(new BitcoinAmount(this)), m_fee_amount(new BitcoinAmount(this)),
      m_total_amount(new BitcoinAmount(this)), m_wtx(m_review_lifetime->transaction), m_display_unit(m_review_lifetime->display_unit),
      m_requested_display_unit(m_review_lifetime->display_unit)
{
    const auto& snapshot = *m_review_lifetime;
    m_node_policy_status = snapshot.node_policy_status; m_node_policy_blocked = snapshot.node_policy_blocked;
    if (snapshot.assessment) m_assessed_psbt = std::shared_ptr<const PartiallySignedTransaction>{m_review_lifetime, snapshot.assessment.get()};
    const auto unit = static_cast<BitcoinAmount::Unit>(static_cast<int>(QmlBitcoinUnits::fromDisplayUnit(snapshot.display_unit)));
    // Every transaction copy, including flow/request identity captures, must
    // retain native cleanup rather than merely the underlying raw tx ref.
    if (snapshot.transaction) m_wtx = CTransactionRef{m_review_lifetime, snapshot.transaction.get()};
    m_txid = m_wtx ? QString::fromStdString(m_wtx->GetHash().ToString()) : QString{};
    m_amount_amount->setUnit(unit);
    m_fee_amount->setUnit(unit);
    m_total_amount->setUnit(unit);
    m_amount_amount->setSatoshi(m_amount);
    m_fee_amount->setSatoshi(m_fee);
    m_total_amount->setSatoshi(m_amount + m_fee);
    if (auto* owner = qobject_cast<ReviewOwner*>(parent)) owner->registerReview(this);
}

WalletQmlModelTransaction::WalletQmlModelTransaction(const WalletQmlModelTransaction& reviewed, CAmount authenticated_fee, QObject* parent)
    : QObject(parent),
      m_review_lifetime(reviewed.m_review_lifetime),
      m_presentation_lifetime(reviewed.m_presentation_lifetime),
      m_address(reviewed.m_address),
      m_txid(reviewed.m_txid),
      m_label(reviewed.m_label),
      m_recipient_labels(reviewed.m_recipient_labels),
      m_reviewed_recipients(reviewed.m_reviewed_recipients),
      m_reviewed_presentation(reviewed.m_reviewed_presentation),
      m_reviewed_addresses(reviewed.m_reviewed_addresses),
      m_opening(reviewed.m_opening),
      m_post(reviewed.m_post),
      m_review_target_blocks(reviewed.m_review_target_blocks),
      m_review_fee_rate(reviewed.m_review_fee_rate), m_review_fee_source(reviewed.m_review_fee_source),
      m_amount(reviewed.m_amount),
      m_fee(authenticated_fee),
      m_fee_known(true),
      m_amount_amount(new BitcoinAmount(this)),
      m_fee_amount(new BitcoinAmount(this)),
      m_total_amount(new BitcoinAmount(this)),
      m_wtx(reviewed.m_wtx),
      m_display_unit(reviewed.m_display_unit),
      m_requested_display_unit(reviewed.m_requested_display_unit)
{
    m_assessed_psbt = reviewed.m_assessed_psbt;
    m_source = reviewed.m_source;
    m_psbt = reviewed.m_psbt;
    m_fee_proof = reviewed.m_fee_proof;
    m_sweeps_wallet = reviewed.m_sweeps_wallet;
    m_can_send = reviewed.m_can_send;
    m_can_broadcast = reviewed.m_can_broadcast;
    m_requires_recheck = reviewed.m_requires_recheck;
    m_key_can_send = reviewed.m_key_can_send;
    m_can_commit_complete = reviewed.m_can_commit_complete;
    m_review_message = reviewed.m_review_message;
    m_node_policy_status = reviewed.m_node_policy_status; m_node_policy_blocked = reviewed.m_node_policy_blocked;
    const auto unit = reviewed.m_amount_amount->unit();
    m_amount_amount->setUnit(unit);
    m_amount_amount->setSatoshi(m_amount);
    m_fee_amount->setUnit(unit);
    m_fee_amount->setSatoshi(m_fee);
    m_total_amount->setUnit(unit);
    m_total_amount->setSatoshi(m_amount + m_fee);
    if (auto* owner = qobject_cast<ReviewOwner*>(parent)) owner->registerReview(this);
    if (m_requested_display_unit != m_display_unit) {
        m_presentation_pending = true;
        const QPointer<WalletQmlModelTransaction> observer{this};
        const auto revision = m_presentation_revision;
        const auto unit = m_requested_display_unit;
        presentation::QueuedPost()([observer, revision, unit] {
            if (observer && !observer->m_closed && observer->m_presentation_revision == revision &&
                observer->m_requested_display_unit == unit) observer->refreshRecipientPresentation();
        });
    }
}

void WalletQmlModelTransaction::setNodePolicyStatus(QString status, bool blocked)
{
    if (m_node_policy_status == status && m_node_policy_blocked == blocked) return;
    m_node_policy_status = std::move(status); m_node_policy_blocked = blocked;
    Q_EMIT nodePolicyChanged();
}

BitcoinAmount* WalletQmlModelTransaction::amountAmount() const
{
    return m_amount_amount;
}

QString WalletQmlModelTransaction::formatWithUnit(CAmount value, int display_unit)
{
    const QmlBitcoinUnits::Unit unit = QmlBitcoinUnits::fromDisplayUnit(display_unit);
    return QmlBitcoinUnits::formatForDisplay(unit, value) + QLatin1Char(' ')
        + QmlBitcoinUnits::label(unit, value);
}

QString WalletQmlModelTransaction::amount() const
{
    return formatWithUnit(m_amount, m_display_unit);
}

BitcoinAmount* WalletQmlModelTransaction::feeAmount() const
{
    return m_fee_amount;
}

BitcoinAmount* WalletQmlModelTransaction::totalAmount() const
{
    return m_total_amount;
}

QString WalletQmlModelTransaction::fee() const
{
    return m_fee_known ? formatWithUnit(m_fee, m_display_unit) : tr("Unknown");
}

QString WalletQmlModelTransaction::total() const
{
    return m_fee_known ? formatWithUnit(m_amount + m_fee, m_display_unit) : tr("Unknown");
}

QString WalletQmlModelTransaction::label() const
{
    return m_label;
}

QString WalletQmlModelTransaction::txid() const
{
    return m_txid;
}

void WalletQmlModelTransaction::close()
{
    if (std::exchange(m_closed, true)) return;
    // Quietly drop aliases before releasing their reserved cleanup owners.
    // The shutdown view retains bounded scalar presentation only.
    ++m_presentation_revision;
    m_presentation_pending = false;
    m_wtx.reset();
    m_reviewed_recipients.clear();
    m_reviewed_presentation.clear();
    m_reviewed_addresses.clear();
    m_recipient_labels.clear();
    m_presentation_lifetime.reset();
    m_review_lifetime.reset();
}

QVariantList WalletQmlModelTransaction::reviewedRecipients() const
{
    return m_reviewed_presentation;
}

bool WalletQmlModelTransaction::isReviewedRecipient(const QString& address) const
{
    return m_reviewed_addresses.contains(address);
}

void WalletQmlModelTransaction::setReviewFeeDetails(int target_blocks, const QString& fee_rate)
{
    m_review_target_blocks = target_blocks;
    m_review_fee_rate = fee_rate;
}

void WalletQmlModelTransaction::setDisplayUnit(int unit)
{
    if (m_closed || (m_opening && !m_opening->observationOpen())) return;
    if (unit == m_requested_display_unit && m_presentation_error.isEmpty()) return;
    m_requested_display_unit = unit;
    refreshRecipientPresentation();
}

void WalletQmlModelTransaction::refreshRecipientPresentation()
{
    if (m_closed || (m_opening && !m_opening->observationOpen())) return;
    if (!m_opening || !m_post) {
        m_presentation_pending = false;
        m_presentation_error = tr("Review presentation has no background execution service.");
        Q_EMIT presentationChanged();
        return;
    }
    const auto revision = ++m_presentation_revision;
    const QPointer<WalletQmlModelTransaction> observer{this};
    const int unit = m_requested_display_unit;
    const auto opening = m_opening;
    const auto original = m_review_lifetime;
    const auto post = m_post;
    const auto scheduler = opening->scheduler();
    const auto owns = [observer, opening, revision, unit] {
        return observer && !observer->m_closed && observer->m_opening == opening && opening->observationOpen() &&
            observer->m_presentation_revision == revision && observer->m_requested_display_unit == unit;
    };
    auto rows = walletopening::RetainValues<QVariantList>(opening, [] { return std::make_unique<QVariantList>(); });
    if (!owns()) return;
    if (!rows || !original || !opening->available()) {
        m_presentation_pending = false;
        m_presentation_error = tr("Review presentation is unavailable. Retry after background work completes.");
        Q_EMIT presentationChanged();
        return;
    }
    const auto submitted = execution::Submit(*scheduler, [original, unit, rows] {
        *rows = walletreview::FormatRecipients(original->recipients, unit);
        return rows;
    }, [post, observer, revision, unit](std::shared_ptr<QVariantList> result) {
        post([observer, revision, unit, result = std::move(result)] {
            if (!observer || observer->m_closed || (observer->m_opening && !observer->m_opening->observationOpen()) || observer->m_presentation_revision != revision) return;
            observer->m_display_unit = unit;
            observer->m_reviewed_presentation = *result;
            observer->m_presentation_lifetime = result;
            observer->m_presentation_pending = false;
            observer->m_presentation_error.clear();
            const auto native_unit = static_cast<BitcoinAmount::Unit>(static_cast<int>(QmlBitcoinUnits::fromDisplayUnit(unit)));
            observer->m_amount_amount->setUnit(native_unit);
            if (!observer || observer->m_closed || (observer->m_opening && !observer->m_opening->observationOpen()) || observer->m_presentation_revision != revision) return;
            observer->m_fee_amount->setUnit(native_unit);
            if (!observer || observer->m_closed || (observer->m_opening && !observer->m_opening->observationOpen()) || observer->m_presentation_revision != revision) return;
            observer->m_total_amount->setUnit(native_unit);
            if (!observer || observer->m_closed || (observer->m_opening && !observer->m_opening->observationOpen()) || observer->m_presentation_revision != revision) return;
            Q_EMIT observer->presentationChanged();
            if (!observer || observer->m_closed || (observer->m_opening && !observer->m_opening->observationOpen()) || observer->m_presentation_revision != revision) return;
            Q_EMIT observer->amountChanged();
            if (!observer || observer->m_closed || (observer->m_opening && !observer->m_opening->observationOpen()) || observer->m_presentation_revision != revision) return;
            Q_EMIT observer->feeChanged();
            if (!observer || observer->m_closed || (observer->m_opening && !observer->m_opening->observationOpen()) || observer->m_presentation_revision != revision) return;
            Q_EMIT observer->totalChanged();
            if (!observer || observer->m_closed || (observer->m_opening && !observer->m_opening->observationOpen()) || observer->m_presentation_revision != revision) return;
            Q_EMIT observer->reviewedRecipientsChanged();
        });
    }, [post, observer, revision](std::exception_ptr) {
        post([observer, revision] {
            if (!observer || observer->m_closed || (observer->m_opening && !observer->m_opening->observationOpen()) || observer->m_presentation_revision != revision) return;
            observer->m_presentation_pending = false;
            observer->m_presentation_error = tr("Review presentation could not be prepared.");
            Q_EMIT observer->presentationChanged();
        });
    }, {execution::Lane::Domain, {}, execution::WorkClass::Observation});
    if (!owns()) return;
    m_presentation_pending = bool(submitted);
    m_presentation_error = submitted ? QString{} : tr("The review could not be shown. Try again in a moment.");
    Q_EMIT presentationChanged();
}

CTransactionRef& WalletQmlModelTransaction::getWtx()
{
    return m_wtx;
}

void WalletQmlModelTransaction::setWtx(const CTransactionRef& newTx)
{
    const QString old_txid{txid()};
    m_wtx = newTx;
    m_txid = newTx ? QString::fromStdString(newTx->GetHash().ToString()) : QString{};
    if (txid() != old_txid) {
        Q_EMIT txidChanged();
    }
}

CAmount WalletQmlModelTransaction::getTransactionFee() const
{
    return m_fee;
}

CAmount WalletQmlModelTransaction::getTotalTransactionAmount() const
{
    return m_amount + m_fee;
}

void WalletQmlModelTransaction::setTransactionFee(const CAmount& newFee)
{
    if (!m_fee_known || m_fee != newFee) {
        m_fee_known = true;
        m_fee = newFee;
        m_fee_amount->setSatoshi(m_fee);
        m_total_amount->setSatoshi(m_amount + m_fee);
        Q_EMIT feeChanged();
        Q_EMIT totalChanged();
    }
}
