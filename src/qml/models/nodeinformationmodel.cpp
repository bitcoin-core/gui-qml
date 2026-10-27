// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/nodeinformationmodel.h>
#include <qml/models/chainsyncmodel.h>
#include <qml/models/nodenetworkmodel.h>
#include <qml/models/runtimedialogmodel.h>
#include <chainparams.h>
#include <clientversion.h>
#include <common/args.h>
#include <common/system.h>
#include <interfaces/node.h>
#include <util/fs.h>
#include <util/time.h>
#include <QDateTime>
#include <QVariantMap>
#include <algorithm>

namespace {
QVariant InformationRow(const QString& label, const QString& value)
{
    return QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("value"), value}};
}
}

NodeInformationModel::NodeInformationModel(interfaces::Node& node, const ChainSyncModel& sync,
                                         const NodeNetworkModel& network, const RuntimeDialogModel& dialogs)
    : m_node{node}, m_sync{sync}, m_network{network}, m_dialogs{dialogs}
{
}

QVariantList NodeInformationModel::nodeInformationRows()
{
    int header_height{m_sync.headerTipHeight()};
    int64_t header_time{m_sync.headerTipTime()};
    if (m_node_ready && header_height == 0) {
        m_node.getHeaderTip(header_height, header_time);
    }

    QString local_addresses;
    if (m_node_ready) {
        for (const auto& [addr, info] : m_node.getNetLocalAddresses()) {
            local_addresses += QString::fromStdString(addr.ToStringAddr());
            if (!addr.IsI2P()) {
                local_addresses += QStringLiteral(":") + QString::number(info.nPort);
            }
            local_addresses += QStringLiteral(", ");
        }
    }
    if (!local_addresses.isEmpty()) {
        local_addresses.chop(2);
    } else {
        local_addresses = tr("None");
    }

    const int block_height{m_node_ready ? std::max(m_sync.blockTipHeight(), m_node.getNumBlocks()) : m_sync.blockTipHeight()};
    const int64_t last_block_time{m_node_ready ? m_node.getLastBlockTime() : 0};
    const QString warning_text{m_dialogs.warningList().empty() ? tr("None") : m_dialogs.warningList().join(QStringLiteral("\n"))};

    QVariantList rows;
    rows.push_back(InformationRow(tr("Client version"), QString::fromStdString(FormatFullVersion())));
    rows.push_back(InformationRow(tr("User agent"), QString::fromStdString(strSubVersion)));
    rows.push_back(InformationRow(tr("Datadir"), QString::fromStdString(fs::PathToString(gArgs.GetDataDirNet()))));
    rows.push_back(InformationRow(tr("Blocks dir"), QString::fromStdString(fs::PathToString(gArgs.GetBlocksDirPath()))));
    rows.push_back(InformationRow(tr("Startup time"), QDateTime::currentDateTime().addSecs(-TicksSeconds(GetUptime())).toString()));
    rows.push_back(InformationRow(tr("Network"), QString::fromStdString(Params().GetChainTypeString())));
    rows.push_back(InformationRow(tr("Block height"), QString::number(block_height)));
    rows.push_back(InformationRow(tr("Header height"), QString::number(header_height)));
    rows.push_back(InformationRow(tr("Header time"), header_time > 0 ? QDateTime::fromSecsSinceEpoch(header_time).toString() : tr("Unknown")));
    rows.push_back(InformationRow(tr("Last block time"), last_block_time > 0 ? QDateTime::fromSecsSinceEpoch(last_block_time).toString() : tr("Unknown")));
    rows.push_back(InformationRow(tr("Verification progress"), QStringLiteral("%1%").arg(QString::number(m_sync.verificationProgress() * 100.0, 'f', 2))));
    rows.push_back(InformationRow(tr("Peers"), tr("%1 total (%2 inbound, %3 outbound)").arg(m_network.numPeers()).arg(m_network.numInboundPeers()).arg(m_network.numOutboundPeers())));
    rows.push_back(InformationRow(tr("Network active"), m_node_ready ? (m_node.getNetworkActive() ? tr("Yes") : tr("No")) : tr("Unknown")));
    rows.push_back(InformationRow(tr("Local addresses"), local_addresses));
    rows.push_back(InformationRow(tr("Warnings"), warning_text));
    return rows;
}
