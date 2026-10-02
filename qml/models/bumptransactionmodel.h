// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BUMPTRANSACTIONMODEL_H
#define BITCOIN_QML_MODELS_BUMPTRANSACTIONMODEL_H

#include <consensus/amount.h>
#include <primitives/transaction.h>
#include <support/allocators/secure.h>
#include <uint256.h>

#include <functional>
#include <optional>
#include <vector>

#include <QObject>
#include <QString>

namespace interfaces {
class Wallet;
} // namespace interfaces

struct bilingual_str;

class BumpTransactionModel : public QObject
{
    Q_OBJECT

public:
    void detachWallet() { m_wallet = nullptr; }
    enum State { Idle, Preparing, NeedsConfirmation, Committing, Succeeded, Failed };
    Q_ENUM(State)

    enum ActionType { SpeedUp /*, Cancel */ };
    Q_ENUM(ActionType)

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(ActionType actionType READ actionType NOTIFY actionTypeChanged)
    Q_PROPERTY(QString oldFee READ oldFee NOTIFY resultChanged)
    Q_PROPERTY(QString newFee READ newFee NOTIFY resultChanged)
    Q_PROPERTY(QString feeIncrease READ feeIncrease NOTIFY resultChanged)
    Q_PROPERTY(QString oldTxid READ oldTxid NOTIFY resultChanged)
    Q_PROPERTY(QString newTxid READ newTxid NOTIFY resultChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY resultChanged)
    Q_PROPERTY(bool needsUnlock READ needsUnlock NOTIFY needsUnlockChanged)

    explicit BumpTransactionModel(interfaces::Wallet* wallet, QObject* parent = nullptr);

    State state() const { return m_state; }
    ActionType actionType() const { return m_action_type; }
    QString oldFee() const;
    QString newFee() const;
    QString feeIncrease() const;
    QString oldTxid() const;
    QString newTxid() const { return m_new_txid; }
    QString errorText() const { return m_error; }
    bool needsUnlock() const { return m_needs_unlock; }

    Q_INVOKABLE void prepareFeeBump(const QString& txid, unsigned int targetBlocks);
    Q_INVOKABLE bool confirmFeeBump();
    Q_INVOKABLE bool confirmFeeBumpWithPassphrase(const QString& passphrase);
    Q_INVOKABLE void reset();
    void setSecurityStateChangedFn(std::function<void()> fn);

Q_SIGNALS:
    void stateChanged();
    void actionTypeChanged();
    void resultChanged();
    void needsUnlockChanged();

private:
    void setState(State state);
    void setActionType(ActionType type);
    void setError(const QString& error);
    void setNeedsUnlock(bool needs_unlock);
    void setUnlockRequired(const QString& error);
    bool confirmFeeBumpInternal(std::optional<SecureString> passphrase);
    bool unlockForCommit(std::optional<SecureString>& passphrase, bool& relock);

    interfaces::Wallet* m_wallet{nullptr};
    std::function<void()> m_security_state_changed_fn;
    State m_state{Idle};
    ActionType m_action_type{SpeedUp};
    CAmount m_old_fee{0};
    CAmount m_new_fee{0};
    CMutableTransaction m_bump_mtx;
    Txid m_original_txid;
    QString m_new_txid;
    QString m_error;
    bool m_needs_unlock{false};
};

#endif // BITCOIN_QML_MODELS_BUMPTRANSACTIONMODEL_H
