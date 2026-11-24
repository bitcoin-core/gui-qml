// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/paymentrequestviewcontroller.h>

#include <qml/models/paymentrequesteditormodel.h>
#include <qml/models/walletreceivemodel.h>

PaymentRequestViewController::PaymentRequestViewController(QObject* parent) : QObject(parent) {}

PaymentRequestViewController::~PaymentRequestViewController() { cancel(); }

void PaymentRequestViewController::setReceive(WalletReceiveModel* receive)
{
    if (m_receive == receive) return;
    cancel();
    dismissFailure();
    m_receive = receive;
    Q_EMIT receiveChanged();
}

void PaymentRequestViewController::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    if (!m_active) {
        cancel();
        dismissFailure();
    }
    Q_EMIT activeChanged();
}

PaymentRequestEditorModel* PaymentRequestViewController::editor() const
{
    return m_editor_receive ? m_editor.data() : nullptr;
}

bool PaymentRequestViewController::open(const QString& id)
{
    if (!m_active || !m_receive) return false;
    cancel();
    m_failure.clear();
    auto* operation = m_receive->openRequest(id, this);
    if (!operation) {
        m_failure = m_receive ? m_receive->error().message : QString{};
        Q_EMIT changed();
        return false;
    }
    m_open = operation;
    connect(operation, &PaymentRequestOpenOperation::changed, this, &PaymentRequestViewController::openChanged);
    // The open can finish before it is returned.
    if (!operation->pending()) {
        const bool opened = operation->state() == PaymentRequestOpenOperation::State::Opened;
        openChanged();
        return opened;
    }
    Q_EMIT changed();
    return true;
}

void PaymentRequestViewController::cancel()
{
    if (!m_open) return;
    const auto operation = m_open;
    m_open = nullptr;
    operation->cancel();
    operation->deleteLater();
    Q_EMIT changed();
}

void PaymentRequestViewController::release()
{
    if (!m_editor && !m_editor_receive) return;
    m_editor = nullptr;
    m_editor_receive = nullptr;
    Q_EMIT changed();
}

void PaymentRequestViewController::dismissFailure()
{
    if (m_failure.isEmpty()) return;
    m_failure.clear();
    Q_EMIT changed();
}

void PaymentRequestViewController::openChanged()
{
    const auto operation = m_open;
    if (!operation || operation->pending()) return;
    m_open = nullptr;
    operation->deleteLater();
    if (operation->state() == PaymentRequestOpenOperation::State::Opened && m_active && operation->editor()) {
        m_editor = operation->editor();
        m_editor_receive = m_receive;
    } else if (operation->state() == PaymentRequestOpenOperation::State::Failed) {
        m_failure = operation->error().message;
    }
    Q_EMIT changed();
}
