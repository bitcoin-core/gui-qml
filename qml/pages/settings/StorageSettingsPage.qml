pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"

SettingsPage {
    id: root
    objectName: "storageSettingsPage"
    title: qsTr("Storage")
    showBackButton: false

    property var settingsModel: optionsModel
    property var coreSettingsModel: settingsModel.coreSettings
    readonly property var pruneSetting: coreSettingsModel.entry("prune")
    readonly property bool hasStorageResult: root.settingsModel
        && root.settingsModel["storageAvailableText"] !== undefined
        && root.settingsModel.storageAvailableText.length > 0
        && !root.settingsModel.storageCheckPending
    readonly property int availableStorageGB: root.hasStorageResult
        ? root.settingsModel.storageAvailableGB
        : 0
    readonly property int assumedChainstateSizeGB: root.settingsModel
        && root.settingsModel["assumedChainstateSize"] !== undefined
        ? root.settingsModel.assumedChainstateSize
        : 0
    readonly property int maxPruneSizeGB: root.hasStorageResult
        ? Math.max(0, root.availableStorageGB - root.assumedChainstateSizeGB)
        : 0
    property string pruneTargetText: String(pruneSetting.value)
    property string pruneTargetError: ""

    function validatePruneTarget(value) {
        if (isNaN(value) || value < 1) {
            return qsTr("Choose a storage limit of at least 1 GB.")
        }
        if (root.hasStorageResult && value > root.maxPruneSizeGB) {
            if (root.maxPruneSizeGB < 1) {
                return qsTr("There is not enough available storage for reduced storage in this data directory.")
            }
            return qsTr("Choose a value between 1 GB and %1 GB for this data directory.").arg(root.maxPruneSizeGB)
        }
        return ""
    }

    FormSection {
        Layout.fillWidth: true
        title: qsTr("Block storage")

        FormRow {
            Layout.fillWidth: true
            title: qsTr("Store recent blocks only")
            supportingText: root.pruneSetting.infoText
            enabled: root.pruneSetting.canEdit
            trailingItem: OptionSwitch {
                objectName: "pruneSwitch"
                checked: root.pruneSetting.enabled
                onToggled: root.pruneSetting.enabled = checked
            }
        }

        TextFieldRow {
            id: pruneTargetRow
            Layout.fillWidth: true
            title: qsTr("Block storage limit (GB)")
            enabled: root.pruneSetting.enabled && root.pruneSetting.canEdit
            fieldObjectName: "pruneTargetInput"
            fieldWidth: 80
            text: root.pruneTargetText
            validator: IntValidator { bottom: 1 }
            errorText: root.pruneTargetError
            supportingText: root.pruneTargetError.length === 0 ? root.pruneSetting.infoText : ""
            showDivider: false
            onTextEdited: function(text) {
                root.pruneTargetText = text
                root.pruneTargetError = ""
            }
            onEditingFinished: {
                const parsed = parseInt(pruneTargetRow.text)
                root.pruneTargetError = root.validatePruneTarget(parsed)
                if (root.pruneTargetError.length === 0) {
                    root.pruneSetting.value = parsed
                    root.pruneTargetText = String(parsed)
                }
            }
        }
    }

    FormSection {
        Layout.fillWidth: true
        title: qsTr("Data directory")
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
                font: Theme.text.caption.font
                lineHeight: Theme.text.caption.lineHeight
                lineHeightMode: Text.FixedHeight
                horizontalAlignment: Text.AlignLeft
                wrap: true
            }
        }
    }

    SettingsRestartNotice {
        objectName: "storageRestartNotice"
        visible: root.settingsModel.storageSettingsDirty
        Layout.fillWidth: true
        Layout.maximumWidth: root.contentLayout.width
    }
}
