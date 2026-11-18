// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Layouts 1.15
import "../controls"

ColumnLayout {
    id: root
    property SettingsEditorModel editor: null
    readonly property SettingsEntryModel cacheEntry: editor ? editor.entry("dbcache") : null
    readonly property SettingsEntryModel threadsEntry: editor ? editor.entry("par") : null
    property bool showRestartNotice: false
    property int surfaceLevel: 0
    property string dbcacheText: String(ApplicationModels.optionsModel.dbcacheSizeMiB)
    property string scriptThreadsText: String(ApplicationModels.optionsModel.scriptThreads)
    property string dbcacheError: ""
    property string scriptThreadsError: ""
    readonly property var coreStatuses: ApplicationModels.optionsModel.coreSettingStatuses

    spacing: Theme.space.xxl

    ToastBanner {
        shown: root.showRestartNotice
        Layout.fillWidth: true
        text: qsTr("Restart the application for these changes to take effect.")
    }

    FormSection {
        objectName: "developerSettingsSection"
        Layout.fillWidth: true
        surfaceLevel: root.surfaceLevel

        TextFieldRow {
            id: dbcacheRow
            Layout.fillWidth: true
            title: qsTr("Database cache size (MiB)")
            dividerColor: Theme.color.surface(root.surfaceLevel + 2)
            enabled: root.cacheEntry ? root.cacheEntry.editable : (root.coreStatuses.dbcache || ({})).canEdit !== false
            supportingText: root.cacheEntry ? root.cacheEntry.information : (root.coreStatuses.dbcache || ({})).infoText || ""
            errorText: root.cacheEntry ? root.cacheEntry.error : root.dbcacheError
            fieldObjectName: "developerDbcacheField"
            fieldWidth: 88
            maximumLength: root.cacheEntry ? 32769 : 10
            text: root.cacheEntry ? root.cacheEntry.text : root.dbcacheText
            validator: root.cacheEntry ? null : legacyCacheValidator
            IntValidator {
                id: legacyCacheValidator
                bottom: ApplicationModels.optionsModel.minDbcacheSizeMiB
                top: ApplicationModels.optionsModel.maxDbcacheSizeMiB
            }
            onTextEdited: function(text) {
                if (root.cacheEntry) { root.cacheEntry.editText(text); return }
                root.dbcacheText = text
                root.dbcacheError = ""
            }
            onEditingFinished: {
                if (root.cacheEntry) return
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
            enabled: root.threadsEntry ? root.threadsEntry.editable : (root.coreStatuses.par || ({})).canEdit !== false
            supportingText: root.threadsEntry ? root.threadsEntry.information : (root.coreStatuses.par || ({})).infoText || ""
            errorText: root.threadsEntry ? root.threadsEntry.error : root.scriptThreadsError
            fieldObjectName: "developerScriptThreadsField"
            fieldWidth: 88
            maximumLength: root.threadsEntry ? 32769 : 10
            text: root.threadsEntry ? root.threadsEntry.text : root.scriptThreadsText
            validator: root.threadsEntry ? null : legacyThreadValidator
            IntValidator {
                id: legacyThreadValidator
                bottom: ApplicationModels.optionsModel.minScriptThreads
                top: ApplicationModels.optionsModel.maxScriptThreads
            }
            onTextEdited: function(text) {
                if (root.threadsEntry) { root.threadsEntry.editText(text); return }
                root.scriptThreadsText = text
                root.scriptThreadsError = ""
            }
            onEditingFinished: {
                if (root.threadsEntry) return
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
