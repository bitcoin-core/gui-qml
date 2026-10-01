// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../controls"

ColumnLayout {
    id: root
    property bool showRestartNotice: false
    property bool isOnSurface: false
    property string dbcacheText: String(optionsModel.dbcacheSizeMiB)
    property string scriptThreadsText: String(optionsModel.scriptThreads)
    property string dbcacheError: ""
    property string scriptThreadsError: ""
    readonly property var coreStatuses: optionsModel.coreSettingStatuses

    spacing: 24

    SettingsRestartNotice {
        visible: root.showRestartNotice
        Layout.fillWidth: true
    }

    FormSection {
        objectName: "developerSettingsSection"
        Layout.fillWidth: true
        isOnSurface: root.isOnSurface
        showGradientBorder: false

        TextFieldRow {
            id: dbcacheRow
            Layout.fillWidth: true
            title: qsTr("Database cache size (MiB)")
            dividerColor: root.isOnSurface ? Theme.color.neutral3 : Theme.color.neutral2
            enabled: (root.coreStatuses.dbcache || ({})).canEdit !== false
            supportingText: (root.coreStatuses.dbcache || ({})).infoText || ""
            errorText: root.dbcacheError
            fieldObjectName: "developerDbcacheField"
            fieldWidth: 88
            maximumLength: 5
            text: root.dbcacheText
            validator: IntValidator {
                bottom: optionsModel.minDbcacheSizeMiB
                top: optionsModel.maxDbcacheSizeMiB
            }
            onTextEdited: function(text) {
                root.dbcacheText = text
                root.dbcacheError = ""
            }
            onEditingFinished: {
                const value = parseInt(dbcacheRow.text)
                if (isNaN(value) || value < optionsModel.minDbcacheSizeMiB
                        || value > optionsModel.maxDbcacheSizeMiB) {
                    root.dbcacheError = qsTr("This is not a valid cache size. Please choose a value between %1 and %2 MiB.")
                        .arg(optionsModel.minDbcacheSizeMiB).arg(optionsModel.maxDbcacheSizeMiB)
                    return
                }
                optionsModel.dbcacheSizeMiB = value
                root.dbcacheText = String(value)
                root.dbcacheError = ""
            }
        }

        TextFieldRow {
            id: scriptThreadsRow
            Layout.fillWidth: true
            title: qsTr("Script verification threads")
            showDivider: false
            enabled: (root.coreStatuses.par || ({})).canEdit !== false
            supportingText: (root.coreStatuses.par || ({})).infoText || ""
            errorText: root.scriptThreadsError
            fieldObjectName: "developerScriptThreadsField"
            fieldWidth: 88
            maximumLength: 5
            text: root.scriptThreadsText
            validator: IntValidator {
                bottom: optionsModel.minScriptThreads
                top: optionsModel.maxScriptThreads
            }
            onTextEdited: function(text) {
                root.scriptThreadsText = text
                root.scriptThreadsError = ""
            }
            onEditingFinished: {
                const value = parseInt(scriptThreadsRow.text)
                if (isNaN(value) || value < optionsModel.minScriptThreads
                        || value > optionsModel.maxScriptThreads) {
                    root.scriptThreadsError = qsTr("This is not a valid thread count. Please choose a value between %1 and %2 threads.")
                        .arg(optionsModel.minScriptThreads).arg(optionsModel.maxScriptThreads)
                    return
                }
                optionsModel.scriptThreads = value
                root.scriptThreadsText = String(value)
                root.scriptThreadsError = ""
            }
        }
    }
}
