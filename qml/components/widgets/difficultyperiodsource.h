// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_COMPONENTS_WIDGETS_DIFFICULTYPERIODSOURCE_H
#define BITCOIN_QML_COMPONENTS_WIDGETS_DIFFICULTYPERIODSOURCE_H

#include <qml/components/widgets/difficultyperiodmodel.h>

namespace interfaces { class Chain; class Node; }

DifficultyPeriodModel::Snapshot ReadDifficultyPeriod(interfaces::Node& node, interfaces::Chain& chain);

#endif // BITCOIN_QML_COMPONENTS_WIDGETS_DIFFICULTYPERIODSOURCE_H
