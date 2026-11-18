// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/requestdeletecontroller.h>

#include <QQmlEngine>

RequestDeleteController::RequestDeleteController(QObject* parent) : QObject(parent) {}

RequestDeleteController::~RequestDeleteController()
{
    if (!m_deletion) return;
    disconnect(m_deletion, nullptr, this, nullptr);
    if (!m_deletion->busy()) m_deletion->retire();
    QQmlEngine::setObjectOwnership(m_deletion, QQmlEngine::JavaScriptOwnership);
}

void RequestDeleteController::setReceive(WalletReceiveModel* receive)
{
    if (m_receive == receive) return;
    reset();
    m_receive = receive;
    Q_EMIT receiveChanged();
}

bool RequestDeleteController::asking() const
{
    return m_deletion && m_deletion->closePrompt() && !m_deletion->busy();
}

bool RequestDeleteController::failed() const
{
    if (!m_refusal.isEmpty()) return true;
    if (!m_deletion || m_deletion->busy() || m_deletion->closePrompt()) return false;
    const auto result = m_deletion->saveResult();
    return (result.present && !result.erased) || m_deletion->error().code != TextEditError::None;
}

QString RequestDeleteController::error() const
{
    if (!failed()) return {};
    if (!m_refusal.isEmpty()) return m_refusal;
    const auto result = m_deletion->saveResult();
    return !result.errorText.isEmpty() ? result.errorText : m_deletion->error().message;
}

bool RequestDeleteController::request(const QString& id)
{
    // A question already open stays the one on screen.
    if (m_deletion) return false;
    if (!m_receive) return refuse(tr("Payment requests are not available."));
    auto* deletion = m_receive->deleteRequest(id);
    if (!deletion) {
        const auto reason = m_receive->error().message;
        return refuse(reason.isEmpty() ? tr("This payment request could not be deleted. Try again.") : reason);
    }
    m_refusal.clear();
    // QML does not collect the question while this controller follows it.
    QQmlEngine::setObjectOwnership(deletion, QQmlEngine::CppOwnership);
    m_deletion = deletion;
    connect(deletion, &PaymentRequestEditorModel::changed, this, &RequestDeleteController::changed);
    connect(deletion, &PaymentRequestEditorModel::keptEditing, this, &RequestDeleteController::release);
    connect(deletion, &PaymentRequestEditorModel::closed, this, &RequestDeleteController::release);
    connect(deletion, &QObject::destroyed, this, &RequestDeleteController::changed);
    connect(deletion, &PaymentRequestEditorModel::persisted, this, [this](const RequestSaveResult& result) {
        if (result.erased) Q_EMIT deleted(result.requestId);
    });
    Q_EMIT changed();
    return true;
}

void RequestDeleteController::confirm()
{
    if (asking()) m_deletion->closePrompt()->answer(TextEditClosePrompt::Save);
}

void RequestDeleteController::cancel()
{
    if (asking()) m_deletion->closePrompt()->answer(TextEditClosePrompt::KeepEditing);
}

bool RequestDeleteController::refuse(QString reason)
{
    m_refusal = std::move(reason);
    Q_EMIT changed();
    return false;
}

void RequestDeleteController::dismiss()
{
    if (!failed()) return;
    if (!m_refusal.isEmpty()) { m_refusal.clear(); Q_EMIT changed(); return; }
    m_deletion->retire();
    release();
}

void RequestDeleteController::reset()
{
    if (!m_refusal.isEmpty()) { m_refusal.clear(); Q_EMIT changed(); }
    if (!m_deletion) return;
    if (asking()) { cancel(); return; }
    if (!m_deletion->busy()) m_deletion->retire();
    release();
}

void RequestDeleteController::release()
{
    if (!m_deletion) return;
    disconnect(m_deletion, nullptr, this, nullptr);
    QQmlEngine::setObjectOwnership(m_deletion, QQmlEngine::JavaScriptOwnership);
    m_deletion = nullptr;
    Q_EMIT changed();
}
