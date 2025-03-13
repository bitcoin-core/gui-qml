// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.settings 1.0
import "../controls"

ColumnLayout {
    id: root
    spacing: 15

    ButtonGroup {
        id: group
    }

    OptionButton {
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Compact")
        description: qsTr("For personal use on a computer or smartphone.")
        image: "image://images/blockstatus-size-compact"
        checked: Theme.blockstatussize == (1/3)
        onClicked: {
            Theme.blockstatussize = (1/3)
        }
    }

    OptionButton {
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Showcase")
        description: qsTr("A larger block status for public display on a tablet or other large screen.")
        image: "image://images/blockstatus-size-showcase"
        checked: Theme.blockstatussize == (1/2)
        onClicked: {
            Theme.blockstatussize = (1/2)
        }
    }
}
