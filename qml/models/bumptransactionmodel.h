// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BUMPTRANSACTIONMODEL_H
#define BITCOIN_QML_MODELS_BUMPTRANSACTIONMODEL_H

#include <qml/models/feebumpoperation.h>
#include <qml/walletbump.h>
#include <qml/walletopening.h>
#include <qml/snapshotquery.h>
#include <qml/guipresentation.h>
#include <qml/transactionattempt.h>
#include <QObject>
#include <QString>

/** A fee bump attempt (FeeBumpOperation) on one wallet opening. The wallet's
 * FeeBumpModel creates one per review. Attempt and commit ids are C++-only. */
class BumpTransactionModel : public FeeBumpOperation
{
    Q_OBJECT
public:
    enum ActionType { SpeedUp };
    Q_ENUM(ActionType)
    enum RelayState { RelayUnknown };
    Q_ENUM(RelayState)
    Q_PROPERTY(BumpTransactionModel::ActionType actionType READ actionType NOTIFY actionTypeChanged)
    Q_PROPERTY(BumpTransactionModel::RelayState relayState READ relayState NOTIFY resultChanged)

    explicit BumpTransactionModel(QObject* parent = nullptr);
    void setOpening(std::shared_ptr<walletopening::Session> opening, presentation::Post post,
                    walletbump::Services bump_services = {});
    void close() override;
    void setDisplayUnit(int unit) override;
    void retryPublications() override;
    State state() const override { return m_state; }
    bool busy() const override { return m_preparing || m_committing || m_rechecking; }
    bool canRecheckOutcome() const override;
    bool committed() const override { return m_commit_id != 0; }
    quint64 attemptId() const { return m_attempt_id; }
    quint64 commitId() const { return m_commit_id; }
    ActionType actionType() const { return SpeedUp; }
    QString oldFee() const override { return m_old_fee; }
    QString newFee() const override { return m_new_fee; }
    QString feeIncrease() const override { return m_increase; }
    QString oldTxid() const override { return m_old_txid; }
    QString newTxid() const override { return m_new_txid; }
    WalletRecordState walletRecordState() const override;
    WalletSubmissionState walletSubmissionState() const override;
    RelayState relayState() const;
    QString errorText() const override { return m_error; }
    QString unlockErrorText() const override { return m_unlock_error; }
    bool needsUnlock() const override { return m_needs_unlock; }
    std::optional<walletbump::Commit> commitResult() const;
    std::shared_ptr<const execution::OwnedCommand<walletbump::Preparation>> preparationOperation() const { return m_prepare_operation; }
    std::shared_ptr<const execution::OwnedCommand<walletbump::Commit>> commitOperation() const { return m_commit_operation; }
    std::shared_ptr<const execution::OwnedCommand<walletbump::Commit>> outcomeObservation() const { return m_observation_operation; }
    quint64 requestReviewFeeBump(const QString& txid, unsigned int target_blocks);
    quint64 requestRecheckFeeBumpOutcome();
    quint64 requestPrepareFeeBump(const QString& txid, unsigned int target_blocks);
    quint64 requestConfirmFeeBump();
    quint64 requestConfirmFeeBumpWithPassphrase(const QString& passphrase);
    bool review(const QString& txid, unsigned target_blocks) override { return requestReviewFeeBump(txid, target_blocks) != 0; }
    bool prepareAgain(const QString& txid, unsigned target_blocks) override { return requestPrepareFeeBump(txid, target_blocks) != 0; }
    bool confirm() override { return requestConfirmFeeBump() != 0; }
    bool confirmWithPassphrase(const QString& passphrase) override { return requestConfirmFeeBumpWithPassphrase(passphrase) != 0; }
    bool recheck() override { return requestRecheckFeeBumpOutcome() != 0; }
    void discard() override { reset(); }
    // Compatibility methods return admission, never completion.
    void prepareFeeBump(const QString& txid, unsigned int target_blocks) { requestPrepareFeeBump(txid, target_blocks); }
    void reset();
    void setSecurityStateChangedFn(std::function<void()> fn);

Q_SIGNALS:
    void actionTypeChanged();

private:
    quint64 confirm(std::optional<SecureString> passphrase, secretinput::Captured captured = {});
    void publishPreparation(quint64 attempt, quint64 revision);
    void publishCommit(quint64 attempt, quint64 commit);
    void publishObservation(quint64 attempt, quint64 commit, quint64 observation);
    void applyAttempt(transactionattempt::Snapshot snapshot, quint64 attempt, quint64 revision, bool preparation);
    transactionattempt::CurrentFacts attemptFacts() const;
    static State ProjectState(transactionattempt::Phase phase);
    bool publish();
    void clearPresentation();
    void refreshFeePresentation();
    std::shared_ptr<walletopening::Session> m_opening;
    presentation::Post m_post;
    walletbump::Services m_bump_services;
    std::shared_ptr<transactionattempt::Store> m_attempt_owner;
    transactionattempt::Snapshot m_attempt_snapshot;
    transactionattempt::Ticket m_attempt_ticket{};
    std::shared_ptr<const walletbump::Prepared> m_review;
    std::shared_ptr<execution::OwnedCommand<walletbump::Preparation>> m_prepare_operation;
    std::shared_ptr<execution::OwnedCommand<walletbump::Commit>> m_commit_operation;
    std::shared_ptr<execution::OwnedCommand<walletbump::Commit>> m_observation_operation;
    std::optional<walletbump::Commit> m_observed_outcome;
    std::function<void()> m_security_state_changed;
    State m_state{Idle};
    int m_display_unit{0};
    QString m_old_fee, m_new_fee, m_increase, m_old_txid, m_new_txid, m_error, m_unlock_error;
    bool m_needs_unlock{false}, m_closed{false}, m_preparing{false}, m_committing{false};
    bool m_rechecking{false};
    quint64 m_observation_id{0};
    quint64 m_attempt_id{0}, m_commit_id{0}, m_view_revision{0}, m_preparation_revision{0};
};

#endif // BITCOIN_QML_MODELS_BUMPTRANSACTIONMODEL_H
