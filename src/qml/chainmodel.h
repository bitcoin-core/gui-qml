// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_CHAINMODEL_H
#define BITCOIN_QML_CHAINMODEL_H

#include <interfaces/chain.h>

#include <QObject>
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
    Q_PROPERTY(QVariantList timeRatioList READ timeRatioList NOTIFY timeRatioListChanged)

public:
    explicit ChainModel(interfaces::Chain& chain);

    QVariantList timeRatioList() const { return m_time_ratio_list; };

    int timestampAtMeridian();

    void setCurrentTimeRatio();

public Q_SLOTS:
    void setTimeRatioList(int new_time);
    void setTimeRatioListInitial();

Q_SIGNALS:
    void timeRatioListChanged();

private:
    /* time_ratio: Ratio between the time at which an event
     * happened and 12 hours. So, for example, if a block is
     * found at 4 am or pm, the time_ratio would be 0.3.
     * The m_time_ratio_list stores the time ratio value for
     * the current_time and the time at which the blocks in
     * the last 12 hours were mined. */
    QVariantList m_time_ratio_list{0.0};

    interfaces::Chain& m_chain;
};

#endif // BITCOIN_QML_CHAINMODEL_H
