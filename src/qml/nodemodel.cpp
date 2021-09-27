// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/nodemodel.h>

#include <interfaces/node.h>
#include <qml/engine.h>
#include <validation.h>

#include <cassert>

NodeModel::NodeModel(QObject* parent)
    : QObject(parent)
{
}

void NodeModel::setBlockTipHeight(int new_height)
{
    if (new_height != m_block_tip_height) {
        m_block_tip_height = new_height;
        Q_EMIT blockTipHeightChanged();
    }
}

void NodeModel::classBegin()
{
}

void NodeModel::componentComplete()
{
    auto& node = Engine::node(this);
    setBlockTipHeight(node.getNumBlocks());
    assert(!m_handler_notify_block_tip);
    m_handler_notify_block_tip = node.handleNotifyBlockTip(
        [this](SynchronizationState state, interfaces::BlockTip tip, double verification_progress) {
            setBlockTipHeight(tip.block_height);
        });
}
