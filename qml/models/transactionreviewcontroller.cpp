// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/transactionreviewcontroller.h>

TransactionReviewController::TransactionReviewController(QObject* parent) : QObject(parent) {}

TransactionReviewController::~TransactionReviewController()
{
    // The page went away: its prompts are answered with no.
    if (m_send && !m_send->finished()) m_send->cancel();
    if (m_psbt && !m_psbt->finished()) m_psbt->cancel();
}

void TransactionReviewController::setReviewInterface(TransactionReviewInterface* review_interface)
{
    if (m_interface == review_interface) return;
    if (m_interface) disconnect(m_interface, nullptr, this, nullptr);
    cancelPrompt();
    dropPsbt();
    m_interface = review_interface;
    if (m_interface) {
        connect(m_interface, &QObject::destroyed, this, &TransactionReviewController::reviewDetailsChanged);
        connect(m_interface, &TransactionReviewInterface::reviewDetailsChanged, this, &TransactionReviewController::reviewDetailsChanged);
        connect(m_interface, &TransactionReviewInterface::currentReviewChanged, this, &TransactionReviewController::reviewChanged);
        connect(m_interface, &TransactionReviewInterface::unverifiedSignerResultChanged, this, &TransactionReviewController::signerResultChanged);
        connect(m_interface, &TransactionReviewInterface::availableChanged, this, [this] {
            if (m_interface && !m_interface->available()) {
                cancelPrompt();
                dropPsbt();
            }
        });
    }
    Q_EMIT reviewInterfaceChanged();
    Q_EMIT signerResultChanged();
    Q_EMIT reviewDetailsChanged();
}

void TransactionReviewController::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    if (!m_active) {
        // A finished result stays until the page is shown again: its hand-off
        // is what hides the page.
        cancelPrompt();
        if (m_psbt && !m_psbt->finished()) m_psbt->cancel();
        cancelPsbtSave();
    } else {
        dropPsbt();
    }
    Q_EMIT activeChanged();
}

TransactionReviewController::Prompt TransactionReviewController::prompt() const
{
    if (!m_send) return Prompt::None;
    if (m_send->state() == SendOperation::State::NeedsConfirmation) return Prompt::SweepConsent;
    if (m_send->passwordRequested()) return Prompt::Password;
    return Prompt::None;
}

void TransactionReviewController::approveSigner()
{
    if (m_active && m_interface && m_interface->available() && m_interface->currentReview()) {
        m_interface->approveSigner();
    }
}

void TransactionReviewController::send() { start(false); }

void TransactionReviewController::signOnlyTransaction() { start(true); }

void TransactionReviewController::start(bool sign_only)
{
    if (!m_interface || !m_active || !m_interface->available()) return;
    if (m_send && !m_send->finished()) return;
    QObject* review = m_interface->currentReview();
    if (!review) return;
    auto* started = m_interface->startSend(sign_only);
    if (!started) return;
    delete m_send.data();
    started->setParent(this);
    m_send = started;
    m_send_review = review;
    connect(started, &SendOperation::changed, this, &TransactionReviewController::sendChanged);
    Q_EMIT sendChanged();
}

void TransactionReviewController::confirm()
{
    if (m_send) m_send->confirm();
}

void TransactionReviewController::unlock(const QString& passphrase)
{
    if (m_send) m_send->unlock(passphrase);
}

void TransactionReviewController::cancel() { cancelPrompt(); }

void TransactionReviewController::cancelPrompt()
{
    if (m_send && !m_send->finished()) m_send->cancel();
}

void TransactionReviewController::reviewChanged()
{
    // A send and a save answer for the review they were started for.
    if (!m_interface) return;
    if (m_interface->currentReview() != m_send_review) cancelPrompt();
    // A recheck replaces the review it checks.
    if (m_psbt && !m_psbt->finished() && m_psbt->kind() != PsbtOperation::Kind::Recheck &&
        m_interface->currentReview() != m_psbt_review) dropPsbt();
    if (m_save_pending && m_interface->currentReview() != m_save_review) cancelPsbtSave();
}

PsbtOperation::State TransactionReviewController::psbtState() const
{
    if (m_psbt_refused) return PsbtOperation::State::Failed;
    return m_psbt ? m_psbt->state() : PsbtOperation::State::None;
}

QString TransactionReviewController::psbtError() const
{
    if (m_psbt_refused) return tr("Another wallet action is still running.");
    return m_psbt ? m_psbt->error() : QString{};
}

void TransactionReviewController::beginPsbtSave(bool signer_result)
{
    if (!m_interface || !m_active || psbtBusy()) return;
    const QString result = signer_result ? m_interface->unverifiedSignerResult() : QString{};
    if (signer_result && result.isEmpty()) return;
    m_save_pending = true;
    m_save_signer_result = result;
    m_save_review = m_interface->currentReview();
    Q_EMIT psbtChanged();
}

void TransactionReviewController::choosePsbtSaveFile(const QString& path)
{
    if (!m_save_pending) return;
    const QString signer_result = m_save_signer_result;
    m_save_pending = false;
    m_save_signer_result.clear();
    if (path.isEmpty() || !m_interface || m_interface->currentReview() != m_save_review) {
        Q_EMIT psbtChanged();
        return;
    }
    startPsbt(PsbtOperation::Kind::Save, path, signer_result);
}

void TransactionReviewController::cancelPsbtSave()
{
    if (!m_save_pending) return;
    m_save_pending = false;
    m_save_signer_result.clear();
    Q_EMIT psbtChanged();
}

void TransactionReviewController::broadcast() { startPsbt(PsbtOperation::Kind::Broadcast); }

void TransactionReviewController::recheck() { startPsbt(PsbtOperation::Kind::Recheck); }

void TransactionReviewController::startPsbt(PsbtOperation::Kind kind, const QString& path, const QString& signer_result)
{
    if (!m_interface || !m_active || psbtBusy()) return;
    auto* started = m_interface->startPsbt(kind, path, signer_result);
    delete m_psbt.data();
    m_psbt_kind = kind;
    m_psbt_refused = !started;
    if (started) {
        started->setParent(this);
        m_psbt = started;
        m_psbt_review = m_interface->currentReview();
        connect(started, &PsbtOperation::changed, this, &TransactionReviewController::psbtChanged);
    }
    Q_EMIT psbtChanged();
}

void TransactionReviewController::dropPsbt()
{
    const bool had = m_psbt || m_psbt_refused || m_save_pending;
    if (m_psbt && !m_psbt->finished()) m_psbt->cancel();
    delete m_psbt.data();
    m_psbt_refused = false;
    m_save_pending = false;
    m_save_signer_result.clear();
    if (had) Q_EMIT psbtChanged();
}

void TransactionReviewController::dismissPsbtResult()
{
    if (psbtBusy() || (!m_psbt && !m_psbt_refused)) return;
    delete m_psbt.data();
    m_psbt_refused = false;
    Q_EMIT psbtChanged();
}
