// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

ColumnLayout {
    id: root
    property var settingsModel: optionsModel
    property var coreSettingsModel: settingsModel.coreSettings
    property bool customStorage: false
    property int customStorageAmount
    property bool showRestartNotice: false
    readonly property var pruneSetting: coreSettingsModel.entry("prune")
    readonly property bool hasStorageResult: root.settingsModel && root.settingsModel["storageAvailableText"] !== undefined && root.settingsModel.storageAvailableText.length > 0 && !root.settingsModel.storageCheckPending
    readonly property int availableStorageGB: root.hasStorageResult ? root.settingsModel.storageAvailableGB : 0
    readonly property int assumedChainstateSizeGB: root.settingsModel && root.settingsModel["assumedChainstateSize"] !== undefined ? root.settingsModel.assumedChainstateSize : 0
    readonly property int maxPruneSizeGB: root.hasStorageResult ? Math.max(0, root.availableStorageGB - root.assumedChainstateSizeGB) : 0

    function pruneTargetError(value) {
        if (isNaN(value) || value < 1) {
            return qsTr("This is not a valid prune target. Please choose a value that is equal to or larger than 1GB.")
        }
        if (root.hasStorageResult && value > root.maxPruneSizeGB) {
            if (root.maxPruneSizeGB < 1) {
                return qsTr("There is not enough available storage for reduced storage with the selected data directory.")
            }
            return qsTr("This is not a valid prune target. Please choose a value between 1GB and %1GB for the selected data directory.").arg(root.maxPruneSizeGB)
        }
        return ""
    }

    spacing: 4
    SettingsRestartNotice {
        visible: root.showRestartNotice
        Layout.fillWidth: true
        Layout.bottomMargin: visible ? 12 : 0
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Store recent blocks only")
        state: root.pruneSetting.canEdit ? "FILLED" : "DISABLED"
        infoText: root.pruneSetting.infoText
        showInfoText: infoText.length > 0
        actionItem: OptionSwitch {
            checked: root.pruneSetting.enabled
            onToggled: {
                root.pruneSetting.enabled = checked
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
        objectName: "pruneTargetSetting"
        Layout.fillWidth: true
        header: qsTr("Block Storage limit (GB)")
        errorText: ""
        state: root.pruneSetting.enabled && root.pruneSetting.canEdit ? "FILLED" : "DISABLED"
        showErrorText: false
        infoText: root.pruneSetting.infoText
        showInfoText: !showErrorText && infoText.length > 0
        actionItem: ValueInput {
            objectName: "pruneTargetInput"
            parentState: pruneTargetSetting.visualState
            description: root.pruneSetting.value
            onTextEdited: {
                pruneTargetSetting.showErrorText = false
            }
            onEditingFinished: {
                const parsed = parseInt(text)
                const error = root.pruneTargetError(parsed)
                if (error.length > 0) {
                    pruneTargetSetting.errorText = error
                    pruneTargetSetting.showErrorText = true
                } else {
                    root.customStorage = parsed !== 2
                    root.customStorageAmount = parsed
                    root.pruneSetting.value = parsed
                    pruneTargetSetting.forceActiveFocus()
                    pruneTargetSetting.showErrorText = false
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
        id: customDataDirSetting
        Layout.fillWidth: true
        header: qsTr("Data Directory")
        infoText: qsTr("Selected before startup. The data directory cannot be changed while the node is running.")
        showInfoText: true
    }
    CoreText {
        Layout.fillWidth: true
        text: root.settingsModel.dataDir
        color: Theme.color.neutral7
        font.pixelSize: 15
        horizontalAlignment: Text.AlignLeft
    }
}
