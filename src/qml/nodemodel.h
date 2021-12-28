// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_NODEMODEL_H
#define BITCOIN_QML_NODEMODEL_H

#include <interfaces/handler.h>

#include <memory>

#include <QObject>
#include <QQmlParserStatus>

namespace interfaces {
class Node;
}

/** Model for Bitcoin network client. */
class NodeModel : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(int blockTipHeight READ blockTipHeight NOTIFY blockTipHeightChanged)

public:
    explicit NodeModel(QObject* parent = nullptr);

    int blockTipHeight() const { return m_block_tip_height; }
    void setBlockTipHeight(int new_height);

    // QQmlParserStatus
    void classBegin() override;
    void componentComplete() override;

Q_SIGNALS:
    void blockTipHeightChanged();

private:
    // Properties that are exposed to QML.
    int m_block_tip_height{0};

    std::unique_ptr<interfaces::Handler> m_handler_notify_block_tip;
};

#endif // BITCOIN_QML_NODEMODEL_H
