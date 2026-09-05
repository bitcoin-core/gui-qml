// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/peerstatsutil.h>

#include <protocol.h>
#include <util/time.h>
#include <util/translation.h>

#include <chrono>

using namespace std::chrono_literals;

class PeerStatsUtilTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void connectionType_toQString();
    void network_toQString();
    void formatDuration();
    void formatPeerAge();
    void formatServices();
    void formatPingTime();
    void formatTimeOffset();
    void formatBytes();
};

void PeerStatsUtilTests::connectionType_toQString()
{
    QCOMPARE(PeerStatsUtil::ConnectionTypeToQString(ConnectionType::INBOUND, false), QString(""));
    QCOMPARE(PeerStatsUtil::ConnectionTypeToQString(ConnectionType::INBOUND, true), QString("Inbound"));
    QCOMPARE(PeerStatsUtil::ConnectionTypeToQString(ConnectionType::OUTBOUND_FULL_RELAY, false), QString("Full Relay"));
    QCOMPARE(PeerStatsUtil::ConnectionTypeToQString(ConnectionType::OUTBOUND_FULL_RELAY, true), QString("Outbound Full Relay"));
}

void PeerStatsUtilTests::network_toQString()
{
    QCOMPARE(PeerStatsUtil::NetworkToQString(NET_IPV4), QString("IPv4"));
    QCOMPARE(PeerStatsUtil::NetworkToQString(NET_IPV6), QString("IPv6"));
    QCOMPARE(PeerStatsUtil::NetworkToQString(NET_ONION), QString("Onion"));
    QCOMPARE(PeerStatsUtil::NetworkToQString(NET_I2P), QString("I2P"));
}

void PeerStatsUtilTests::formatDuration()
{
    QCOMPARE(PeerStatsUtil::FormatDurationStr(0s), QString("0 s"));
    QCOMPARE(PeerStatsUtil::FormatDurationStr(59s), QString("59 s"));
    QCOMPARE(PeerStatsUtil::FormatDurationStr(1h + 1min + 1s), QString("1 h 1 m 1 s"));
    QCOMPARE(PeerStatsUtil::FormatDurationStr(24h + 2h), QString("1 d 2 h"));
}

void PeerStatsUtilTests::formatPeerAge()
{
    const auto now{NodeClock::now()};
    QCOMPARE(PeerStatsUtil::FormatPeerAge(now - 90s), QString("1 m"));
    QCOMPARE(PeerStatsUtil::FormatPeerAge(now - 3700s), QString("1 h"));
    QCOMPARE(PeerStatsUtil::FormatPeerAge(now - 90000s), QString("1 d"));
}

void PeerStatsUtilTests::formatServices()
{
    QCOMPARE(PeerStatsUtil::FormatServicesStr(0), QString("None"));
    const QString services{PeerStatsUtil::FormatServicesStr(NODE_NETWORK | NODE_WITNESS)};
    QVERIFY(services.contains("NETWORK"));
    QVERIFY(services.contains("WITNESS"));
}

void PeerStatsUtilTests::formatPingTime()
{
    QCOMPARE(PeerStatsUtil::FormatPingTime(0us), QString("N/A"));
    QCOMPARE(PeerStatsUtil::FormatPingTime(NodeClock::duration::max()), QString("N/A"));
    QCOMPARE(PeerStatsUtil::FormatPingTime(1500us), QString("1 ms"));
    QCOMPARE(PeerStatsUtil::FormatPingTime(2500us), QString("2 ms"));
}

void PeerStatsUtilTests::formatTimeOffset()
{
    QCOMPARE(PeerStatsUtil::FormatTimeOffset(42), QString("42 s"));
    QCOMPARE(PeerStatsUtil::FormatTimeOffset(-5), QString("-5 s"));
}

void PeerStatsUtilTests::formatBytes()
{
    QCOMPARE(PeerStatsUtil::FormatBytes(999), QString("999 B"));
    QCOMPARE(PeerStatsUtil::FormatBytes(1000), QString("1 kB"));
    QCOMPARE(PeerStatsUtil::FormatBytes(1'500'000), QString("1 MB"));
    QCOMPARE(PeerStatsUtil::FormatBytes(2'500'000'000), QString("2 GB"));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <qml/test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(PeerStatsUtilTests)
#else
QTEST_MAIN(PeerStatsUtilTests)
#endif
#include "test_peerstatsutil.moc"
