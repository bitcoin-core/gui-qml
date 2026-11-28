// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PAYMENTURIHANDLER_H
#define BITCOIN_QML_PAYMENTURIHANDLER_H

#include <QObject>
#include <QQueue>
#include <QString>
#include <QStringList>

/**
 * Queue of incoming bitcoin: payment requests waiting to reach the Send flow.
 *
 * Requests are read during init, before the shell, the wallet and the Send page
 * exist. They are held as raw strings because BitcoinUri::Parse validates the
 * address against a chain that SelectParams has not picked yet.
 */
class PaymentUriHandler : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasPendingRequest READ hasPendingRequest NOTIFY pendingRequestChanged)

public:
    explicit PaymentUriHandler(QObject* parent = nullptr);

    /** Append a request. Safe to call before the QML engine exists. */
    void queueRequest(const QString& uri);
    void queueRequests(const QStringList& uris);

    bool hasPendingRequest() const { return !m_requests.isEmpty(); }

    /** The request at the front of the queue, or an empty string. */
    Q_INVOKABLE QString pendingRequest() const;

    /** Drop the front request, once the UI has applied or reported it. */
    Q_INVOKABLE void completePendingRequest();

Q_SIGNALS:
    void pendingRequestChanged();

private:
    QQueue<QString> m_requests;
};

#endif // BITCOIN_QML_PAYMENTURIHANDLER_H
