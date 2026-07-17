// Copyright (c) 2022-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/chainmodel.h>

#include <chainparams.h>

ChainModel::ChainModel(QString network_name, QObject* parent)
    : QObject{parent},
      m_network_name{network_name.toUpper()},
      m_assumed_blockchain_size{Params().AssumedBlockchainSize()},
      m_assumed_chainstate_size{Params().AssumedChainStateSize()}
{
}
