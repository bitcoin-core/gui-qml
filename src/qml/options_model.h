// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_OPTIONS_MODEL_H
#define BITCOIN_QML_OPTIONS_MODEL_H

#include <QObject>

namespace interfaces {
class Node;
}

/** Model for Bitcoin client options. */
class OptionsQmlModel : public QObject
{
    Q_OBJECT

public:
    explicit OptionsQmlModel(interfaces::Node& node);

private:
    interfaces::Node& m_node;
};

#endif // BITCOIN_QML_OPTIONS_MODEL_H
