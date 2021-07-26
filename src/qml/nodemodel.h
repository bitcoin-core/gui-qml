// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_NODEMODEL_H
#define BITCOIN_QML_NODEMODEL_H

#include <QObject>

/** Model for Bitcoin network client. */
class NodeModel : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE void startNodeInitializionThread();
    void startNodeShutdown();

Q_SIGNALS:
    void requestedInitialize();
    void requestedShutdown();
};

#endif // BITCOIN_QML_NODEMODEL_H
