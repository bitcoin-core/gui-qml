// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../controls"

// One row of the node information popup. A named type lets the popup read
// the row's text metrics without dynamic lookups.
ValueRow {
    id: informationRow
    required property int rowCount
    required property var modelData
    required property int index

    title: informationRow.modelData.label
    titleColor: Theme.color.neutral7
    valueObjectName: "nodeInformationValue_" + informationRow.index
    value: informationRow.modelData.value
    valueTextStyle: informationRow.modelData.mono ? Theme.text.monoDescription : Theme.text.description
    valueMaximumWidth: Math.round(informationRow.width * 0.65)
    valueWrap: true
    showDivider: informationRow.index < informationRow.rowCount - 1
}
