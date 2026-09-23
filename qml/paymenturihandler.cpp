// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/paymenturihandler.h>

PaymentUriHandler::PaymentUriHandler(QObject* parent)
    : QObject(parent)
{
}

void PaymentUriHandler::queueRequest(const QString& uri)
{
    if (uri.isEmpty()) {
        return;
    }
    const bool was_empty{m_requests.isEmpty()};
    m_requests.enqueue(uri);
    if (was_empty) {
        Q_EMIT pendingRequestChanged();
    }
}

void PaymentUriHandler::queueRequests(const QStringList& uris)
{
    for (const QString& uri : uris) {
        queueRequest(uri);
    }
}

QString PaymentUriHandler::pendingRequest() const
{
    return m_requests.isEmpty() ? QString() : m_requests.head();
}

void PaymentUriHandler::completePendingRequest()
{
    if (m_requests.isEmpty()) {
        return;
    }
    m_requests.dequeue();
    Q_EMIT pendingRequestChanged();
}
