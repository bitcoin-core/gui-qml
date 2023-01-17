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
        header: qsTr("Store recent blocks only")
        actionItem: OptionSwitch {
            checked: optionsModel.prune
            onToggled: optionsModel.prune = checked
        }
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Storage limit (GB)")
        actionItem: ValueInput {
            description: optionsModel.pruneSizeGB
            onEditingFinished: optionsModel.pruneSizeGB = parseInt(text)
        }
    }
}
