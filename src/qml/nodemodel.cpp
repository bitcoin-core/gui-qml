// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/nodemodel.h>

#include <interfaces/node.h>
#include <validation.h>

#include <cassert>

NodeModel::NodeModel(interfaces::Node& node)
    : m_node{node}
{
    ConnectToBlockTipSignal();
}

void NodeModel::setBlockTipHeight(int new_height)
{
    if (new_height != m_block_tip_height) {
        m_block_tip_height = new_height;
        Q_EMIT blockTipHeightChanged();
    }
}

void NodeModel::startNodeInitializionThread()
{
    Q_EMIT requestedInitialize();
}

void NodeModel::startNodeShutdown()
{
    Q_EMIT requestedShutdown();
}

void NodeModel::initializeResult([[maybe_unused]] bool success, interfaces::BlockAndHeaderTipInfo tip_info)
{
    // TODO: Handle the `success` parameter,
    setBlockTipHeight(tip_info.block_height);
}

void NodeModel::ConnectToBlockTipSignal()
{
    assert(!m_handler_notify_block_tip);
    m_handler_notify_block_tip = m_node.handleNotifyBlockTip(
        [this](SynchronizationState state, interfaces::BlockTip tip, double verification_progress) {
            setBlockTipHeight(tip.block_height);
        });
}
