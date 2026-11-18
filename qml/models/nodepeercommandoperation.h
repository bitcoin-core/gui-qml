// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_NODEPEERCOMMANDOPERATION_H
#define BITCOIN_QML_MODELS_NODEPEERCOMMANDOPERATION_H

#include <qml/guipresentation.h>
#include <qml/models/peercommandoperation.h>
#include <qml/peercommands.h>

#include <functional>
#include <memory>
#include <optional>

//! The QML result of a peer command store result.
PeerCommandResult ProjectPeerCommandResult(const peercommands::Result& value);

/** A peer command against the node's peer command store.
 * Destruction cannot cancel or erase an admitted Native outcome. */
class NodePeerCommandOperation final : public PeerCommandOperation {
    Q_OBJECT
public:
    NodePeerCommandOperation(std::shared_ptr<peercommands::Store> store, peercommands::Request request,
                             presentation::Post post, std::function<bool()> ready,
                             std::function<void()> admitted, std::function<bool()> interested = {}, QObject* parent = nullptr);
    QString operationId() const override { return m_operation->request.id; }
    QString connectionId() const override { return m_operation->request.target.key; }
    QString nodeIdText() const override { return QString::number(m_operation->request.target.node_id); }
    QString address() const override { return m_operation->request.target.display_address; }
    int banDuration() const override { return static_cast<int>(m_operation->request.duration); }
    bool confirmationRequired() const override { return !m_confirmed && !m_retired; }
    bool pending() const override { return m_pending; }
    bool queryPending() const override { return m_query_pending; }
    bool resultPresent() const override { return m_result.has_value(); }
    bool outcomeKnown() const override { return m_result && m_result->outcome_known; }
    QString errorText() const override { return m_result ? m_result->error : QString{}; }
    QString queryError() const override { return m_result ? m_result->query_error : QString{}; }
    PeerCommandResult result() const override;
    bool confirm() override;
    bool cancel() override;
    bool reconcile() override;
    void refresh() override;
    void retire() override;
private:
    bool interested() const { return !m_retired && (!m_interested || m_interested()); }
    std::shared_ptr<peercommands::Store> m_store;
    std::shared_ptr<peercommands::Operation> m_operation;
    const std::function<bool()> m_ready;
    const std::function<void()> m_admitted;
    const std::function<bool()> m_interested;
    std::optional<peercommands::Result> m_result;
    bool m_confirmed{false}, m_retired{false}, m_pending{false}, m_query_pending{false};
};
#endif // BITCOIN_QML_MODELS_NODEPEERCOMMANDOPERATION_H
