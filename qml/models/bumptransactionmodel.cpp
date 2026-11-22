// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bumptransactionmodel.h>

#include <interfaces/wallet.h>
#include <qml/bitcoinamount.h>
#include <qml/models/walletunlock.h>
#include <qml/util.h>
#include <wallet/coincontrol.h>

namespace {
QString FormatFee(CAmount amount)
{
    BitcoinAmount bitcoin_amount;
    bitcoin_amount.setSatoshi(amount);
    return bitcoin_amount.displayWithUnit();
}
} // namespace

BumpTransactionModel::BumpTransactionModel(interfaces::Wallet* wallet, QObject* parent)
    : QObject(parent)
    , m_wallet(wallet)
{
}

QString BumpTransactionModel::oldFee() const
{
    return FormatFee(m_old_fee);
}

QString BumpTransactionModel::newFee() const
{
    return FormatFee(m_new_fee);
}

QString BumpTransactionModel::feeIncrease() const
{
    return FormatFee(m_new_fee - m_old_fee);
}

QString BumpTransactionModel::oldTxid() const
{
    return QString::fromStdString(m_original_txid.GetHex());
}

void BumpTransactionModel::setState(State state)
{
    if (m_state != state) {
        m_state = state;
        Q_EMIT stateChanged();
    }
}

void BumpTransactionModel::setActionType(ActionType type)
{
    if (m_action_type != type) {
        m_action_type = type;
        Q_EMIT actionTypeChanged();
    }
}

void BumpTransactionModel::setNeedsUnlock(bool needs_unlock)
{
    if (m_needs_unlock != needs_unlock) {
        m_needs_unlock = needs_unlock;
        Q_EMIT needsUnlockChanged();
    }
}

void BumpTransactionModel::setError(const QString& error)
{
    m_error = error;
    setNeedsUnlock(false);
    setState(Failed);
    Q_EMIT resultChanged();
}

void BumpTransactionModel::setUnlockRequired(const QString& error)
{
    m_error = error;
    setNeedsUnlock(true);
    Q_EMIT resultChanged();
}

void BumpTransactionModel::setSecurityStateChangedFn(std::function<void()> fn)
{
    m_security_state_changed_fn = std::move(fn);
}

void BumpTransactionModel::prepareFeeBump(const QString& txid, unsigned int targetBlocks)
{
    if (!m_wallet) {
        setError(tr("No wallet available."));
        return;
    }

    auto parsed = uint256::FromHex(txid.toStdString());
    if (!parsed) {
        setError(tr("Invalid transaction ID."));
        return;
    }

    setActionType(SpeedUp);
    setNeedsUnlock(false);
    setState(Preparing);

    wallet::CCoinControl coin_control;
    coin_control.m_signal_bip125_rbf = true;
    coin_control.m_confirm_target = targetBlocks;

    std::vector<bilingual_str> errors;
    CAmount old_fee{0};
    CAmount new_fee{0};
    CMutableTransaction mtx;

    if (!m_wallet->createBumpTransaction(Txid::FromUint256(*parsed), coin_control, errors, old_fee, new_fee, mtx)) {
        setError(errors.empty() ? tr("Failed to create bump transaction.") : QString::fromStdString(errors[0].translated));
        return;
    }

    m_original_txid = Txid::FromUint256(*parsed);
    m_old_fee = old_fee;
    m_new_fee = new_fee;
    m_bump_mtx = std::move(mtx);

    setState(NeedsConfirmation);
    Q_EMIT resultChanged();
}

bool BumpTransactionModel::confirmFeeBump()
{
    return confirmFeeBumpInternal(std::nullopt);
}

bool BumpTransactionModel::confirmFeeBumpWithPassphrase(const QString& passphrase)
{
    return confirmFeeBumpInternal(std::optional<SecureString>{QmlUtil::SecureStringFromQString(passphrase)});
}

bool BumpTransactionModel::confirmFeeBumpInternal(std::optional<SecureString> passphrase)
{
    if (!m_wallet || m_state != NeedsConfirmation) {
        if (passphrase.has_value()) {
            QmlUtil::ClearSecureString(*passphrase);
            passphrase.reset();
        }
        return false;
    }

    std::unique_lock<std::mutex> unlock_session;
    const bool crypted{m_wallet->isCrypted()};
    if (crypted) unlock_session = m_unlock_session->tryAcquire();
    if (crypted && !unlock_session) {
        if (passphrase.has_value()) {
            QmlUtil::ClearSecureString(*passphrase);
            passphrase.reset();
        }
        // Stay in NeedsConfirmation so the same bump can be retried.
        m_error = tr("The wallet is busy with another operation. Try again in a moment.");
        setNeedsUnlock(false);
        Q_EMIT resultChanged();
        return false;
    }
    if (!m_wallet->privateKeysDisabled() && m_wallet->isCrypted() && m_wallet->isLocked() && !passphrase.has_value()) {
        setUnlockRequired(tr("Enter your wallet password to update this transaction."));
        return false;
    }

    bool relock{false};
    if (!unlockForCommit(passphrase, relock)) {
        return false;
    }
    WalletRelockGuard relock_guard{*m_wallet, [this] {
        if (m_security_state_changed_fn) {
            m_security_state_changed_fn();
        }
    }, relock};

    setNeedsUnlock(false);
    setState(Committing);

    if (!m_wallet->transactionCanBeBumped(m_original_txid)) {
        relock_guard.relock();
        setError(tr("Transaction can no longer be bumped."));
        return false;
    }

    if (!m_wallet->signBumpTransaction(m_bump_mtx)) {
        relock_guard.relock();
        setError(tr("Failed to sign transaction."));
        return false;
    }

    std::vector<bilingual_str> errors;
    Txid bumped_txid;
    if (!m_wallet->commitBumpTransaction(m_original_txid, std::move(m_bump_mtx), errors, bumped_txid)) {
        relock_guard.relock();
        setError(errors.empty() ? tr("Failed to commit transaction.") : QString::fromStdString(errors[0].translated));
        return false;
    }

    m_new_txid = QString::fromStdString(bumped_txid.GetHex());
    relock_guard.relock();
    setState(Succeeded);
    Q_EMIT resultChanged();
    return true;
}

bool BumpTransactionModel::unlockForCommit(std::optional<SecureString>& passphrase, bool& relock)
{
    relock = false;
    if (!m_wallet) {
        if (passphrase.has_value()) {
            QmlUtil::ClearSecureString(*passphrase);
            passphrase.reset();
        }
        return false;
    }
    if (!passphrase.has_value()) {
        return true;
    }

    const auto result{TryUnlockWithPassphrase(*m_wallet, *passphrase)};
    passphrase.reset();
    switch (result) {
    case WalletUnlockResult::IncorrectPassphrase:
        setUnlockRequired(tr("The wallet password you entered was incorrect."));
        return false;
    case WalletUnlockResult::AlreadyUnlocked:
        return true;
    case WalletUnlockResult::UnlockedNowRelockRequired:
        relock = true;
        if (m_security_state_changed_fn) {
            m_security_state_changed_fn();
        }
        return true;
    }
    return false;
}

void BumpTransactionModel::reset()
{
    m_state = Idle;
    m_action_type = SpeedUp;
    m_old_fee = 0;
    m_new_fee = 0;
    m_bump_mtx = CMutableTransaction{};
    m_original_txid = Txid{};
    m_new_txid.clear();
    m_error.clear();
    m_needs_unlock = false;

    Q_EMIT stateChanged();
    Q_EMIT actionTypeChanged();
    Q_EMIT resultChanged();
    Q_EMIT needsUnlockChanged();
}
