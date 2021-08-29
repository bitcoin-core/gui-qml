// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_NODEMODEL_H
#define BITCOIN_QML_NODEMODEL_H

#include <QObject>

namespace interfaces {
class Node;
}

/** Model for Bitcoin network client. */
class NodeModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int blockTipHeight READ blockTipHeight NOTIFY blockTipHeightChanged)

public:
    explicit NodeModel(interfaces::Node& node);

    int blockTipHeight() const { return m_block_tip_height; }
    void setBlockTipHeight(int new_height);

    Q_INVOKABLE void startNodeInitializionThread();
    void startNodeShutdown();

Q_SIGNALS:
    void blockTipHeightChanged();
    void requestedInitialize();
    void requestedShutdown();

private:
    // Properties that are exposed to QML.
    int m_block_tip_height{0};

    interfaces::Node& m_node;
};

#endif // BITCOIN_QML_NODEMODEL_H
