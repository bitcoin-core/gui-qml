// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PEERDETAILSMODEL_H
#define BITCOIN_QML_MODELS_PEERDETAILSMODEL_H

#include <qml/models/peerrecord.h>
#include <qml/peercommandport.h>

#include <QObject>
#include <QPointer>

class PeerListModel;

class PeerDetailsModel : public QObject, public PeerPresentationObserver
{
    Q_OBJECT
    Q_PROPERTY(qint64 nodeId READ nodeId NOTIFY dataChanged)
    Q_PROPERTY(QString nodeIdText READ nodeIdText CONSTANT)
    Q_PROPERTY(QString connectionId READ connectionId CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY dataChanged)
    Q_PROPERTY(bool canDisconnect READ canDisconnect NOTIFY dataChanged)
    Q_PROPERTY(bool canBan READ canBan NOTIFY dataChanged)
    Q_PROPERTY(QString rawAddress READ rawAddress CONSTANT)
    Q_PROPERTY(QString address READ address NOTIFY dataChanged)
    Q_PROPERTY(QString addressLocal READ addressLocal NOTIFY dataChanged)
    Q_PROPERTY(QString type READ type NOTIFY dataChanged)
    Q_PROPERTY(QString network READ network NOTIFY dataChanged)
    Q_PROPERTY(QString transport READ transport NOTIFY dataChanged)
    Q_PROPERTY(QString sessionId READ sessionId NOTIFY dataChanged)
    Q_PROPERTY(QString version READ version NOTIFY dataChanged)
    Q_PROPERTY(QString userAgent READ userAgent NOTIFY dataChanged)
    Q_PROPERTY(QString services READ services NOTIFY dataChanged)
    Q_PROPERTY(bool transactionRelay READ transactionRelay NOTIFY dataChanged)
    Q_PROPERTY(bool addressRelay READ addressRelay NOTIFY dataChanged)
    Q_PROPERTY(bool highBandwidth READ highBandwidth NOTIFY dataChanged)
    Q_PROPERTY(QString addressesProcessed READ addressesProcessed NOTIFY dataChanged)
    Q_PROPERTY(QString addressesRateLimited READ addressesRateLimited NOTIFY dataChanged)
    Q_PROPERTY(QString startingHeight READ startingHeight NOTIFY dataChanged)
    Q_PROPERTY(QString syncedHeaders READ syncedHeaders NOTIFY dataChanged)
    Q_PROPERTY(QString syncedBlocks READ syncedBlocks NOTIFY dataChanged)
    Q_PROPERTY(QString direction READ direction NOTIFY dataChanged)
    Q_PROPERTY(QString connectionDuration READ connectionDuration NOTIFY dataChanged)
    Q_PROPERTY(QString lastSend READ lastSend NOTIFY dataChanged)
    Q_PROPERTY(QString lastReceived READ lastReceived NOTIFY dataChanged)
    Q_PROPERTY(QString bytesSent READ bytesSent NOTIFY dataChanged)
    Q_PROPERTY(QString bytesReceived READ bytesReceived NOTIFY dataChanged)
    Q_PROPERTY(QString pingTime READ pingTime NOTIFY dataChanged)
    Q_PROPERTY(QString pingWait READ pingWait NOTIFY dataChanged)
    Q_PROPERTY(QString pingMin READ pingMin NOTIFY dataChanged)
    Q_PROPERTY(QString timeOffset READ timeOffset NOTIFY dataChanged)
    Q_PROPERTY(QString mappedAS READ mappedAS NOTIFY dataChanged)
    Q_PROPERTY(QString permission READ permission NOTIFY dataChanged)

public:
    explicit PeerDetailsModel(PeerStatsHandle stats, PeerListModel* model);
    qint64 nodeId() const { return m_node_id; }
    QString nodeIdText() const { return m_node_id < 0 ? QString{} : QString::number(m_node_id); }
    QString connectionId() const { return m_connection.key; }
    bool connected() const;
    PeerConnection commandTarget() const { return m_connection; }
    PeerListModel* source() const;
    bool canDisconnect() const;
    bool canBan() const;
    Q_INVOKABLE PeerCommandOperation* prepareDisconnect();
    Q_INVOKABLE PeerCommandOperation* prepareBan(PeerCommandOperation::BanDuration duration);
    QString rawAddress() const { return m_cache.rawAddress; }
    QString address() const { return m_cache.address; }
    QString addressLocal() const { return m_cache.addressLocal; }
    QString type() const { return m_cache.type; }
    QString network() const { return m_cache.network; }
    QString transport() const { return m_cache.transport; }
    QString sessionId() const { return m_cache.sessionId; }
    QString version() const { return m_cache.version; }
    QString userAgent() const { return m_cache.userAgent; }
    QString services() const { return m_cache.services; }
    bool transactionRelay() const { return m_cache.transactionRelay; }
    bool addressRelay() const { return m_cache.addressRelay; }
    bool highBandwidth() const { return m_cache.highBandwidth; }
    QString addressesProcessed() const { return m_cache.addressesProcessed; }
    QString addressesRateLimited() const { return m_cache.addressesRateLimited; }
    QString startingHeight() const { return m_cache.startingHeight; }
    QString syncedHeaders() const { return m_cache.syncedHeaders; }
    QString syncedBlocks() const { return m_cache.syncedBlocks; }
    QString direction() const { return m_cache.direction; }
    QString connectionDuration() const { return m_cache.connectionDuration; }
    QString lastSend() const { return m_cache.lastSend; }
    QString lastReceived() const { return m_cache.lastReceived; }
    QString bytesSent() const { return m_cache.bytesSent; }
    QString bytesReceived() const { return m_cache.bytesReceived; }
    QString pingTime() const { return m_cache.pingTime; }
    QString pingMin() const { return m_cache.pingMin; }
    QString pingWait() const { return m_cache.pingWait; }
    QString timeOffset() const { return m_cache.timeOffset; }
    QString mappedAS() const { return m_cache.mappedAS; }
    QString permission() const { return m_cache.permission; }
    QObject* observerObject() override { return this; }
    void update(const PeerPresentation& value, bool available) override;
    void markDisconnected() override;
Q_SIGNALS:
    void dataChanged();
    void disconnected();
private:
    qint64 m_node_id{-1};
    QPointer<PeerListModel> m_model;
    PeerPresentation m_cache;
    PeerConnection m_connection;
    bool m_disconnected{true};
};
#endif // BITCOIN_QML_MODELS_PEERDETAILSMODEL_H
