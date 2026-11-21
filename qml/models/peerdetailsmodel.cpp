// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/peerdetailsmodel.h>
#include <qml/models/peerlistmodel.h>
PeerDetailsModel::PeerDetailsModel(PeerStatsHandle stats, PeerListModel* model) : QObject(model), m_model(model)
{
    if (!stats || !model) return;
    m_node_id = stats->nodeStats.nodeid;
    if (m_node_id < 0) return;
    const auto row = model->rowForKey(QString::number(m_node_id));
    if (!row) { m_disconnected = true; return; }
    const auto* record = static_cast<const PeerRecord*>(row->payload.get());
    if (!SamePeerConnection(stats->nodeStats, record->raw.nodeStats)) return;
    m_cache = record->presentation; m_connection = record->connection; m_disconnected = false;
    model->watchDetails(m_node_id, this);
    connect(model, &PeerListModel::commandsChanged, this, &PeerDetailsModel::dataChanged);
}
void PeerDetailsModel::update(const PeerPresentation& value, bool available) {
    if (m_disconnected) return;
    const auto row = m_model ? m_model->rowForKey(QString::number(m_node_id)) : NativeRowHandle{};
    const auto* record = row ? static_cast<const PeerRecord*>(row->payload.get()) : nullptr;
    if (!record || !SamePeerConnection(m_connection, record->connection)) { markDisconnected(); return; }
    // These bounded Qt presentation values own their text independently of
    // the Native row. A retained view cannot hold the Native cleanup lease.
    // Unavailable optional node-state statistics are not a disconnection.
    m_cache = value;
    if (m_connection.session.isEmpty()) m_connection.session = record->connection.session;
    Q_UNUSED(available);
    Q_EMIT dataChanged();
}
PeerListModel* PeerDetailsModel::source() const { return m_model; }
bool PeerDetailsModel::connected() const { return !m_disconnected && m_model && m_model->readingAvailable(); }
bool PeerDetailsModel::canDisconnect() const { const auto* port = m_model ? m_model->commands() : nullptr; return connected() && port && port->canStart(); }
bool PeerDetailsModel::canBan() const { return canDisconnect() && m_connection.ban_supported; }
PeerCommandOperation* PeerDetailsModel::prepareDisconnect()
{
    auto* port = m_model ? m_model->commands() : nullptr;
    return connected() && port ? port->prepare(m_connection, peercommands::Kind::Disconnect, 0) : nullptr;
}
PeerCommandOperation* PeerDetailsModel::prepareBan(PeerCommandOperation::BanDuration duration)
{
    auto* port = m_model ? m_model->commands() : nullptr;
    return connected() && port ? port->prepare(m_connection, peercommands::Kind::Ban, static_cast<int64_t>(duration)) : nullptr;
}
void PeerDetailsModel::markDisconnected() {
    if (m_disconnected) return;
    m_disconnected = true;
    const QPointer<PeerDetailsModel> guard{this};
    Q_EMIT dataChanged();
    if (guard) Q_EMIT disconnected();
}
