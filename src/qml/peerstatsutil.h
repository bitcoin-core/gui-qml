// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PEERSTATSUTIL_H
#define BITCOIN_QML_PEERSTATSUTIL_H

#include <net.h>
#include <node/timeoffsets.h>

#include <QString>

#include <chrono>

namespace PeerStatsUtil {

QString ConnectionTypeToQString(ConnectionType conn_type, bool prepend_direction);
QString NetworkToQString(Network net);
QString FormatDurationStr(std::chrono::nanoseconds dur);
QString FormatPeerAge(NodeClock::time_point time_connected);
QString FormatServicesStr(quint64 mask);
QString FormatPingTime(NodeClock::duration ping_time);
QString FormatTimeOffset(int64_t time_offset);
QString FormatBytes(uint64_t bytes);

} // namespace PeerStatsUtil

#endif // BITCOIN_QML_PEERSTATSUTIL_H
