// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PAYMENTREQUESTDELETECONTROLLER_H
#define BITCOIN_QML_MODELS_PAYMENTREQUESTDELETECONTROLLER_H

#include <qml/models/paymentrequesteditormodel.h>
#include <qml/models/walletreceivemodel.h>

#include <QObject>
#include <QPointer>
#include <QString>

/** Deleting a saved payment request: the question that confirms it, the
 * delete and its outcome. A view that offers "Delete payment request"
 * declares one; each owns its own question. */
class PaymentRequestDeleteController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(WalletReceiveModel* receive READ receive WRITE setReceive NOTIFY receiveChanged)
    //! The delete question and its outcome, while there is one.
    Q_PROPERTY(PaymentRequestEditorModel* deletion READ deletion NOTIFY changed)
    //! The question waits for an answer.
    Q_PROPERTY(bool asking READ asking NOTIFY changed)
    //! The delete was refused or failed; error says why.
    Q_PROPERTY(bool failed READ failed NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit PaymentRequestDeleteController(QObject* parent = nullptr);
    ~PaymentRequestDeleteController() override;

    WalletReceiveModel* receive() const { return m_receive; }
    void setReceive(WalletReceiveModel* receive);
    PaymentRequestEditorModel* deletion() const { return m_deletion; }
    bool asking() const;
    bool failed() const;
    QString error() const;

    //! Asks whether to delete saved request \p id.
    Q_INVOKABLE bool request(const QString& id);
    //! Deletes the request the question is about.
    Q_INVOKABLE void confirm();
    //! Keeps the request.
    Q_INVOKABLE void cancel();
    //! Forgets a failed delete.
    Q_INVOKABLE void dismiss();
    //! Drops the question and a failed outcome: the view now shows something else.
    Q_INVOKABLE void reset();

Q_SIGNALS:
    void receiveChanged();
    void changed();
    //! Saved request \p id was deleted.
    void deleted(const QString& id);

private:
    void release();
    bool refuse(QString reason);

    QPointer<WalletReceiveModel> m_receive;
    //! Why request() asked nothing; shown like a failed delete.
    QString m_refusal;
    QPointer<PaymentRequestEditorModel> m_deletion;
};

#endif // BITCOIN_QML_MODELS_PAYMENTREQUESTDELETECONTROLLER_H
