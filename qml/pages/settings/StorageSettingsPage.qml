// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"

SettingsPage {
    id: root
    objectName: "storageSettingsPage"
    title: qsTr("Storage")
    showBackButton: false

    property var settingsModel: optionsModel
    property bool onboarding: false
    property var coreSettingsModel: settingsModel.coreSettings
    readonly property var pruneSetting: coreSettingsModel.entry("prune")
    property string pruneTargetText: String(pruneSetting.value)
    property string pruneTargetError: ""

    signal pruneTargetCommitted(int target)

    function resetPruneTarget() {
        pruneTargetText = String(pruneSetting.value)
        pruneTargetError = ""
    }

    FormSection {
        Layout.fillWidth: true
        title: qsTr("Block storage")
        surfaceLevel: root.onboarding ? 1 : 0

        SwitchRow {
            Layout.fillWidth: true
            objectName: "pruneModeRow"
            title: qsTr("Store recent blocks only")
            dividerColor: Theme.color.surfaceDivider
            supportingText: root.onboarding && root.settingsModel.keepingCurrentStorage
                && root.settingsModel.currentPruneTargetMiB === 1
                //: Explains the automatic-pruning switch when onboarding an existing manually pruned node. Turning it off after enabling it selects full storage.
                ? qsTr("Manual pruning is currently selected. Turning this switch on enables automatic pruning; turning it off afterwards selects full storage.")
                : root.pruneSetting.infoText
            enabled: root.pruneSetting.canEdit
            switchObjectName: "pruneSwitch"
            checked: root.pruneSetting.enabled
            onToggled: root.pruneSetting.enabled = checked
        }

        TextFieldRow {
            id: pruneTargetRow
            Layout.fillWidth: true
            title: qsTr("Block storage limit (GB)")
            enabled: root.pruneSetting.enabled && root.pruneSetting.canEdit
            fieldObjectName: "pruneTargetInput"
            fieldWidth: 80
            text: root.pruneTargetText
            validator: IntegerInputValidator {
                bottom: 1
                onInputRejected: pruneTargetRow.editingFinished()
            }
            errorText: root.pruneTargetError
            supportingText: root.pruneTargetError.length === 0 ? root.pruneSetting.infoText : ""
            showDivider: false
            onTextEdited: function(text) {
                root.pruneTargetText = text
                root.pruneTargetError = ""
            }
            onEditingFinished: {
                root.pruneTargetError = root.settingsModel.commitPruneSizeGB(
                    pruneTargetRow.text, pruneTargetRow.validator.locale)
                if (root.pruneTargetError.length === 0) {
                    root.pruneTargetText = String(root.pruneSetting.value)
                    root.pruneTargetCommitted(root.pruneSetting.value)
                }
            }
        }
    }

    FormSection {
        Layout.fillWidth: true
        title: qsTr("Data directory")
        surfaceLevel: root.onboarding ? 1 : 0
        description: qsTr("Selected before startup. The data directory cannot be changed while the node is running.")

        FormRow {
            Layout.fillWidth: true
            title: qsTr("Location")
            showDivider: false
            bodyItem: CoreText {
                objectName: "dataDirectoryValue"
                Layout.fillWidth: true
                text: root.settingsModel.dataDir
                color: Theme.color.neutral7
                textStyle: Theme.text.monoCaption
                horizontalAlignment: Text.AlignLeft
                wrap: true
            }
        }
    }

    ToastBanner {
        objectName: "storageRestartNotice"
        shown: !root.onboarding && root.settingsModel.storageSettingsDirty
        Layout.fillWidth: true
        Layout.maximumWidth: root.contentLayout.width
        text: qsTr("Restart the application for these changes to take effect.")
    }
}
