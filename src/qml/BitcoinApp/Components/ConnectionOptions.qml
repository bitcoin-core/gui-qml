// Copyright (c) 2021 The Bitcoin Core developers
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
        text: qsTr("Fast always on")
        description: qsTr("Loads quickly at all times and uses as much cellular data as needed.")
        recommended: true
    }
    OptionButton {
        Layout.fillWidth: true
        ButtonGroup.group: group
        checked: true
        text: qsTr("Slow always on")
        description: qsTr("Loads at all times with reduced cellular data usage.")
    }
    OptionButton {
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Only when on Wi-Fi")
        description: qsTr("Loads quickly when on wi-fi and pauses when on cellular data.")
    }
}
