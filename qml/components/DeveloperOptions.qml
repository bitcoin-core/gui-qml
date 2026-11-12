// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Layouts 1.15
import "../controls"

ColumnLayout {
    id: root
    property bool showRestartNotice: false
    property bool isOnSurface: false
    property string dbcacheText: String(ApplicationModels.optionsModel.dbcacheSizeMiB)
    property string scriptThreadsText: String(ApplicationModels.optionsModel.scriptThreads)
    property string dbcacheError: ""
    property string scriptThreadsError: ""
    readonly property var coreStatuses: ApplicationModels.optionsModel.coreSettingStatuses

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
                bottom: ApplicationModels.optionsModel.minDbcacheSizeMiB
                top: ApplicationModels.optionsModel.maxDbcacheSizeMiB
            }
            onTextEdited: function(text) {
                root.dbcacheText = text
                root.dbcacheError = ""
            }
            onEditingFinished: {
                const value = parseInt(dbcacheRow.text)
                if (isNaN(value) || value < ApplicationModels.optionsModel.minDbcacheSizeMiB
                        || value > ApplicationModels.optionsModel.maxDbcacheSizeMiB) {
                    root.dbcacheError = qsTr("This is not a valid cache size. Please choose a value between %1 and %2 MiB.")
                        .arg(ApplicationModels.optionsModel.minDbcacheSizeMiB).arg(ApplicationModels.optionsModel.maxDbcacheSizeMiB)
                    return
                }
                ApplicationModels.optionsModel.dbcacheSizeMiB = value
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
                bottom: ApplicationModels.optionsModel.minScriptThreads
                top: ApplicationModels.optionsModel.maxScriptThreads
            }
            onTextEdited: function(text) {
                root.scriptThreadsText = text
                root.scriptThreadsError = ""
            }
            onEditingFinished: {
                const value = parseInt(scriptThreadsRow.text)
                if (isNaN(value) || value < ApplicationModels.optionsModel.minScriptThreads
                        || value > ApplicationModels.optionsModel.maxScriptThreads) {
                    root.scriptThreadsError = qsTr("This is not a valid thread count. Please choose a value between %1 and %2 threads.")
                        .arg(ApplicationModels.optionsModel.minScriptThreads).arg(ApplicationModels.optionsModel.maxScriptThreads)
                    return
                }
                ApplicationModels.optionsModel.scriptThreads = value
                root.scriptThreadsText = String(value)
                root.scriptThreadsError = ""
            }
        }
    }
}
