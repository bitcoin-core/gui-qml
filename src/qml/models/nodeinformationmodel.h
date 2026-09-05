// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_NODEINFORMATIONMODEL_H
#define BITCOIN_QML_MODELS_NODEINFORMATIONMODEL_H
#include <QObject>
#include <QVariantList>
namespace interfaces { class Node; }
class ChainSyncModel;
class NodeNetworkModel;
class RuntimeDialogModel;

/** Diagnostic presentation reads existing feature state without duplicating it. */
class NodeInformationModel : public QObject
{
    Q_OBJECT
public:
    NodeInformationModel(interfaces::Node& node, const ChainSyncModel& sync,
                         const NodeNetworkModel& network, const RuntimeDialogModel& dialogs);
    Q_INVOKABLE QVariantList nodeInformationRows();
public Q_SLOTS:
    void setReady(bool ready) { m_node_ready = ready; }
private:
    interfaces::Node& m_node;
    const ChainSyncModel& m_sync;
    const NodeNetworkModel& m_network;
    const RuntimeDialogModel& m_dialogs;
    bool m_node_ready{false};
};
#endif // BITCOIN_QML_MODELS_NODEINFORMATIONMODEL_H
