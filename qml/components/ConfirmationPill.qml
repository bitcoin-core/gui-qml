// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../controls"

StatusPill {
    id: root
    property int confirmations: 0
    property bool statusKnown: true
    property bool inactive: false
    accentColor: !statusKnown || inactive || confirmations <= 0 ? Theme.color.neutral7
        : confirmations < 6 ? Theme.color.amber : Theme.color.green
    text: !statusKnown ? qsTr("Status unavailable")
        : !inactive && confirmations === 0 ? qsTr("Unconfirmed")
        : confirmations === 1 ? qsTr("1 confirmation") : qsTr("%1 confirmations").arg(Math.max(0, confirmations))
    backgroundColor: !statusKnown || inactive || confirmations <= 0 ? Theme.color.neutral2
        : Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.16)
}
