// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

ColumnLayout {
    id: root
    property bool showRestartNotice: false
    readonly property var coreStatuses: optionsModel.coreSettingStatuses
    spacing: 4
    SettingsRestartNotice {
        visible: root.showRestartNotice
        Layout.fillWidth: true
        Layout.bottomMargin: visible ? 12 : 0
    }
    Setting {
        id: dbcacheSetting
        Layout.fillWidth: true
        header: qsTr("Database cache size (MiB)")
        errorText: qsTr("This is not a valid cache size. Please choose a value between %1 and %2 MiB.").arg(optionsModel.minDbcacheSizeMiB).arg(optionsModel.maxDbcacheSizeMiB)
        state: (root.coreStatuses.dbcache || ({})).canEdit === false ? "DISABLED" : "FILLED"
        showErrorText: false
        infoText: (root.coreStatuses.dbcache || ({})).infoText || ""
        showInfoText: !showErrorText && infoText.length > 0
        actionItem: ValueInput {
            parentState: dbcacheSetting.visualState
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
        state: (root.coreStatuses.par || ({})).canEdit === false ? "DISABLED" : "FILLED"
        showErrorText: !loadedItem.acceptableInput && loadedItem.length > 0
        infoText: (root.coreStatuses.par || ({})).infoText || ""
        showInfoText: !showErrorText && infoText.length > 0
        actionItem: ValueInput {
            parentState: parSetting.visualState
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
}
