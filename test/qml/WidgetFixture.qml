// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../../qml/components/widgets"

DashboardWidget {
    Rectangle {
        anchors.fill: parent
        color: "#122d29"
        radius: 12
    }
    Text {
        anchors.centerIn: parent
        text: parent.columnSpan + " × " + parent.rowSpan
        color: "#85d5b3"
        font.pixelSize: 24
    }
}
