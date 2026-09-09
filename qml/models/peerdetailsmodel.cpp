// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/peerdetailsmodel.h>

PeerDetailsModel::PeerDetailsModel(const CNodeCombinedStats* nodeStats, PeerListModel* parent)
: QObject(parent)
, m_node_id{static_cast<int>(nodeStats->nodeStats.nodeid)}
, m_addr{nodeStats->nodeStats.addr}
, m_combinedStats{nodeStats}
, m_model{parent}
{
    for (int row = 0; row < m_model->rowCount(); ++row) {
        QModelIndex index = m_model->index(row, 0);
        int nodeIdInRow = m_model->data(index, PeerListModel::NetNodeId).toInt();
        if (nodeIdInRow == m_node_id) {
            m_row = row;
            break;
        }
    }
    connect(parent, &PeerListModel::rowsRemoved, this, &PeerDetailsModel::onModelRowsRemoved);
    connect(parent, &PeerListModel::dataChanged, this, &PeerDetailsModel::onModelDataChanged);
    connect(parent, &PeerListModel::modelReset, this, &PeerDetailsModel::onModelReset);
}

void PeerDetailsModel::onModelRowsRemoved(const QModelIndex&  parent, int first, int last)
{
    for (int row = first; row <= last; ++row) {
        QModelIndex index = m_model->index(row, 0, parent);
        int nodeIdInRow = m_model->data(index, PeerListModel::NetNodeId).toInt();
        if (nodeIdInRow == this->nodeId()) {
            if (!m_disconnected) {
                m_disconnected = true;
                Q_EMIT disconnected();
            }
            break;
        }
    }
}

void PeerDetailsModel::onModelDataChanged(const QModelIndex& /* top_left */, const QModelIndex& /* bottom_right */)
{
    if (m_model->data(m_model->index(m_row, 0), PeerListModel::NetNodeId).isNull() ||
        m_model->data(m_model->index(m_row, 0), PeerListModel::NetNodeId).toInt() != nodeId()) {
        if (!m_disconnected) {
            m_disconnected = true;
            Q_EMIT disconnected();
        }
        return;
    }

    m_combinedStats = m_model->data(m_model->index(m_row, 0), PeerListModel::StatsRole).value<const CNodeCombinedStats*>();

    // Only update when all information is available
    if (m_combinedStats && m_combinedStats->fNodeStateStatsAvailable) {
        Q_EMIT dataChanged();
    }
}

void PeerDetailsModel::onModelReset()
{
    if (m_disconnected) return;

    const int tracked_node_id = m_node_id;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QModelIndex index = m_model->index(row, 0);
        const int node_id_in_row = m_model->data(index, PeerListModel::NetNodeId).toInt();
        if (node_id_in_row == tracked_node_id) {
            m_row = row;
            m_combinedStats = m_model->data(index, PeerListModel::StatsRole).value<const CNodeCombinedStats*>();
            if (m_combinedStats && m_combinedStats->fNodeStateStatsAvailable) {
                Q_EMIT dataChanged();
            }
            return;
        }
    }

    m_disconnected = true;
    Q_EMIT disconnected();
}
