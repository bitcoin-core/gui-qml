// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_TRANSACTIONREVIEWCONTROLLER_H
#define BITCOIN_QML_MODELS_TRANSACTIONREVIEWCONTROLLER_H

#include <qml/models/sendoperation.h>
#include <qml/models/transactionreviewinterface.h>

#include <QObject>
#include <QPointer>
#include <QString>

/** The review page's actions on the transaction under review. The page
 * declares it, binds to its state and calls its actions. It owns the send it
 * starts: a newer send, another review, another wallet or the page hiding
 * cancels it. */
class TransactionReviewController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(TransactionReviewInterface* reviewInterface READ reviewInterface WRITE setReviewInterface NOTIFY reviewInterfaceChanged)
    //! False while the page is hidden; it cancels the send's prompts.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(SendOperation::State sendState READ sendState NOTIFY sendChanged)
    Q_PROPERTY(Prompt prompt READ prompt NOTIFY sendChanged)
    Q_PROPERTY(bool sending READ sending NOTIFY sendChanged)
    //! The review can be closed: false while a send is submitting with no
    //! prompt open, so the user sees the result they started.
    Q_PROPERTY(bool canDismiss READ canDismiss NOTIFY sendChanged)
    Q_PROPERTY(bool signOnly READ signOnly NOTIFY sendChanged)
    Q_PROPERTY(QString sendError READ sendError NOTIFY sendChanged)
    Q_PROPERTY(QString txid READ txid NOTIFY sendChanged)
    //! WalletQmlModel::SendSubmission and SendRecord values.
    Q_PROPERTY(int submission READ submission NOTIFY sendChanged)
    Q_PROPERTY(int record READ record NOTIFY sendChanged)
    //! The latest PSBT action on this page: save, broadcast or recheck.
    Q_PROPERTY(PsbtOperation::Kind psbtKind READ psbtKind NOTIFY psbtChanged)
    Q_PROPERTY(PsbtOperation::State psbtState READ psbtState NOTIFY psbtChanged)
    Q_PROPERTY(bool psbtBusy READ psbtBusy NOTIFY psbtChanged)
    Q_PROPERTY(QString psbtError READ psbtError NOTIFY psbtChanged)
    Q_PROPERTY(QString psbtTxid READ psbtTxid NOTIFY psbtChanged)
    //! The review has an unverified signer result that can be saved.
    Q_PROPERTY(bool signerResultAvailable READ signerResultAvailable NOTIFY signerResultChanged)
    //! A save is waiting for the user to choose its file.
    Q_PROPERTY(bool savePending READ savePending NOTIFY psbtChanged)
    Q_PROPERTY(bool saveSignerResult READ saveSignerResult NOTIFY psbtChanged)
    Q_PROPERTY(int signerApprovalState READ signerApprovalState NOTIFY reviewDetailsChanged)
    Q_PROPERTY(QString signerApprovalError READ signerApprovalError NOTIFY reviewDetailsChanged)
    Q_PROPERTY(bool signerNotFound READ signerNotFound NOTIFY reviewDetailsChanged)
    Q_PROPERTY(FileSaveOperation* fileExportOperation READ fileExportOperation NOTIFY reviewDetailsChanged)
    Q_PROPERTY(QString reviewError READ reviewError NOTIFY reviewDetailsChanged)

public:
    enum class Prompt { None, SweepConsent, Password };
    Q_ENUM(Prompt)

    explicit TransactionReviewController(QObject* parent = nullptr);
    ~TransactionReviewController() override;

    TransactionReviewInterface* reviewInterface() const { return m_interface; }
    void setReviewInterface(TransactionReviewInterface* review_interface);
    bool active() const { return m_active; }
    void setActive(bool active);

    SendOperation::State sendState() const { return m_send ? m_send->state() : SendOperation::State::None; }
    Prompt prompt() const;
    bool sending() const { return m_send && m_send->pending(); }
    bool canDismiss() const { return !m_send || m_send->state() != SendOperation::State::Pending || prompt() != Prompt::None; }
    bool signOnly() const { return m_send && m_send->signOnly(); }
    QString sendError() const { return m_send ? m_send->error() : QString{}; }
    QString txid() const { return m_send ? m_send->txid() : QString{}; }
    int submission() const { return m_send ? m_send->submission() : 0; }
    int record() const { return m_send ? m_send->record() : 0; }

    PsbtOperation::Kind psbtKind() const { return m_psbt ? m_psbt->kind() : m_psbt_kind; }
    PsbtOperation::State psbtState() const;
    bool psbtBusy() const { return m_psbt && m_psbt->pending(); }
    QString psbtError() const;
    QString psbtTxid() const { return m_psbt ? m_psbt->txid() : QString{}; }
    bool savePending() const { return m_save_pending; }
    bool saveSignerResult() const { return m_save_pending && !m_save_signer_result.isEmpty(); }
    bool signerResultAvailable() const { return m_interface && !m_interface->unverifiedSignerResult().isEmpty(); }

    int signerApprovalState() const { return m_interface ? m_interface->signerApprovalState() : 0; }
    QString signerApprovalError() const { return m_interface ? m_interface->signerApprovalError() : QString{}; }
    bool signerNotFound() const { return m_interface && m_interface->signerNotFound(); }
    FileSaveOperation* fileExportOperation() const { return m_interface ? m_interface->fileExportOperation() : nullptr; }
    QString reviewError() const { return m_interface ? m_interface->reviewError() : QString{}; }

    //! Sends, or only signs, the current review. Ignored while a send runs.
    Q_INVOKABLE void send();
    Q_INVOKABLE void approveSigner();
    Q_INVOKABLE void signOnlyTransaction();
    //! Answers the current send's prompts.
    Q_INVOKABLE void confirm();
    Q_INVOKABLE void unlock(const QString& passphrase);
    Q_INVOKABLE void cancel();

    //! Saving asks for a file first: begin, then choose or cancel. With
    //! \p signerResult it exports the review's unverified signer result.
    Q_INVOKABLE void beginPsbtSave(bool signerResult = false);
    Q_INVOKABLE void choosePsbtSaveFile(const QString& path);
    Q_INVOKABLE void cancelPsbtSave();
    Q_INVOKABLE void broadcast();
    Q_INVOKABLE void recheck();
    //! Clears a finished PSBT action's result once the page showed it.
    Q_INVOKABLE void dismissPsbtResult();

Q_SIGNALS:
    void reviewInterfaceChanged();
    void activeChanged();
    void sendChanged();
    void psbtChanged();
    void signerResultChanged();
    void reviewDetailsChanged();

private:
    void start(bool sign_only);
    //! Answers an open prompt with no and keeps the operation's final state.
    void cancelPrompt();
    void reviewChanged();
    void startPsbt(PsbtOperation::Kind kind, const QString& path = {}, const QString& signer_result = {});
    void dropPsbt();

    QPointer<TransactionReviewInterface> m_interface;
    bool m_active{true};
    QPointer<SendOperation> m_send;
    //! The review m_send was started for.
    QPointer<QObject> m_send_review;
    QPointer<PsbtOperation> m_psbt;
    //! The review m_psbt was started for.
    QPointer<QObject> m_psbt_review;
    PsbtOperation::Kind m_psbt_kind{PsbtOperation::Kind::Save};
    //! The wallet refused the latest PSBT action because another one runs.
    bool m_psbt_refused{false};
    bool m_save_pending{false};
    QString m_save_signer_result;
    QPointer<QObject> m_save_review;
};

#endif // BITCOIN_QML_MODELS_TRANSACTIONREVIEWCONTROLLER_H
