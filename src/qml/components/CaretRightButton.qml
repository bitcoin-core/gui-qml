// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
    id: root
    required property color stateColor

    leftPadding: 0
    topPadding: 0
    bottomPadding: 0
    icon.source: "image://images/caret-right"
    icon.color: root.stateColor
    icon.height: 18
    icon.width: 18
    background: null

    Behavior on icon.color {
        ColorAnimation { duration: 150 }
    }
}
