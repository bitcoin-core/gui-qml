// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

ColumnLayout {
    id: root
    property bool customStorage: false
    property int customStorageAmount
    spacing: 4
    Setting {
        Layout.fillWidth: true
        header: qsTr("Store recent blocks only")
        actionItem: OptionSwitch {
            checked: optionsModel.prune
            onToggled: optionsModel.prune = checked
            onCheckedChanged: {
                if (checked == false) {
                    pruneTargetSetting.state = "DISABLED"
                } else {
                    pruneTargetSetting.state = "FILLED"
                }
            }
        }
        onClicked: {
          loadedItem.toggle()
          loadedItem.toggled()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: pruneTargetSetting
        Layout.fillWidth: true
        header: qsTr("Storage limit (GB)")
        actionItem: ValueInput {
            parentState: pruneTargetSetting.state
            description: optionsModel.pruneSizeGB
            onEditingFinished: {
                root.customStorage = true
                root.customStorageAmount = parseInt(text)
                optionsModel.pruneSizeGB = parseInt(text)
                pruneTargetSetting.forceActiveFocus()
            }
        }
        onClicked: {
            loadedItem.filled = true
            loadedItem.forceActiveFocus()
        }
    }
}
