// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bumptransactionmodel.h>

#include <interfaces/wallet.h>
#include <qml/bitcoinamount.h>
#include <qml/models/walletunlock.h>
#include <qml/util.h>
#include <wallet/coincontrol.h>

#include <QScopeGuard>

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
    m_bump_mtx = CMutableTransaction{};
    m_sp_recipients.clear();
    m_bump_signed = false;
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
    reset();
    prepareFeeBumpInternal(txid, targetBlocks, std::nullopt);
}

bool BumpTransactionModel::prepareFeeBumpWithPassphrase(const QString& txid, unsigned int targetBlocks, const QString& passphrase)
{
    const auto parsed = uint256::FromHex(txid.toStdString());
    if (m_state != Preparing || !m_needs_unlock || !parsed || Txid::FromUint256(*parsed) != m_original_txid) return false;
    return prepareFeeBumpInternal(txid, targetBlocks, std::optional<SecureString>{QmlUtil::SecureStringFromQString(passphrase)});
}

bool BumpTransactionModel::prepareFeeBumpInternal(const QString& txid, unsigned int target_blocks, std::optional<SecureString> passphrase)
{
    const auto clear_passphrase = qScopeGuard([&] {
        if (passphrase) QmlUtil::ClearSecureString(*passphrase);
    });
    if (!m_wallet) {
        setError(tr("No wallet available."));
        return false;
    }

    auto parsed = uint256::FromHex(txid.toStdString());
    if (!parsed) {
        setError(tr("Invalid transaction ID."));
        return false;
    }
    m_original_txid = Txid::FromUint256(*parsed);
    m_error.clear();
    setActionType(SpeedUp);
    setNeedsUnlock(false);
    setState(Preparing);

    // TODO: Revisit SP detection after Core fixes RBF for pre-payment backups;
    // restored transactions may lack both SP recipients and the RBF marker.
    const bool silent_payment = m_wallet->getWalletTx(m_original_txid).rbf_needs_sp_outputs;
    if (silent_payment && (m_wallet->privateKeysDisabled() || m_wallet->hasExternalSigner())) {
        //: Error when increasing a Silent Payments transaction's fee without local wallet private keys.
        setError(tr("This interface currently requires a wallet with local private keys to bump silent payments."));
        return false;
    }
    if (silent_payment && m_wallet->isCrypted() && m_wallet->isLocked() && !passphrase) {
        //: Password prompt before preparing and signing a Silent Payments replacement for fee review. Nothing is sent yet.
        setUnlockRequired(tr("Enter your wallet password to prepare this transaction for review."));
        return false;
    }
    bool relock{false};
    if (silent_payment && !unlockForAction(passphrase, relock)) return false;
    WalletRelockGuard relock_guard{*m_wallet, [this] {
                                       if (m_security_state_changed_fn) m_security_state_changed_fn();
                                   },
                                   relock};

    wallet::CCoinControl coin_control;
    coin_control.m_signal_bip125_rbf = true;
    coin_control.m_confirm_target = target_blocks;

    std::vector<bilingual_str> errors;
    CAmount old_fee{0};
    CAmount new_fee{0};
    CMutableTransaction mtx;
    std::vector<bip352::SilentPaymentsDestination> sp_recipients;

    if (!m_wallet->createBumpTransaction(m_original_txid, coin_control, errors, old_fee, new_fee, mtx, sp_recipients)) {
        relock_guard.relock();
        setError(errors.empty() ? tr("Failed to create bump transaction.") : QString::fromStdString(errors[0].translated));
        return false;
    }
    if (silent_payment) {
        if (sp_recipients.empty()) {
            relock_guard.relock();
            //: Error when the wallet backend does not provide Silent Payments recipients for a replacement transaction.
            setError(tr("Silent payment recipient data is missing for this transaction."));
            return false;
        }
        if (!m_wallet->signBumpTransaction(mtx)) {
            relock_guard.relock();
            //: Error while signing a prepared replacement transaction before showing its fee for confirmation.
            setError(tr("Failed to sign transaction."));
            return false;
        }
    }

    m_old_fee = old_fee;
    m_new_fee = new_fee;
    m_bump_mtx = std::move(mtx);
    m_sp_recipients = std::move(sp_recipients);
    m_bump_signed = silent_payment;

    relock_guard.relock();
    setState(NeedsConfirmation);
    Q_EMIT resultChanged();
    return true;
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

    if (!m_bump_signed && !m_wallet->privateKeysDisabled() && m_wallet->isCrypted() && m_wallet->isLocked() && !passphrase.has_value()) {
        setUnlockRequired(tr("Enter your wallet password to update this transaction."));
        return false;
    }

    bool relock{false};
    if (!m_bump_signed && !unlockForAction(passphrase, relock)) {
        return false;
    }
    if (passphrase) {
        QmlUtil::ClearSecureString(*passphrase);
        passphrase.reset();
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

    if (!m_bump_signed && !m_wallet->signBumpTransaction(m_bump_mtx)) {
        relock_guard.relock();
        setError(tr("Failed to sign transaction."));
        return false;
    }

    std::vector<bilingual_str> errors;
    Txid bumped_txid;
    if (!m_wallet->commitBumpTransaction(m_original_txid, std::move(m_bump_mtx), errors, bumped_txid, m_sp_recipients)) {
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

bool BumpTransactionModel::unlockForAction(std::optional<SecureString>& passphrase, bool& relock)
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
    m_sp_recipients.clear();
    m_original_txid = Txid{};
    m_new_txid.clear();
    m_error.clear();
    m_needs_unlock = false;
    m_bump_signed = false;

    Q_EMIT stateChanged();
    Q_EMIT actionTypeChanged();
    Q_EMIT resultChanged();
    Q_EMIT needsUnlockChanged();
}
