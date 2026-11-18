// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/peerselectionmodel.h>
#include <qml/models/peerlistmodel.h>
#include <limits>

void PeerSelectionModel::detach()
{
    for (const auto& connection : m_detail_connections) disconnect(connection);
    m_detail_connections.clear(); m_selected.clear();
}
void PeerSelectionModel::setSource(PeerListSortProxy* source)
{
    if (m_source == source || m_phase == Phase::Stopped || m_revision == std::numeric_limits<quint64>::max()) return;
    for (const auto& connection : m_source_connections) disconnect(connection);
    m_source_connections.clear(); detach(); m_target = {}; m_phase = Phase::NoSelection; m_source = source;
    if (source) {
        m_source_connections.push_back(connect(source, &NativeListModel::snapshotReady, this, &PeerSelectionModel::reconcile));
        m_source_connections.push_back(connect(source, &PeerListSortProxy::admissionClosed, this, &PeerSelectionModel::stop));
        m_source_connections.push_back(connect(source, &QObject::destroyed, this, &PeerSelectionModel::stop));
    }
    const auto revision = ++m_revision;
    const QPointer<PeerSelectionModel> guard{this};
    Q_EMIT sourceChanged(); if (!guard || m_revision != revision) return;
    Q_EMIT changed(); if (guard && m_revision == revision) reconcile();
}
void PeerSelectionModel::setAutoSelectFirst(bool value)
{
    if (m_auto == value || m_phase == Phase::Stopped) return;
    m_auto = value; const auto revision = m_revision; const QPointer<PeerSelectionModel> guard{this};
    Q_EMIT autoSelectFirstChanged(); if (guard && m_revision == revision) reconcile();
}
bool PeerSelectionModel::select(PeerDetailsModel* details)
{
    if (m_phase == Phase::Stopped || m_revision == std::numeric_limits<quint64>::max() || !m_source || !details || !details->connected() || details->source() != m_source->sourceModel()) return false;
    const auto row = m_source->indexOfNodeId(details->nodeId());
    if (row < 0 || m_source->peerDetailsAt(row) != details) return false;
    if (m_selected == details && m_phase == Phase::Connected) return true;
    detach(); m_selected = details; m_target = details->commandTarget(); m_phase = Phase::Connected; ++m_revision;
    m_detail_connections.push_back(connect(details, &PeerDetailsModel::dataChanged, this, [this, original = QPointer<PeerDetailsModel>{details}] {
        if (m_selected != original || !original || original->connected() || m_phase == Phase::Disconnected) return;
        m_phase = Phase::Disconnected; if (m_revision < std::numeric_limits<quint64>::max()) ++m_revision;
        Q_EMIT changed();
    }));
    m_detail_connections.push_back(connect(details, &QObject::destroyed, this, [this] {
        m_selected.clear(); if (m_phase != Phase::Stopped) m_phase = Phase::Disconnected;
        if (m_revision < std::numeric_limits<quint64>::max()) ++m_revision;
        Q_EMIT changed();
    }));
    Q_EMIT changed(); return true;
}
void PeerSelectionModel::clear()
{
    if (m_phase == Phase::Stopped || (!m_selected && m_phase == Phase::NoSelection)) return;
    detach(); m_target = {}; m_phase = Phase::NoSelection;
    if (m_revision < std::numeric_limits<quint64>::max()) ++m_revision;
    Q_EMIT changed();
}
void PeerSelectionModel::reconcile()
{
    if (m_phase == Phase::Stopped || m_revision == std::numeric_limits<quint64>::max() || !m_source || m_source->projectionPending()) return;
    if (connected()) {
        const auto row = m_source->indexOfNodeId(m_target.node_id);
        if (row >= 0 && m_source->peerDetailsAt(row) == m_selected) return;
    }
    const QPointer<PeerSelectionModel> guard{this};
    const auto revision = m_revision + (m_selected || m_phase != Phase::NoSelection ? 1 : 0);
    clear(); if (!guard || m_revision != revision || !m_auto || !m_source || m_source->rowCount() == 0) return;
    if (auto* first = m_source->peerDetailsAt(0)) select(first);
}
void PeerSelectionModel::stop()
{
    if (m_phase == Phase::Stopped) return;
    detach(); m_target = {}; m_phase = Phase::Stopped;
    if (m_revision < std::numeric_limits<quint64>::max()) ++m_revision;
    Q_EMIT changed();
}
