// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/senddraftcontroller.h>

SendDraftController::SendDraftController(QObject* parent) : QObject(parent) {}

SendDraftController::~SendDraftController()
{
    if (m_preparation && !m_preparation->finished()) m_preparation->cancel();
    if (m_import && !m_import->finished()) m_import->cancel();
}

void SendDraftController::setDraftInterface(SendDraftInterface* draft_interface)
{
    if (m_interface == draft_interface) return;
    if (m_interface) disconnect(m_interface, nullptr, this, nullptr);
    dropAll();
    m_interface = draft_interface;
    if (m_interface) {
        connect(m_interface, &SendDraftInterface::draftChanged, this, [this] {
            // A result answers for the draft it was started for.
            if (!m_interface) return;
            if (m_interface->draftRevision() != m_sweep_draft) cancelSweepConsent();
            if (m_interface->draftRevision() != m_draft) dropAll();
        });
        connect(m_interface, &SendDraftInterface::availableChanged, this, [this] {
            if (m_interface && !m_interface->available()) dropAll();
        });
    }
    Q_EMIT draftInterfaceChanged();
}

void SendDraftController::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    if (!m_active) dropAll();
    Q_EMIT activeChanged();
}

PreparationOperation::State SendDraftController::preparationState() const
{
    if (m_preparation_refused) return PreparationOperation::State::Failed;
    return m_preparation ? m_preparation->state() : PreparationOperation::State::None;
}

QString SendDraftController::preparationError() const
{
    if (m_preparation_refused) return tr("Another wallet action is still running.");
    return m_preparation ? m_preparation->error() : QString{};
}

void SendDraftController::prepare()
{
    if (!m_interface || !m_active || !m_interface->available() || preparing()) return;
    dropPreparation();
    auto* started = m_interface->startPreparation(std::nullopt);
    m_draft = m_interface->draftRevision();
    m_preparation_refused = !started;
    if (started) {
        started->setParent(this);
        m_preparation = started;
        connect(started, &PreparationOperation::changed, this, &SendDraftController::preparationChanged);
    }
    Q_EMIT preparationChanged();
}

void SendDraftController::unlock(const QString& passphrase)
{
    if (!passwordRequested() || preparing() || !m_interface || !m_active) return;
    if (m_preparation->state() == PreparationOperation::State::NeedsUnlock) {
        m_preparation->unlock(passphrase);
        return;
    }
    // The last try failed: prepare again with this password. A refused retry
    // keeps the failed one and its prompt.
    auto* started = m_interface->startPreparation(passphrase);
    if (!started) return;
    delete m_preparation.data();
    started->setParent(this);
    m_preparation = started;
    m_draft = m_interface->draftRevision();
    connect(started, &PreparationOperation::changed, this, &SendDraftController::preparationChanged);
    Q_EMIT preparationChanged();
}

void SendDraftController::cancelPreparation()
{
    if (m_preparation && !m_preparation->finished()) m_preparation->cancel();
    // A failed retry's prompt goes with it.
    else if (passwordRequested()) dropPreparation();
}

void SendDraftController::dismissPreparationResult()
{
    if (!m_preparation_refused && (!m_preparation || !m_preparation->finished())) return;
    dropPreparation();
}

void SendDraftController::importPsbt(const QString& path)
{
    if (!m_interface || !m_active || importing() || path.isEmpty()) return;
    dropImport();
    auto* started = m_interface->startImport(path);
    if (!started) return;
    m_draft = m_interface->draftRevision();
    started->setParent(this);
    m_import = started;
    connect(started, &PsbtOperation::changed, this, &SendDraftController::importChangedState);
    Q_EMIT importChanged();
}

void SendDraftController::importChangedState()
{
    // A coin selection still being applied makes the result stale.
    if (m_import && m_import->finished() && m_interface && m_interface->selectionPending()) {
        dropImport();
        return;
    }
    Q_EMIT importChanged();
}

void SendDraftController::askSweepConsent()
{
    if (!m_interface || !m_active || m_sweep_consent) return;
    m_sweep_consent = true;
    m_sweep_draft = m_interface->draftRevision();
    Q_EMIT sweepConsentChanged();
}

void SendDraftController::confirmSweepConsent()
{
    if (!m_sweep_consent) return;
    const bool same_draft = m_interface && m_interface->draftRevision() == m_sweep_draft;
    cancelSweepConsent();
    if (same_draft) prepare();
}

void SendDraftController::cancelSweepConsent()
{
    if (!m_sweep_consent) return;
    m_sweep_consent = false;
    m_sweep_draft.clear();
    Q_EMIT sweepConsentChanged();
}

void SendDraftController::dropAll()
{
    cancelSweepConsent();
    dropPreparation();
    dropImport();
}

void SendDraftController::dropPreparation()
{
    const bool had = m_preparation || m_preparation_refused;
    if (m_preparation && !m_preparation->finished()) m_preparation->cancel();
    delete m_preparation.data();
    m_preparation_refused = false;
    if (had) Q_EMIT preparationChanged();
}

void SendDraftController::dropImport()
{
    const bool had = m_import;
    if (m_import && !m_import->finished()) m_import->cancel();
    delete m_import.data();
    if (had) Q_EMIT importChanged();
}
