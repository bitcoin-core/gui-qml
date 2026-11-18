// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletsecuritymodel.h>

#include <QUuid>

#include <utility>

namespace {
QString ErrorText(walletsecurity::Error error)
{
    using Error = walletsecurity::Error;
    switch (error) {
    case Error::None: return {};
    case Error::MissingCurrent: return QObject::tr("Enter the current wallet password.");
    case Error::MissingNew: return QObject::tr("Enter a new wallet password.");
    case Error::NoPrivateKeys: return QObject::tr("This wallet cannot manage a local password.");
    case Error::EncryptionFailed: return QObject::tr("The wallet password could not be set.");
    case Error::IncorrectCurrent: return QObject::tr("The current wallet password was incorrect.");
    case Error::ChangeFailed: return QObject::tr("The wallet password could not be changed.");
    case Error::Uncertain: return QObject::tr("The wallet operation outcome could not be confirmed. Check the wallet before retrying.");
    case Error::Rejected: return QObject::tr("The wallet operation could not be started.");
    case Error::ConfirmationMismatch: return QObject::tr("The new passwords do not match.");
    }
    return QObject::tr("The wallet operation could not be started.");
}
} // namespace

SecurityOperationModel::SecurityOperationModel(std::shared_ptr<walletopening::Session> opening, Kind kind,
                                             std::shared_ptr<QObject> bridge, std::function<bool()> admission,
                                             QObject* parent)
    : QObject(parent), m_id(QUuid::createUuid().toString(QUuid::WithoutBraces)), m_kind(kind),
      m_opening(std::move(opening)), m_bridge(std::move(bridge)), m_admission(std::move(admission))
{
}

bool SecurityOperationModel::canSubmit() const
{
    return !m_closed && m_phase == Phase::AwaitingCredentials && m_opening && m_opening->available();
}

bool SecurityOperationModel::submitCredentials(const QString& prompt, const QString& current,
                                              const QString& replacement, const QString& confirmation)
{
    if (prompt != m_id || !canSubmit() || !m_admission || !m_admission()) return false;
    auto captured_current = secretinput::Capture(m_opening->scheduler(), current);
    auto captured_replacement = secretinput::Capture(m_opening->scheduler(), replacement);
    auto captured_confirmation = secretinput::Capture(m_opening->scheduler(), confirmation);
    if (!captured_current || !captured_replacement || !captured_confirmation) {
        m_phase = Phase::Failed;
        m_error = tr("The password input could not be queued. No wallet password was changed.");
        Q_EMIT changed();
        return false;
    }
    const QPointer<SecurityOperationModel> observer{this};
    const auto kind = m_kind == Kind::Encrypt ? walletsecurity::Kind::Encrypt : walletsecurity::Kind::ChangePassphrase;
    m_owner = std::make_shared<walletsecurity::Operation>(m_opening, kind,
        [bridge = m_bridge, observer] {
            QMetaObject::invokeMethod(bridge.get(), [observer] { if (observer) observer->consume(); }, Qt::QueuedConnection);
        });
    m_phase = Phase::Pending;
    const auto admission = m_owner->start({kind, {}, {}, std::move(captured_current.input),
                                          std::move(captured_replacement.input), std::move(captured_confirmation.input)});
    // Even transport rejection owns a definite outcome. Never turn a successful
    // enqueue into a claim that the password changed.
    Q_EMIT changed();
    return bool(admission);
}

bool SecurityOperationModel::cancel()
{
    if (m_phase != Phase::AwaitingCredentials) return false;
    m_phase = Phase::Cancelled;
    Q_EMIT changed();
    return true;
}

void SecurityOperationModel::close()
{
    m_closed = true;
    if (!cancel()) Q_EMIT changed();
}

void SecurityOperationModel::consume()
{
    if (!m_owner || terminal()) return;
    const auto result = m_owner->result();
    if (!result) return;
    if (result->error == walletsecurity::Error::None) m_phase = Phase::Succeeded;
    else if (result->error == walletsecurity::Error::Uncertain) m_phase = Phase::Uncertain;
    else m_phase = Phase::Failed;
    m_error = ErrorText(result->error);
    // An operation's sampled lock result is historical. Refresh Session's sole
    // observation stream on every outcome instead of overwriting newer RPC state.
    m_opening->refreshMetadata();
    Q_EMIT changed();
}

void WalletSecurityModel::bind(std::shared_ptr<walletopening::Session> opening, std::shared_ptr<QObject> bridge,
                             std::function<bool()> admission)
{
    if (m_opening || m_closed) return;
    m_opening = std::move(opening);
    m_bridge = std::move(bridge);
    m_admission = std::move(admission);
}

WalletSecurityModel::ObservedState WalletSecurityModel::observedState() const
{
    if (m_closed) return ObservedState::Closed;
    if (!m_known) return ObservedState::Unknown;
    if (!m_encrypted) return ObservedState::Unencrypted;
    return m_locked ? ObservedState::Locked : ObservedState::Unlocked;
}

bool WalletSecurityModel::canManagePassphrase() const
{
    return !m_closed && m_known && !m_no_keys && m_opening && m_opening->available();
}

void WalletSecurityModel::observe(const walletopening::Metadata& metadata)
{
    if (m_closed) return;
    const bool changed = !m_known || m_encrypted != metadata.encrypted || m_locked != metadata.locked
        || m_no_keys != metadata.private_keys_disabled;
    m_known = true;
    m_encrypted = metadata.encrypted;
    m_locked = metadata.locked;
    m_no_keys = metadata.private_keys_disabled;
    if (changed) {
        const QPointer<WalletSecurityModel> observer{this};
        Q_EMIT observedChanged();
        if (observer) Q_EMIT capabilitiesChanged();
    }
}

void WalletSecurityModel::close()
{
    if (std::exchange(m_closed, true)) return;
    const QPointer<WalletSecurityModel> observer{this};
    if (m_active) m_active->close();
    if (!observer) return;
    Q_EMIT observedChanged();
    if (observer) Q_EMIT capabilitiesChanged();
}

SecurityOperationModel* WalletSecurityModel::beginEncryption()
{
    return begin(SecurityOperationModel::Kind::Encrypt);
}

SecurityOperationModel* WalletSecurityModel::beginPassphraseChange()
{
    return begin(SecurityOperationModel::Kind::ChangePassphrase);
}

bool WalletSecurityModel::canBegin() const
{
    return !m_closed && m_opening && m_opening->available() && m_admission && m_admission()
        && m_live_operations < 128 && (!m_active || (m_active->terminal()
            && m_active->phase() != SecurityOperationModel::Phase::Uncertain));
}

SecurityOperationModel* WalletSecurityModel::begin(SecurityOperationModel::Kind kind)
{
    if (!canBegin()) return nullptr;
    const QPointer<WalletSecurityModel> observer{this};
    const QPointer<SecurityOperationModel> previous{m_active};
    auto* operation = new SecurityOperationModel(m_opening, kind, m_bridge, m_admission, this);
    ++m_live_operations;
    connect(operation, &QObject::destroyed, this, [this] { --m_live_operations; Q_EMIT capabilitiesChanged(); });
    m_active = operation;
    connect(operation, &SecurityOperationModel::changed, this, &WalletSecurityModel::operationChanged);
    if (previous) previous->deleteLater();
    Q_EMIT operationChanged();
    return observer && m_active == operation ? operation : nullptr;
}
