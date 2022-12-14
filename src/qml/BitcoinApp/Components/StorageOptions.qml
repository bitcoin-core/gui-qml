// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls

ColumnLayout {
    ButtonGroup {
        id: group
    }
    spacing: 15
    OptionButton {
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Reduce storage")
        description: qsTr("Uses about 2GB. For simple wallet use.")
        recommended: true
        checked: true
    }
    OptionButton {
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Store all data")
        description: qsTr("Uses about 550GB. Support the network.")
    }
}
