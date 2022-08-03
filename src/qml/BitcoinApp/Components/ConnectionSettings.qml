// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls

ColumnLayout {
    spacing: 20
    Setting {
        Layout.fillWidth: true
        header: qsTr("Use cellular data")
        actionItem: OptionSwitch{}
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Daily upload limit")
        actionItem: ValueInput {
            description: qsTr("250 MB")
        }
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Connection limit")
        actionItem: ValueInput {
            description: qsTr("6")
        }
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Enable listening")
        description: qsTr("Increases data usage")
        actionItem: OptionSwitch {}
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Blocks Only")
        description: qsTr("Reduces data usage.")
        actionItem: OptionSwitch {}
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Networks")
        description: qsTr("Which networks to use for communication")
        actionItem: ValueInput {
            description: qsTr("6")
        }
    }
    Setting {
        last: true
        Layout.fillWidth: true
        header: qsTr("Proxy settings")
        actionItem: Button {
            icon.source: "image://images/caret-right"
            icon.color: Theme.color.neutral9
            icon.height: 18
            icon.width: 18
            background: null
        }
    }
}
