// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/BitcoinApp/options_model.h>

#include <interfaces/node.h>

OptionsQmlModel::OptionsQmlModel(interfaces::Node& node)
    : m_node{node}
{
}
