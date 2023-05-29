// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

ColumnLayout {
    spacing: 4
    Setting {
        id: dbcacheSetting
        Layout.fillWidth: true
        header: qsTr("Database cache size (MiB)")
        errorText: qsTr("This is not a valid cache size. Please choose a value between %1 and %2 MiB.").arg(optionsModel.minDbcacheSizeMiB).arg(optionsModel.maxDbcacheSizeMiB)
        showErrorText: false
        actionItem: ValueInput {
            parentState: dbcacheSetting.state
            description: optionsModel.dbcacheSizeMiB
            onEditingFinished: {
                if (checkValidity(optionsModel.minDbcacheSizeMiB, optionsModel.maxDbcacheSizeMiB, parseInt(text))) {
                    optionsModel.dbcacheSizeMiB = parseInt(text)
                    dbcacheSetting.forceActiveFocus()
                    dbcacheSetting.showErrorText = false
                } else {
                    dbcacheSetting.showErrorText = true
                }
            }
        }
        onClicked: {
            loadedItem.filled = true
            loadedItem.forceActiveFocus()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: parSetting
        Layout.fillWidth: true
        header: qsTr("Script verification threads")
        errorText: qsTr("This is not a valid thread count. Please choose a value between %1 and %2 threads.").arg(optionsModel.minScriptThreads).arg(optionsModel.maxScriptThreads)
        showErrorText: !loadedItem.acceptableInput && loadedItem.length > 0
        actionItem: ValueInput {
            parentState: parSetting.state
            description: optionsModel.scriptThreads
            onEditingFinished: {
                if (checkValidity(optionsModel.minScriptThreads, optionsModel.maxScriptThreads, parseInt(text))) {
                    optionsModel.scriptThreads = parseInt(text)
                    parSetting.forceActiveFocus()
                    parSetting.showErrorText = false
                } else {
                    parSetting.showErrorText = true
                }
            }
        }
        onClicked: {
            loadedItem.filled = true
            loadedItem.forceActiveFocus()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Dark Mode")
        actionItem: OptionSwitch {
            checked: Theme.dark
            onToggled: Theme.toggleDark()
        }
        onClicked: loadedItem.toggled()
    }
}
