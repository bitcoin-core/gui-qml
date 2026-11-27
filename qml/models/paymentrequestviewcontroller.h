// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PAYMENTREQUESTVIEWCONTROLLER_H
#define BITCOIN_QML_MODELS_PAYMENTREQUESTVIEWCONTROLLER_H

#include <qml/models/paymentrequesteditormodel.h>
#include <qml/models/walletreceivemodel.h>

#include <QObject>
#include <QPointer>
#include <QString>

/** Opens one saved payment request for a request view. The view declares it
 * and binds to the opened request. Opening is asynchronous: another wallet or
 * the view going inactive cancels it, and only the request it asked for
 * opens. The opened request stays bound, with its wallet's receive model,
 * until the view is done with it. */
class PaymentRequestViewController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(WalletReceiveModel* receive READ receive WRITE setReceive NOTIFY receiveChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool opening READ opening NOTIFY changed)
    //! The opened request and the receive model it belongs to.
    Q_PROPERTY(PaymentRequestEditorModel* editor READ editor NOTIFY changed)
    Q_PROPERTY(WalletReceiveModel* editorReceive READ editorReceive NOTIFY changed)
    //! Why the latest request could not be opened.
    Q_PROPERTY(QString failure READ failure NOTIFY changed)

public:
    explicit PaymentRequestViewController(QObject* parent = nullptr);
    ~PaymentRequestViewController() override;

    WalletReceiveModel* receive() const { return m_receive; }
    void setReceive(WalletReceiveModel* receive);
    bool active() const { return m_active; }
    void setActive(bool active);
    bool opening() const { return m_open && m_open->pending(); }
    PaymentRequestEditorModel* editor() const;
    WalletReceiveModel* editorReceive() const { return editor() ? m_editor_receive.data() : nullptr; }
    QString failure() const { return m_failure; }

    //! Starts opening request \p id. False when it can't start; failure says why.
    Q_INVOKABLE bool open(const QString& id);
    Q_INVOKABLE void cancel();
    //! The view closed: it no longer shows the opened request.
    Q_INVOKABLE void release();
    Q_INVOKABLE void dismissFailure();

Q_SIGNALS:
    void receiveChanged();
    void activeChanged();
    void changed();

private:
    //! Takes the result of the open this view started.
    void openChanged();

    QPointer<WalletReceiveModel> m_receive;
    bool m_active{true};
    //! The open this view started; it owns it.
    QPointer<PaymentRequestOpenOperation> m_open;
    QPointer<PaymentRequestEditorModel> m_editor;
    QPointer<WalletReceiveModel> m_editor_receive;
    QString m_failure;
};

#endif // BITCOIN_QML_MODELS_PAYMENTREQUESTVIEWCONTROLLER_H
