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
    }
    Information {
        Layout.fillWidth: true
        header: qsTr("Daily upload limit")
        description: qsTr("250 MB")
        isReadonly: false
    }
    Information {
        Layout.fillWidth: true
        header: qsTr("Connection limit")
        description: qsTr("6")
        isReadonly: false
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Enable listening")
        description: qsTr("Increases data usage")
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Blocks Only")
        description: qsTr("Reduces data usage.")
    }
    Information {
        Layout.fillWidth: true
        header: qsTr("Networks")
        subtext: qsTr("Which networks to use for communication")
        description: qsTr("6")
        isReadonly: false
    }
    Information {
        last: true
        Layout.fillWidth: true
        header: qsTr("Proxy settings")
        description: qsTr(">")
    }
}
