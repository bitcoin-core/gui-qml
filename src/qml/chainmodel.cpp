// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/chainmodel.h>

#include <interfaces/chain.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <qml/engine.h>
#include <qt/guiutil.h>

#include <vector>

struct Block
{
    const int height;
    const std::string hash;
};

struct ChainModelPrivate
{
    std::unique_ptr<interfaces::Handler> handler_notify_block_tip;
    std::vector<Block> blocks;
};

ChainModel::ChainModel(QObject* parent)
    : QAbstractListModel(parent)
    , d(new ChainModelPrivate)
{
}

ChainModel::~ChainModel()
{
}

void ChainModel::classBegin()
{
}

void ChainModel::componentComplete()
{
    assert(!d->handler_notify_block_tip);
    d->handler_notify_block_tip = Engine::node(this).handleNotifyBlockTip(
      [this](SynchronizationState state, interfaces::BlockTip tip, double verification_progress) {
          // TODO: update existing model incrementally instead of reset
          GUIUtil::ObjectInvoke(this, [this] {
              beginResetModel();
              d->blocks.clear();
              endResetModel();
          });
      });
}

QHash<int, QByteArray> ChainModel::roleNames() const
{
    return {
        { BlockHeightRole, "blockHeight" },
        { BlockHashRole, "blockHash" }
    };
}

bool ChainModel::canFetchMore(const QModelIndex&) const
{
    return d->blocks.size() == 0 || d->blocks[0].height > 0;
}

void ChainModel::fetchMore(const QModelIndex& parent)
{
    auto& chain = Engine::chain(this);
    int height = d->blocks.size() > 0 ? d->blocks[d->blocks.size() - 1].height - 1 : *chain.getHeight();
    // TODO: make page size configurable
    // TODO: refactor to call beginInsertRows before the loop
    for (int count = 10; count > 0 && height >= 0; --count, --height) {
        const auto hash = chain.getBlockHash(height).ToString();
        beginInsertRows(QModelIndex(), d->blocks.size(), d->blocks.size());
        d->blocks.push_back({height, hash});
        endInsertRows();
    }
}

int ChainModel::rowCount(const QModelIndex& parent) const
{
    return d->blocks.size();
}

QVariant ChainModel::data(const QModelIndex& index, int role) const
{
    switch (role) {
      case BlockHeightRole: return d->blocks[index.row()].height;
      case BlockHashRole: return QString::fromStdString(d->blocks[index.row()].hash);
    } // no default case, so the compiler can warn about missing cases
    assert(false);
}
