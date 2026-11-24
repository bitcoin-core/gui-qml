// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_PEERSELECTIONMODEL_H
#define BITCOIN_QML_MODELS_PEERSELECTIONMODEL_H
#include <qml/models/peerdetailsmodel.h>
#include <qml/models/peerlistproxymodel.h>
#include <QObject>
#include <QPointer>

/** A view-scoped, bounded projection. It never owns a peer command and never
 * substitutes a different connection just because its numeric ID is equal. */
class PeerSelectionModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(PeerListProxyModel* source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool autoSelectFirst READ autoSelectFirst WRITE setAutoSelectFirst NOTIFY autoSelectFirstChanged)
    Q_PROPERTY(PeerDetailsModel* selectedDetails READ selectedDetails NOTIFY changed)
    Q_PROPERTY(QString connectionId READ connectionId NOTIFY changed)
    Q_PROPERTY(QString nodeIdText READ nodeIdText NOTIFY changed)
    Q_PROPERTY(qint64 nodeId READ nodeId NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(Phase phase READ phase NOTIFY changed)
public:
    enum class Phase { NoSelection, Connected, Disconnected, Stopped };
    Q_ENUM(Phase)
    explicit PeerSelectionModel(QObject* parent = nullptr) : QObject(parent) {}
    PeerListProxyModel* source() const { return m_source; }
    void setSource(PeerListProxyModel* source);
    bool autoSelectFirst() const { return m_auto; }
    void setAutoSelectFirst(bool value);
    PeerDetailsModel* selectedDetails() const { return m_selected; }
    QString connectionId() const { return m_target.key; }
    QString nodeIdText() const { return m_target.node_id < 0 ? QString{} : QString::number(m_target.node_id); }
    qint64 nodeId() const { return m_target.node_id; }
    bool connected() const { return m_phase == Phase::Connected && m_selected && m_selected->connected(); }
    Phase phase() const { return m_phase; }
    Q_INVOKABLE bool select(PeerDetailsModel* details);
    Q_INVOKABLE void clear();
    void stop();
Q_SIGNALS:
    void sourceChanged();
    void autoSelectFirstChanged();
    void changed();
private:
    void reconcile();
    void detach();
    QPointer<PeerListProxyModel> m_source;
    QPointer<PeerDetailsModel> m_selected;
    PeerConnection m_target;
    QList<QMetaObject::Connection> m_source_connections, m_detail_connections;
    Phase m_phase{Phase::NoSelection};
    quint64 m_revision{0};
    bool m_auto{false};
};
#endif // BITCOIN_QML_MODELS_PEERSELECTIONMODEL_H
