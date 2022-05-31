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
        header: qsTr("Store Recent blocks only")
    }
    Information {
        Layout.fillWidth: true
        header: qsTr("Storage limit")
        description: qsTr("75 GB")
        isReadonly: false
    }
    Information {
        Layout.fillWidth: true
        header: qsTr("Data location")
        description: qsTr("c://.../data")
        isReadonly: false
    }
    Information {
        Layout.fillWidth: true
        header: qsTr("Block location")
        description: qsTr("c://.../blocks")
        isReadonly: false
    }
}
