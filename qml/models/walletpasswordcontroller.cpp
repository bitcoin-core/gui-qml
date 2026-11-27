// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletpasswordcontroller.h>

#include <utility>

WalletPasswordController::WalletPasswordController(QObject* parent) : WalletPageController(parent) {}

WalletPasswordController::~WalletPasswordController()
{
    if (m_operation) {
        disconnect(m_operation, nullptr, this, nullptr);
        if (m_operation->canSubmit()) m_operation->cancel();
    }
}

void WalletPasswordController::setSecurity(WalletSecurityInterface* security)
{
    if (m_security == security || !bindWallet(security)) return;
    m_security = security;
    Q_EMIT securityChanged();
}

QString WalletPasswordController::errorText() const
{
    if (!m_admission_error.isEmpty()) return m_admission_error;
    if (!m_operation) return {};
    const auto phase = m_operation->phase();
    return phase == SecurityOperation::Phase::Failed || phase == SecurityOperation::Phase::Uncertain ? m_operation->error() : QString{};
}

bool WalletPasswordController::passwordUpdated() const
{
    return m_operation && m_operation->phase() == SecurityOperation::Phase::Succeeded
        && m_operation->kind() == SecurityOperation::Kind::ChangePassphrase;
}

bool WalletPasswordController::encrypt(const QString& replacement, const QString& confirmation)
{
    return start(SecurityOperation::Kind::Encrypt, {}, replacement, confirmation);
}

bool WalletPasswordController::changePassphrase(const QString& current, const QString& replacement, const QString& confirmation)
{
    return start(SecurityOperation::Kind::ChangePassphrase, current, replacement, confirmation);
}

bool WalletPasswordController::start(SecurityOperation::Kind kind, const QString& current, const QString& replacement, const QString& confirmation)
{
    forget();
    const QPointer<WalletPasswordController> guard{this};
    auto* operation = !available() || !m_security ? nullptr
        : kind == SecurityOperation::Kind::Encrypt ? m_security->beginEncryption() : m_security->beginPassphraseChange();
    if (!guard) return false;
    if (!operation) {
        m_admission_error = tr("The wallet operation could not be started.");
        Q_EMIT changed();
        return false;
    }
    m_operation = operation;
    m_reported = false;
    connect(operation, &SecurityOperation::changed, this, &WalletPasswordController::follow);
    connect(operation, &QObject::destroyed, this, &WalletPasswordController::changed);
    Q_EMIT changed();
    if (!guard || m_operation != operation) return false;
    const bool admitted = operation->submit(current, replacement, confirmation);
    if (!guard || m_operation != operation) return admitted;
    if (!admitted && operation->canSubmit()) {
        operation->cancel();
        if (!guard) return false;
        m_admission_error = tr("The wallet operation could not be started.");
        Q_EMIT changed();
    }
    return admitted;
}

void WalletPasswordController::clearResult()
{
    const bool had_error = !m_admission_error.isEmpty();
    m_admission_error.clear();
    if (m_operation && !m_operation->busy()) {
        disconnect(m_operation, nullptr, this, nullptr);
        m_operation = nullptr;
    } else if (!had_error) {
        return;
    }
    Q_EMIT changed();
}

void WalletPasswordController::follow()
{
    if (!m_operation) return;
    const QPointer<WalletPasswordController> guard{this};
    Q_EMIT changed();
    if (!guard || !m_operation) return;
    if (m_operation->phase() == SecurityOperation::Phase::Succeeded && !std::exchange(m_reported, true)) {
        Q_EMIT succeeded(m_operation->kind());
    }
}

void WalletPasswordController::forget()
{
    if (m_operation) disconnect(m_operation, nullptr, this, nullptr);
    m_operation = nullptr;
    m_admission_error.clear();
}

void WalletPasswordController::stop()
{
    const QPointer<SecurityOperation> operation{m_operation};
    const bool had_state = operation || !m_admission_error.isEmpty();
    forget();
    if (operation && operation->canSubmit()) operation->cancel();
    if (had_state) Q_EMIT changed();
}
