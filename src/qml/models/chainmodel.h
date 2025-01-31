// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_CHAINMODEL_H
#define BITCOIN_QML_MODELS_CHAINMODEL_H

#include <chainparams.h>
#include <interfaces/chain.h>

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariant>

namespace interfaces {
class FoundBlock;
class Chain;
} // namespace interfaces

static const int SECS_IN_12_HOURS = 43200;

class ChainModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentNetworkName READ currentNetworkName WRITE setCurrentNetworkName NOTIFY currentNetworkNameChanged)
    Q_PROPERTY(quint64 assumedBlockchainSize READ assumedBlockchainSize CONSTANT)
    Q_PROPERTY(quint64 assumedChainstateSize READ assumedChainstateSize CONSTANT)
    Q_PROPERTY(QVariantList timeRatioList READ timeRatioList NOTIFY timeRatioListChanged)
    Q_PROPERTY(bool isSnapshotActive READ isSnapshotActive NOTIFY isSnapshotActiveChanged)

public:
    explicit ChainModel(interfaces::Chain& chain);

    QString currentNetworkName() const { return m_current_network_name; };
    void setCurrentNetworkName(QString network_name);
    quint64 assumedBlockchainSize() const { return m_assumed_blockchain_size; };
    quint64 assumedChainstateSize() const { return m_assumed_chainstate_size; };
    QVariantList timeRatioList() const { return m_time_ratio_list; };
    bool isSnapshotActive() const { return m_chain.hasAssumedValidChain(); };
    int timestampAtMeridian();

    void setCurrentTimeRatio();

    Q_INVOKABLE QVariantMap getSnapshotInfo();

public Q_SLOTS:
    void setTimeRatioList(int new_time);
    void setTimeRatioListInitial();

Q_SIGNALS:
    void timeRatioListChanged();
    void currentNetworkNameChanged();
    void isSnapshotActiveChanged();

private:
    QString m_current_network_name;
    quint64 m_assumed_blockchain_size{ Params().AssumedBlockchainSize() };
    quint64 m_assumed_chainstate_size{ Params().AssumedChainStateSize() };
    /* time_ratio: Ratio between the time at which an event
     * happened and 12 hours. So, for example, if a block is
     * found at 4 am or pm, the time_ratio would be 0.3.
     * The m_time_ratio_list stores the time ratio value for
     * the current_time and the time at which the blocks in
     * the last 12 hours were mined. */
    QVariantList m_time_ratio_list{0.0};

    interfaces::Chain& m_chain;
};

#endif // BITCOIN_QML_MODELS_CHAINMODEL_H
