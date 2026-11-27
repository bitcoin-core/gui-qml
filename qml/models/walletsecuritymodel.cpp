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
    case Error::Uncertain: return QObject::tr("The wallet action may not have finished. Check the wallet before trying again.");
    case Error::Rejected: return QObject::tr("The wallet operation could not be started.");
    case Error::ConfirmationMismatch: return QObject::tr("The new passwords do not match.");
    }
    return QObject::tr("The wallet operation could not be started.");
}
} // namespace

SecurityOperationModel::SecurityOperationModel(std::shared_ptr<walletopening::Session> opening, Kind kind,
                                             std::shared_ptr<QObject> bridge, std::function<bool()> submitted,
                                             QObject* parent)
    : SecurityOperation(kind, parent), m_id(QUuid::createUuid().toString(QUuid::WithoutBraces)),
      m_opening(std::move(opening)), m_bridge(std::move(bridge)), m_admission(std::move(submitted))
{
}

bool SecurityOperationModel::canSubmit() const
{
    return !m_closed && SecurityOperation::canSubmit() && m_opening && m_opening->available();
}

bool SecurityOperationModel::submitCredentials(const QString& prompt, const QString& current,
                                              const QString& replacement, const QString& confirmation)
{
    if (prompt != m_id || !canSubmit() || !m_admission || !m_admission()) return false;
    auto captured_current = secretinput::Capture(m_opening, current);
    auto captured_replacement = secretinput::Capture(m_opening, replacement);
    auto captured_confirmation = secretinput::Capture(m_opening, confirmation);
    if (!captured_current || !captured_replacement || !captured_confirmation) {
        publish(Phase::Failed, tr("The password could not be used. The wallet password was not changed."));
        return false;
    }
    const QPointer<SecurityOperationModel> observer{this};
    const auto kind = this->kind() == Kind::Encrypt ? walletsecurity::Kind::Encrypt : walletsecurity::Kind::ChangePassphrase;
    m_owner = std::make_shared<walletsecurity::Operation>(m_opening, kind,
        [bridge = m_bridge, observer] {
            QMetaObject::invokeMethod(bridge.get(), [observer] { if (observer) observer->consume(); }, Qt::QueuedConnection);
        });
    // Pending before the start: a reentrant submit is refused. Even transport
    // rejection owns a definite outcome, reported later by consume().
    publish(Phase::Pending);
    if (!observer) return false;
    const auto submitted = m_owner->start({kind, {}, {}, std::move(captured_current.input),
                                          std::move(captured_replacement.input), std::move(captured_confirmation.input)});
    return bool(submitted);
}

bool SecurityOperationModel::cancel()
{
    if (phase() != Phase::AwaitingCredentials) return false;
    publish(Phase::Cancelled);
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
    const auto outcome = result->error == walletsecurity::Error::None ? Phase::Succeeded
        : result->error == walletsecurity::Error::Uncertain ? Phase::Uncertain : Phase::Failed;
    // An operation's sampled lock result is historical. Refresh Session's sole
    // observation stream on every outcome instead of overwriting newer RPC state.
    m_opening->refreshMetadata();
    publish(outcome, ErrorText(result->error));
}

void WalletSecurityModel::bind(std::shared_ptr<walletopening::Session> opening, std::shared_ptr<QObject> bridge,
                             std::function<bool()> submitted)
{
    if (m_opening || m_closed) return;
    m_opening = std::move(opening);
    m_bridge = std::move(bridge);
    m_admission = std::move(submitted);
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
    Q_EMIT availableChanged();
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
