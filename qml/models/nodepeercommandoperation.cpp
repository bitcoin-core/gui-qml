// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/nodepeercommandoperation.h>
#include <QPointer>

PeerCommandResult ProjectPeerCommandResult(const peercommands::Result& value)
{
    return {value.request.id, value.request.target.key, QString::number(value.request.target.node_id),
        value.request.target.display_address, value.error, value.query_error,
        static_cast<PeerCommandOperation::Outcome>(value.outcome), value.present, value.outcome_known,
        value.mutation_attempted, value.ban_applied, value.disconnect_confirmed,
        value.observation_available, value.target_connected, value.ban_observation_available, value.target_banned};
}
NodePeerCommandOperation::NodePeerCommandOperation(std::shared_ptr<peercommands::Store> store, peercommands::Request request,
    presentation::Post post, std::function<bool()> ready, std::function<void()> admitted, std::function<bool()> interested, QObject* parent)
    : PeerCommandOperation(parent), m_store(std::move(store)), m_ready(std::move(ready)), m_admitted(std::move(admitted)), m_interested(std::move(interested))
{
    const QPointer<NodePeerCommandOperation> observer{this};
    m_operation = std::make_shared<peercommands::Operation>(std::move(request), [post = std::move(post), observer] {
        if (post) post([observer] { if (observer) observer->refresh(); });
    });
}
PeerCommandResult NodePeerCommandOperation::result() const { return m_result ? ProjectPeerCommandResult(*m_result) : PeerCommandResult{}; }
bool NodePeerCommandOperation::confirm()
{
    if (m_confirmed || m_retired) return false;
    const auto store = m_store;
    const auto original = m_operation;
    const auto ready = m_ready;
    const auto admitted = m_admitted;
    const QPointer<NodePeerCommandOperation> guard{this};
    const bool available = ready && ready();
    if (!guard) return false;
    m_confirmed = true;
    if (!store || !available) {
        if (store) {
            store->refuse(original, tr("The node is busy or closing. The peer command did not run."));
            refresh();
        } else {
            m_result.emplace();
            m_result->request = original->request;
            m_result->present = true;
            m_result->error = tr("The peer command is no longer available.");
            if (interested()) Q_EMIT changed();
        }
        return false;
    }
    const auto submitted = store->submit(original);
    if (guard) m_pending = bool(submitted);
    // Immediate GUI intent publication is separate from the queued Native
    // terminal receipt, even when the worker finishes before confirm returns.
    if (submitted && admitted) admitted();
    if (guard) {
        if (submitted) {
            if (interested()) Q_EMIT changed();
        } else {
            refresh();
        }
    }
    return bool(submitted);
}
bool NodePeerCommandOperation::cancel()
{
    if (m_retired || m_confirmed || !m_store || !m_store->cancel(m_operation)) return false;
    m_confirmed = true;
    refresh();
    return true;
}
bool NodePeerCommandOperation::reconcile()
{
    if (m_retired || m_pending || m_query_pending || !m_result || m_result->outcome_known || !m_store) return false;
    const auto store = m_store;
    const auto original = m_operation;
    const auto ready = m_ready;
    const auto admitted = m_admitted;
    const QPointer<NodePeerCommandOperation> guard{this};
    const bool available = ready && ready();
    if (!guard) return false;
    if (!available) {
        store->refuseQuery(original, tr("The node is busy or closing. Peer status was not refreshed."));
        refresh();
        return false;
    }
    const auto submitted = store->reconcile(original);
    if (guard) {
        m_query_pending = bool(submitted);
        m_pending = bool(submitted);
    }
    if (submitted && admitted) admitted();
    if (guard) {
        if (submitted) {
            if (interested()) Q_EMIT changed();
        } else {
            refresh();
        }
    }
    return bool(submitted);
}
void NodePeerCommandOperation::refresh()
{
    m_result = m_operation->result(); m_query_pending = m_operation->queryPending();
    m_pending = m_query_pending || (m_confirmed && !m_result);
    if (interested()) Q_EMIT changed();
}
void NodePeerCommandOperation::retire()
{
    if (m_retired) return;
    if (!m_confirmed && m_store) { m_store->cancel(m_operation); m_confirmed = true; m_result = m_operation->result(); }
    // Retirement ends this projection's interest. It is not a new terminal
    // event and cannot reopen a prior error while a successor is attached.
    m_retired = true;
}
