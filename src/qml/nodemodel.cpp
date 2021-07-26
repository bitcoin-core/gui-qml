// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/nodemodel.h>

#include <QDebug>

void NodeModel::startNodeInitializionThread()
{
    Q_EMIT requestedInitialize();
}

void NodeModel::startNodeShutdown()
{
    Q_EMIT requestedShutdown();
}
