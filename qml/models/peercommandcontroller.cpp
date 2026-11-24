// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/peercommandcontroller.h>

#include <QQmlEngine>

PeerCommandController::PeerCommandController(QObject* parent) : QObject(parent) {}

PeerCommandController::~PeerCommandController()
{
    release(m_operation);
}

void PeerCommandController::release(PeerCommandOperation* operation)
{
    if (!operation) return;
    disconnect(operation, nullptr, this, nullptr);
    operation->retire();
    QQmlEngine::setObjectOwnership(operation, QQmlEngine::JavaScriptOwnership);
}

void PeerCommandController::setPresenting(bool presenting)
{
    if (m_presenting == presenting) return;
    m_presenting = presenting;
    Q_EMIT presentingChanged();
}

PeerCommandController::Prompt PeerCommandController::prompt() const
{
    return m_operation && m_operation->confirmationRequired() ? m_kind : Prompt::None;
}

bool PeerCommandController::busy() const
{
    return m_operation && (m_operation->confirmationRequired() || m_operation->pending());
}

QString PeerCommandController::errorText() const
{
    if (!m_operation) return {};
    return m_operation->queryError().isEmpty() ? m_operation->errorText() : m_operation->queryError();
}

QString PeerCommandController::banDurationText() const
{
    const int seconds = m_operation ? m_operation->banDuration() : 0;
    using Duration = PeerCommandOperation::BanDuration;
    return seconds >= int(Duration::Year) ? tr("1 year") : seconds >= int(Duration::Week) ? tr("1 week")
        : seconds >= int(Duration::Day) ? tr("1 day") : tr("1 hour");
}

bool PeerCommandController::disconnectPeer(PeerActionsInterface* peer)
{
    if (!peer || busy() || m_presenting || !peer->canDisconnect()) return false;
    return start(peer->prepareDisconnect(), Prompt::Disconnect);
}

bool PeerCommandController::banPeer(PeerActionsInterface* peer, PeerCommandOperation::BanDuration duration)
{
    if (!peer || busy() || m_presenting || !peer->canBan()) return false;
    return start(peer->prepareBan(duration), Prompt::Ban);
}

bool PeerCommandController::start(PeerCommandOperation* operation, Prompt kind)
{
    if (!operation) return false;
    // QML cannot collect the command while the controller follows it.
    QQmlEngine::setObjectOwnership(operation, QQmlEngine::CppOwnership);
    if (busy() || m_presenting) {
        release(operation);
        return false;
    }
    release(m_operation);
    m_operation = operation;
    m_kind = kind;
    m_error_shown = false;
    connect(operation, &PeerCommandOperation::changed, this, &PeerCommandController::follow);
    connect(operation, &QObject::destroyed, this, &PeerCommandController::changed);
    Q_EMIT changed();
    return true;
}

void PeerCommandController::follow()
{
    if (m_operation && m_operation->resultPresent() && !m_operation->pending() && !errorText().isEmpty()) m_error_shown = true;
    Q_EMIT changed();
}

void PeerCommandController::confirm()
{
    if (m_operation && m_operation->confirmationRequired()) m_operation->confirm();
}

void PeerCommandController::cancel()
{
    if (m_operation && m_operation->confirmationRequired()) m_operation->cancel();
}

void PeerCommandController::reconcile()
{
    if (m_operation) m_operation->reconcile();
}

void PeerCommandController::dismissError()
{
    if (!m_error_shown) return;
    m_error_shown = false;
    Q_EMIT changed();
}
