// Copyright (c) 2011-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_TRANSACTIONREVIEWMODEL_H
#define BITCOIN_QML_MODELS_TRANSACTIONREVIEWMODEL_H

#include <qml/bitcoinamount.h>
#include <qml/models/psbtfee.h>
#include <qml/walletreview.h>
#include <qml/walletopening.h>

#include <consensus/amount.h>
#include <primitives/transaction.h>

#include <QMap>
#include <QPointer>
#include <QVariantList>

#include <optional>
#include <vector>

class TransactionReviewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(BitcoinAmount* amountAmount READ amountAmount CONSTANT)
    Q_PROPERTY(BitcoinAmount* feeAmount READ feeAmount CONSTANT)
    Q_PROPERTY(BitcoinAmount* totalAmount READ totalAmount CONSTANT)
    Q_PROPERTY(QString amount READ amount NOTIFY amountChanged)
    Q_PROPERTY(QString fee READ fee NOTIFY feeChanged)
    Q_PROPERTY(bool feeKnown READ feeKnown NOTIFY feeChanged)
    Q_PROPERTY(QString nodePolicyStatus READ nodePolicyStatus NOTIFY nodePolicyChanged)
    Q_PROPERTY(QString total READ total NOTIFY totalChanged)
    Q_PROPERTY(QString label READ label CONSTANT)
    Q_PROPERTY(QString txid READ txid NOTIFY txidChanged)
    Q_PROPERTY(bool presentationPending READ presentationPending NOTIFY presentationChanged)
    Q_PROPERTY(QString presentationError READ presentationError NOTIFY presentationChanged)
    Q_PROPERTY(QVariantList reviewedRecipients READ reviewedRecipients NOTIFY reviewedRecipientsChanged)
    Q_PROPERTY(int reviewTargetBlocks READ reviewTargetBlocks CONSTANT)
    Q_PROPERTY(QString reviewFeeRate READ reviewFeeRate CONSTANT)
    Q_PROPERTY(int reviewFeeSource READ reviewFeeSource CONSTANT)
    Q_PROPERTY(bool reviewUsesFallback READ reviewUsesFallback CONSTANT)
public:
    //! Where the reviewed transaction came from.
    enum class Source { None, SendDraft, ImportedPsbt };

    TransactionReviewModel(std::shared_ptr<const walletreview::Snapshot> snapshot,
        std::shared_ptr<walletopening::Session> opening, std::function<void(std::function<void()>)> post,
        QObject* parent = nullptr);
    TransactionReviewModel(const walletreview::Snapshot& snapshot,
        std::shared_ptr<walletopening::Session> opening, std::function<void(std::function<void()>)> post,
        QObject* parent = nullptr);

    TransactionReviewModel(const TransactionReviewModel& reviewed, CAmount authenticated_fee, QObject* parent);

    BitcoinAmount* amountAmount() const;
    BitcoinAmount* feeAmount() const;
    BitcoinAmount* totalAmount() const;
    QString amount() const;
    QString fee() const;
    QString total() const;
    QString label() const;
    QString txid() const;
    bool presentationPending() const { return m_presentation_pending; }
    QString presentationError() const { return m_presentation_error; }
    QVariantList reviewedRecipients() const;
    bool isReviewedRecipient(const QString& address) const;
    int reviewTargetBlocks() const { return m_review_target_blocks; }
    QString reviewFeeRate() const { return m_review_fee_rate; }
    int reviewFeeSource() const { return m_review_fee_source; }
    bool reviewUsesFallback() const { return m_review_fee_source == static_cast<int>(feeestimate::Provenance::Fallback); }
    const QMap<QString, QString>& recipientLabels() const { return m_recipient_labels; }
    const QSet<QString>& reviewedAddresses() const { return m_reviewed_addresses; }
    // What the wallet established about this transaction. Signing updates
    // them in place; a new preparation or import makes a new review.
    Source source() const { return m_source; }
    void setSource(Source source) { m_source = source; }
    std::shared_ptr<const PartiallySignedTransaction> psbt() const { return m_psbt; }
    void setPsbt(std::shared_ptr<const PartiallySignedTransaction> psbt) { m_psbt = std::move(psbt); }
    std::shared_ptr<const psbtfee::Analysis> feeProof() const { return m_fee_proof; }
    void setFeeProof(std::shared_ptr<const psbtfee::Analysis> proof) { m_fee_proof = std::move(proof); }
    bool sweepsWallet() const { return m_sweeps_wallet; }
    void setSweepsWallet(bool sweeps) { m_sweeps_wallet = sweeps; }
    bool canSend() const { return m_can_send; }
    void setCanSend(bool can_send) { m_can_send = can_send; }
    bool canBroadcast() const { return m_can_broadcast; }
    void setCanBroadcast(bool can_broadcast) { m_can_broadcast = can_broadcast; }
    //! A broadcast attempt may have changed what the node knows; check again first.
    bool requiresRecheck() const { return m_requires_recheck; }
    void setRequiresRecheck(bool requires_recheck) { m_requires_recheck = requires_recheck; }
    //! This wallet's keys can sign the imported PSBT.
    bool keyCanSend() const { return m_key_can_send; }
    void setKeyCanSend(bool key_can_send) { m_key_can_send = key_can_send; }
    //! The imported PSBT is complete and the wallet may commit it.
    bool canCommitComplete() const { return m_can_commit_complete; }
    void setCanCommitComplete(bool can_commit) { m_can_commit_complete = can_commit; }
    QString reviewMessage() const { return m_review_message; }
    void setReviewMessage(QString message) { m_review_message = std::move(message); }
    bool nodePolicyBlocked() const { return m_node_policy_blocked; }
    QString nodePolicyStatus() const { return m_node_policy_status; }
    void setNodePolicyStatus(QString status, bool blocked);
    std::shared_ptr<const void> retainedValues() const { return m_review_lifetime; }
    std::shared_ptr<const PartiallySignedTransaction> assessedPsbt() const { return m_assessed_psbt; }
    // The producer retains Native cleanup for this immutable analysis value.
    void setAssessedPsbt(std::shared_ptr<const PartiallySignedTransaction> assessment) { m_assessed_psbt = std::move(assessment); }

    CTransactionRef& getWtx();
    void setWtx(const CTransactionRef&);

    void setTransactionFee(const CAmount& newFee);
    CAmount getTransactionFee() const;
    bool feeKnown() const { return m_fee_known; }
    std::optional<CAmount> reviewedFee() const { return m_fee_known ? std::optional{m_fee} : std::nullopt; }

    CAmount getTotalTransactionAmount() const;

    void setDisplayUnit(int unit);
    void close();
    void setReviewFeeDetails(int target_blocks, const QString& fee_rate);


