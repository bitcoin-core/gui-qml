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
    Setting {
        Layout.fillWidth: true
        header: qsTr("Daily upload limit")
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Connection limit")
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Listening enabled")
        description: qsTr("Reduces data usage.")
    }
    Setting {
        last: true
        Layout.fillWidth: true
        header: qsTr("Blocks Only")
        description: qsTr("Do not transfer unconfirmed transactions. Also disabled listening.")
    }
}
