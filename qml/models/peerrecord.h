// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PEERRECORD_H
#define BITCOIN_QML_MODELS_PEERRECORD_H

#include <net.h>
#include <net_processing.h>
#include <memory>
#include <QMetaType>
#include <QString>

class QObject;

struct CNodeCombinedStats {
    CNodeStats nodeStats;
    CNodeStateStats nodeStateStats;
    bool fNodeStateStatsAvailable;
};
using PeerStatsHandle = std::shared_ptr<const CNodeCombinedStats>;
Q_DECLARE_METATYPE(PeerStatsHandle)
struct PeerPresentation {
    QString rawAddress;
    QString address;
    QString addressLocal;
    QString type;
    QString network;
    QString transport;
    QString sessionId;
    QString version;
    QString userAgent;
    QString services;
    bool transactionRelay {false};
    bool addressRelay {false};
    bool highBandwidth {false};
    QString addressesProcessed;
    QString addressesRateLimited;
    QString startingHeight;
    QString syncedHeaders;
    QString syncedBlocks;
    QString direction;
    QString connectionDuration;
    QString lastSend;
    QString lastReceived;
    QString bytesSent;
    QString bytesReceived;
    QString pingTime;
    QString pingMin;
    QString pingWait;
    QString timeOffset;
    QString mappedAS;
    QString permission;
};
/** Public connection facts, copied while the immutable peer row is prepared.
 * A numeric ID alone is not authority to act on a later connection. An empty
 * transport session can become populated during the original handshake. */
struct PeerConnection {
    QString key;
    qint64 node_id{-1};
    NodeClock::time_point connected;
    CService endpoint;
    QString session;
    QString raw_address;
    QString display_address;
    bool ban_supported{false};
};
inline bool SamePeerConnection(const PeerConnection& expected, const PeerConnection& current)
{
    return expected.node_id == current.node_id && expected.connected == current.connected && expected.endpoint == current.endpoint &&
        (expected.session.isEmpty() || expected.session == current.session);
}
inline bool SamePeerConnection(const CNodeStats& expected, const CNodeStats& current)
{
    return expected.nodeid == current.nodeid && expected.m_connected == current.m_connected && expected.addr == current.addr &&
        (expected.m_session_id.empty() || expected.m_session_id == current.m_session_id);
}
struct PeerRecord { CNodeCombinedStats raw; PeerPresentation presentation; PeerConnection connection; };

/** GUI presentation subscription. The receiver owns this port and remains
 * valid until observerObject() emits destroyed; no Native work uses the port. */
class PeerPresentationObserver
{
public:
    virtual ~PeerPresentationObserver() = default;
    virtual QObject* observerObject() = 0;
    virtual void update(const PeerPresentation& value, bool available) = 0;
    virtual void markDisconnected() = 0;
};

#endif // BITCOIN_QML_MODELS_PEERRECORD_H