Q_SIGNALS:
    void amountChanged();
    void feeChanged();
    void totalChanged();
    void txidChanged();
    void nodePolicyChanged();
    void reviewedRecipientsChanged();
    void presentationChanged();

private:
    struct ConstructionInput {
        std::shared_ptr<const walletreview::Snapshot> snapshot;
        std::shared_ptr<walletopening::Session> opening;
        std::function<void(std::function<void()>)> post;
        QPointer<QObject> parent;
    };
    static ConstructionInput prepareConstruction(const walletreview::Snapshot&, std::shared_ptr<walletopening::Session>,
        std::function<void(std::function<void()>)>, QObject*);
    explicit TransactionReviewModel(ConstructionInput input);
    static QString formatWithUnit(CAmount value, int display_unit);

    // Declared before cached aliases so they release before the cleanup lease.
    std::shared_ptr<const walletreview::Snapshot> m_review_lifetime;
    std::shared_ptr<const PartiallySignedTransaction> m_assessed_psbt;
    Source m_source{Source::None};
    std::shared_ptr<const PartiallySignedTransaction> m_psbt;
    std::shared_ptr<const psbtfee::Analysis> m_fee_proof;
    bool m_sweeps_wallet{false};
    bool m_can_send{false};
    bool m_can_broadcast{false};
    bool m_requires_recheck{false};
    bool m_key_can_send{false};
    bool m_can_commit_complete{false};
    QString m_review_message;
    QString m_node_policy_status;
    bool m_node_policy_blocked{false};
    std::shared_ptr<const void> m_presentation_lifetime;
    bool m_presentation_pending{false};
    bool m_closed{false};
    QString m_presentation_error;

    QString m_address;
    QString m_txid;
    QString m_label;
    // Keep the reviewed notes even if the send form is edited or reset later.
    QMap<QString, QString> m_recipient_labels;
    using ReviewedRecipient = walletreview::Recipient;
    QList<ReviewedRecipient> m_reviewed_recipients;
    QVariantList m_reviewed_presentation;
    QSet<QString> m_reviewed_addresses;
    std::shared_ptr<walletopening::Session> m_opening;
    std::function<void(std::function<void()>)> m_post;
    quint64 m_presentation_revision{0};
    void refreshRecipientPresentation();
    int m_review_target_blocks{0};
    QString m_review_fee_rate;
    int m_review_fee_source{0};
    CAmount m_amount;
    CAmount m_fee{0};
    bool m_fee_known{false};
    BitcoinAmount* m_amount_amount;
    BitcoinAmount* m_fee_amount;
    BitcoinAmount* m_total_amount;
    CTransactionRef m_wtx;
    int m_display_unit{0};
    int m_requested_display_unit{0};
};

#endif // BITCOIN_QML_MODELS_TRANSACTIONREVIEWMODEL_H
