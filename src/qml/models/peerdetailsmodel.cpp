// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/peerdetailsmodel.h>
#include <cassert>

PeerDetailsModel::PeerDetailsModel(const CNodeCombinedStats* stats, PeerListModel* model)
    : QObject{model}, m_node_id{stats ? stats->nodeStats.nodeid : -1},
      m_addr{stats ? static_cast<const CNetAddr&>(stats->nodeStats.addr) : CNetAddr{}},
      m_combinedStats{stats ? *stats : CNodeCombinedStats{}}, m_model{model},
      m_disconnected{!stats}
{
    assert(model);
    connect(model, &PeerListModel::rowsRemoved, this, &PeerDetailsModel::onModelRowsRemoved);
    connect(model, &PeerListModel::dataChanged, this, &PeerDetailsModel::onModelDataChanged);
    connect(model, &PeerListModel::modelReset, this, &PeerDetailsModel::onModelReset);
}

void PeerDetailsModel::onModelRowsRemoved(const QModelIndex&, int, int)
{
    onModelReset();
}

void PeerDetailsModel::onModelDataChanged(const QModelIndex&, const QModelIndex&)
{
    onModelReset();
}

void PeerDetailsModel::onModelReset()
{
    if (m_disconnected) return;
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QModelIndex index{m_model->index(row, 0)};
        if (m_model->data(index, PeerListModel::NetNodeId).toLongLong() != m_node_id) continue;
        const auto* stats{m_model->data(index, PeerListModel::StatsRole).value<const CNodeCombinedStats*>()};
        if (stats) {
            m_combinedStats = *stats;
            Q_EMIT dataChanged();
        }
        return;
    }
    // Retain the last safe value snapshot for navigation history.
    m_disconnected = true;
    Q_EMIT disconnected();
}
