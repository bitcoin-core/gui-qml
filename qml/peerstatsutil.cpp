// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/peerstatsutil.h>

#include <protocol.h>
#include <util/time.h>

#include <QObject>
#include <QString>
#include <QStringList>

#include <cassert>

using namespace std::chrono_literals;

namespace PeerStatsUtil {

QString ConnectionTypeToQString(ConnectionType conn_type, bool prepend_direction)
{
    QString prefix;
    if (prepend_direction) {
        prefix = (conn_type == ConnectionType::INBOUND)
            ? QObject::tr("Inbound")
            : QObject::tr("Outbound") + " ";
    }
    switch (conn_type) {
    case ConnectionType::INBOUND: return prefix;
    case ConnectionType::OUTBOUND_FULL_RELAY: return prefix + QObject::tr("Full Relay");
    case ConnectionType::BLOCK_RELAY: return prefix + QObject::tr("Block Relay");
    case ConnectionType::MANUAL: return prefix + QObject::tr("Manual");
    case ConnectionType::FEELER: return prefix + QObject::tr("Feeler");
    case ConnectionType::ADDR_FETCH: return prefix + QObject::tr("Address Fetch");
    case ConnectionType::PRIVATE_BROADCAST: return prefix + QObject::tr("Private Broadcast");
    }
    assert(false);
}

QString NetworkToQString(Network net)
{
    switch (net) {
    case NET_UNROUTABLE: return QObject::tr("Unroutable");
    case NET_IPV4: return QObject::tr("IPv4", "network name");
    case NET_IPV6: return QObject::tr("IPv6", "network name");
    case NET_ONION: return QObject::tr("Onion", "network name");
    case NET_I2P: return QObject::tr("I2P", "network name");
    case NET_CJDNS: return QObject::tr("CJDNS", "network name");
    case NET_INTERNAL: return "Internal";
    case NET_MAX: assert(false);
    }
    assert(false);
}

QString FormatDurationStr(std::chrono::seconds dur)
{
    const auto d{std::chrono::duration_cast<std::chrono::days>(dur)};
    const auto h{std::chrono::duration_cast<std::chrono::hours>(dur - d)};
    const auto m{std::chrono::duration_cast<std::chrono::minutes>(dur - d - h)};
    const auto s{std::chrono::duration_cast<std::chrono::seconds>(dur - d - h - m)};

    QStringList str_list;
    if (auto d2{d.count()}) str_list.append(QObject::tr("%1 d").arg(d2));
    if (auto h2{h.count()}) str_list.append(QObject::tr("%1 h").arg(h2));
    if (auto m2{m.count()}) str_list.append(QObject::tr("%1 m").arg(m2));
    const auto s2{s.count()};
    if (s2 || str_list.empty()) str_list.append(QObject::tr("%1 s").arg(s2));
    return str_list.join(" ");
}

QString FormatPeerAge(std::chrono::seconds time_connected)
{
    const auto time_now{GetTime<std::chrono::seconds>()};
    const auto age{time_now - time_connected};
    if (age >= 24h) return QObject::tr("%1 d").arg(age / 24h);
    if (age >= 1h) return QObject::tr("%1 h").arg(age / 1h);
    if (age >= 1min) return QObject::tr("%1 m").arg(age / 1min);
    return QObject::tr("%1 s").arg(age / 1s);
}

QString FormatServicesStr(quint64 mask)
{
    QStringList str_list;
    for (const auto& flag : serviceFlagsToStr(mask)) {
        str_list.append(QString::fromStdString(flag));
    }
    if (!str_list.empty()) {
        return str_list.join(", ");
    }
    return QObject::tr("None");
}

QString FormatPingTime(std::chrono::microseconds ping_time)
{
    if (ping_time == std::chrono::microseconds::max() || ping_time == 0us) {
        return QObject::tr("N/A");
    }
    return QObject::tr("%1 ms").arg(QString::number(static_cast<int>(count_microseconds(ping_time) / 1000), 10));
}

QString FormatTimeOffset(int64_t time_offset)
{
    return QObject::tr("%1 s").arg(QString::number(static_cast<int>(time_offset), 10));
}

QString FormatBytes(uint64_t bytes)
{
    if (bytes < 1'000) return QObject::tr("%1 B").arg(bytes);
    if (bytes < 1'000'000) return QObject::tr("%1 kB").arg(bytes / 1'000);
    if (bytes < 1'000'000'000) return QObject::tr("%1 MB").arg(bytes / 1'000'000);
    return QObject::tr("%1 GB").arg(bytes / 1'000'000'000);
}

} // namespace PeerStatsUtil
