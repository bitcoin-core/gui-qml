// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/receiverequestoperation.h>

#include <QPointer>

void ReceiveRequestOperation::start(const std::shared_ptr<walletrequests::Store>& store,
                                   walletrequests::Command command, const std::shared_ptr<QObject>& bridge)
{
    if (m_started) return;
    m_started = true;
    const QPointer<ReceiveRequestOperation> observer{this};
    auto notify = [observer, bridge] {
        QMetaObject::invokeMethod(bridge.get(), [observer] { if (observer) observer->consume(); }, Qt::QueuedConnection);
    };
    walletrequests::Admission admission;
    if (store && bridge) admission = store->submit(std::move(command), notify);
    else admission.rejection = execution::Rejection::Closed;
    m_operation = std::move(admission.operation);
    if (m_operation) m_address_intent = m_operation->address_intent;
    if (!m_operation) {
        m_result.emplace();
        m_result->error = admission.rejection == execution::Rejection::Closed
            ? walletrequests::Error::Closed : walletrequests::Error::Rejected;
        // Even an immediate rejection is delivered after the caller has bound
        // its view to this particular operation.
        if (bridge) notify();
    }
}

void ReceiveRequestOperation::consume()
{
    if (m_operation) {
        auto result = m_operation->result();
        if (!result) return;
        m_result = std::move(result);
        m_operation.reset();
    }
    if (m_result) Q_EMIT changed();
}

QString ReceiveRequestOperation::errorText() const
{
    return ErrorText(m_result ? m_result->error : walletrequests::Error::None, requestSaved());
}

QString ReceiveRequestOperation::ErrorText(walletrequests::Error error, bool request_saved)
{
    return walletrequests::ErrorText(error, request_saved);
}
