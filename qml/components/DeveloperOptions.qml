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
            text: root.dbcacheText
            validator: IntegerInputValidator {
                bottom: optionsModel.minDbcacheSizeMiB
                top: optionsModel.maxDbcacheSizeMiB
                onInputRejected: dbcacheRow.editingFinished()
            }
            onTextEdited: function(text) {
                root.dbcacheText = text
                root.dbcacheError = ""
            }
            onEditingFinished: {
                root.dbcacheError = optionsModel.commitDbcacheSizeMiB(dbcacheRow.text, dbcacheRow.validator.locale)
                if (root.dbcacheError.length === 0) {
                    root.dbcacheText = String(optionsModel.dbcacheSizeMiB)
                }
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
            text: root.scriptThreadsText
            validator: IntegerInputValidator {
                bottom: optionsModel.minScriptThreads
                top: optionsModel.maxScriptThreads
                onInputRejected: scriptThreadsRow.editingFinished()
            }
            onTextEdited: function(text) {
                root.scriptThreadsText = text
                root.scriptThreadsError = ""
            }
            onEditingFinished: {
                root.scriptThreadsError = optionsModel.commitScriptThreads(scriptThreadsRow.text, scriptThreadsRow.validator.locale)
                if (root.scriptThreadsError.length === 0) {
                    root.scriptThreadsText = String(optionsModel.scriptThreads)
                }
            }
        }
    }
}
