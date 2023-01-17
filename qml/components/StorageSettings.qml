// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

ColumnLayout {
    spacing: 20
    Setting {
        Layout.fillWidth: true
        header: qsTr("Store Recent blocks only")
        actionItem: OptionSwitch {}
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Storage limit")
        actionItem: ValueInput {
            description: qsTr("2 GB")
        }
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Data location")
        actionItem: ValueInput {
            description: "c://.../data"
        }
    }
}
