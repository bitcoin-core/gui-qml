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
        ButtonGroup.group: group
        Layout.fillWidth: true
        text: qsTr("Fast always on")
        description: qsTr("Loads quickly at all times and uses as much cellular data as needed.")
        recommended: true
    }
    OptionButton {
        ButtonGroup.group: group
        Layout.fillWidth: true
        checked: true
        text: qsTr("Slow always on")
        description: qsTr("Loads quickly at all times and uses as much cellular data as needed.")
    }
    OptionButton {
        ButtonGroup.group: group
        Layout.fillWidth: true
        text: qsTr("Only when on Wi-Fi")
        description: qsTr("Loads quickly when on wi-fi and pauses when on cellular data.")
        detail: ProgressIndicator {
            implicitWidth: 50
            SequentialAnimation on progress {
                loops: Animation.Infinite
                SmoothedAnimation { to: 1; velocity: 1; }
                SmoothedAnimation { to: 0; velocity: 1; }
            }
        }
    }
}
