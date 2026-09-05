// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../controls"

ColumnLayout {
    id: root
    property var settingsModel: optionsModel
    property var coreSettingsModel: settingsModel.coreSettings
    property int assumedBlockchainSize: 0
    property int assumedChainstateSize: 0
    property bool customStorage: false
    property int customStorageAmount
    signal storageSelectionChanged(bool customStorage, int customStorageAmount)
    readonly property int reducedStorageTargetGB: 2
    readonly property int reduceRequiredGB: root.assumedChainstateSize + root.reducedStorageTargetGB
    readonly property int fullRequiredGB: root.settingsModel.fullStorageRequiredGB || (root.assumedBlockchainSize + root.assumedChainstateSize)
    readonly property int availableGB: root.settingsModel.storageAvailableGB || 0
    readonly property bool hasStorageResult: root.settingsModel.storageAvailableText && !root.settingsModel.storageCheckPending
    readonly property var pruneSetting: root.coreSettingsModel.entry("prune")
    readonly property bool storageOptionsEditable: root.pruneSetting.canEdit
    readonly property bool showCustomOption: root.customStorage || (root.pruneSetting.enabled && root.pruneSetting.value !== root.reducedStorageTargetGB)
    readonly property int effectiveCustomStorageAmount: root.customStorage ? root.customStorageAmount : root.pruneSetting.value
    readonly property int customRequiredGB: root.effectiveCustomStorageAmount + root.assumedChainstateSize

    function hasEnoughStorage(requiredGB) {
        if (root.settingsModel.existingProfile) return root.settingsModel.storageEnoughForSelected
        return !root.hasStorageResult || root.availableGB >= requiredGB
    }

    ButtonGroup {
        id: group
    }
    spacing: 10
    CoreText {
        objectName: "storagePruneCommandLineInfo"
        Layout.fillWidth: true
        visible: !root.storageOptionsEditable && root.pruneSetting.infoText.length > 0
        text: root.pruneSetting.infoText
        color: Theme.color.neutral7
        font.pixelSize: 15
        horizontalAlignment: Text.AlignLeft
    }
    OptionButton {
        objectName: "storageReduceOption"
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Reduce storage")
        description: qsTr("Uses about %1GB. Suitable for most nodes.").arg(root.reduceRequiredGB)
        enabled: root.storageOptionsEditable && root.hasEnoughStorage(root.reduceRequiredGB)
        recommended: root.storageOptionsEditable && !root.settingsModel.storageEnoughForFull && root.hasEnoughStorage(root.reduceRequiredGB)
        checked: root.pruneSetting.enabled && root.pruneSetting.value === root.reducedStorageTargetGB
        onClicked: {
            root.storageSelectionChanged(root.showCustomOption, root.effectiveCustomStorageAmount)
            root.pruneSetting.enabled = true
            root.pruneSetting.value = root.reducedStorageTargetGB
        }
    }
    OptionButton {
        objectName: "storageFullOption"
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Store all data")
        checked: !root.pruneSetting.enabled
        description: qsTr("Uses about %1GB. Support the network.").arg(root.fullRequiredGB)
        enabled: root.storageOptionsEditable && root.hasEnoughStorage(root.fullRequiredGB)
        recommended: root.storageOptionsEditable && root.settingsModel.storageEnoughForFull
        onClicked: {
            root.storageSelectionChanged(root.showCustomOption, root.effectiveCustomStorageAmount)
            root.pruneSetting.enabled = false
        }
    }
    Loader {
        Layout.fillWidth: true
        active: root.showCustomOption
        visible: active
        sourceComponent: OptionButton {
            objectName: "storageCustomOption"
            ButtonGroup.group: group
            checked: root.pruneSetting.enabled && root.pruneSetting.value === root.effectiveCustomStorageAmount
            text: qsTr("Custom")
            description: qsTr("Storing recent blocks up to %1 GB.").arg(root.effectiveCustomStorageAmount)
            enabled: root.storageOptionsEditable && root.hasEnoughStorage(root.customRequiredGB)
            onClicked: {
                root.storageSelectionChanged(true, root.effectiveCustomStorageAmount)
                root.pruneSetting.enabled = true
                root.pruneSetting.value = root.effectiveCustomStorageAmount
            }
        }
    }
}
