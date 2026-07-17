// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_CHAINMODEL_H
#define BITCOIN_QML_MODELS_CHAINMODEL_H

#include <QObject>
#include <QString>

/**
 * Immutable chain metadata shared by the application UI.
 *
 * Dynamic block-clock state deliberately lives in BlockClockModel. Keeping
 * this model limited to configuration values makes its ownership and update
 * behavior explicit.
 */
class ChainModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString networkName READ networkName CONSTANT)
    Q_PROPERTY(quint64 assumedBlockchainSize READ assumedBlockchainSize CONSTANT)
    Q_PROPERTY(quint64 assumedChainstateSize READ assumedChainstateSize CONSTANT)

public:
    explicit ChainModel(QString network_name, QObject* parent = nullptr);

    QString networkName() const { return m_network_name; }
    quint64 assumedBlockchainSize() const { return m_assumed_blockchain_size; }
    quint64 assumedChainstateSize() const { return m_assumed_chainstate_size; }

private:
    /** Uppercase chain identifier used by network badges and settings scope. */
    const QString m_network_name;
    /** Estimated disk space, in GiB, shown during initial setup. */
    quint64 m_assumed_blockchain_size;
    /** Estimated chainstate disk space, in GiB, shown during initial setup. */
    quint64 m_assumed_chainstate_size;
};

#endif // BITCOIN_QML_MODELS_CHAINMODEL_H
