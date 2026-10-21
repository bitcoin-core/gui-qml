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
    readonly property int reducedStorageTargetGB: root.existingProfile && root.currentPruneTargetMiB > 1
        ? Math.ceil(root.currentPruneTargetMiB * 1024 * 1024 / 1000000000) : 2
    readonly property int reduceRequiredGB: root.assumedChainstateSize + root.reducedStorageTargetGB
    readonly property int fullRequiredGB: root.settingsModel.fullStorageRequiredGB || (root.assumedBlockchainSize + root.assumedChainstateSize)
    readonly property int availableGB: root.settingsModel.storageAvailableGB || 0
    readonly property bool hasStorageResult: root.settingsModel.storageAvailableText && !root.settingsModel.storageCheckPending
    readonly property var pruneSetting: root.coreSettingsModel.entry("prune")
    readonly property bool storageOptionsEditable: root.pruneSetting.canEdit
    readonly property bool existingProfile: root.settingsModel.existingProfile || false
    readonly property bool keepingCurrentStorage: root.existingProfile && root.settingsModel.keepingCurrentStorage
    readonly property real currentPruneTargetMiB: root.existingProfile ? root.settingsModel.currentPruneTargetMiB : 0
    readonly property bool showCustomOption: root.customStorage || (!root.keepingCurrentStorage && root.pruneSetting.enabled && root.pruneSetting.value !== root.reducedStorageTargetGB)
    readonly property int effectiveCustomStorageAmount: root.customStorage ? root.customStorageAmount : root.pruneSetting.value
    readonly property int customRequiredGB: root.effectiveCustomStorageAmount + root.assumedChainstateSize

    function hasEnoughStorage(requiredGB) {
        if (root.existingProfile) return root.settingsModel.storageEnoughForSelected
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
        surfaceSelectionStyle: true
        text: root.currentPruneTargetMiB === 1 ? qsTr("Manual pruning") : qsTr("Reduce storage")
        badgeText: root.existingProfile && root.currentPruneTargetMiB > 0
            ? qsTr("Your current setting") : recommended ? qsTr("Recommended") : ""
        description: root.currentPruneTargetMiB === 1
            ? qsTr("Prune older blocks manually to control storage use.")
            : qsTr("Uses about %1GB. For regular wallet use.").arg(root.reduceRequiredGB)
        enabled: root.storageOptionsEditable && root.hasEnoughStorage(root.reduceRequiredGB)
        recommended: !root.existingProfile && root.storageOptionsEditable && !root.settingsModel.storageEnoughForFull && root.hasEnoughStorage(root.reduceRequiredGB)
        checked: root.keepingCurrentStorage ? root.currentPruneTargetMiB > 0
            : root.pruneSetting.enabled && root.pruneSetting.value === root.reducedStorageTargetGB
                && !(root.existingProfile && root.currentPruneTargetMiB > 0 && root.showCustomOption)
        onClicked: {
            root.storageSelectionChanged(root.showCustomOption, root.effectiveCustomStorageAmount)
            if (root.existingProfile && root.currentPruneTargetMiB > 0) {
                root.settingsModel.keepCurrentStorage()
            } else {
                root.settingsModel.selectStorage(true, root.reducedStorageTargetGB)
            }
        }
    }
    OptionButton {
        objectName: "storageFullOption"
        Layout.fillWidth: true
        ButtonGroup.group: group
        surfaceSelectionStyle: true
        text: qsTr("Store all data")
        checked: root.keepingCurrentStorage ? root.currentPruneTargetMiB === 0 : !root.pruneSetting.enabled
        badgeText: root.existingProfile && root.currentPruneTargetMiB === 0
            ? qsTr("Your current setting") : recommended ? qsTr("Recommended") : ""
        description: qsTr("Uses about %1GB. Support the network.").arg(root.fullRequiredGB)
        enabled: root.storageOptionsEditable && root.hasEnoughStorage(root.fullRequiredGB)
        recommended: !root.existingProfile && root.storageOptionsEditable && root.settingsModel.storageEnoughForFull
        errorText: root.existingProfile && root.currentPruneTargetMiB > 0
            ? qsTr("Requires downloading the full blockchain again.")
                + (root.hasStorageResult && root.availableGB < root.fullRequiredGB
                    ? "\n" + qsTr("%1GB needed; %2GB available.").arg(root.fullRequiredGB).arg(root.availableGB)
                    : "")
            : ""
        onClicked: {
            root.storageSelectionChanged(root.showCustomOption, root.effectiveCustomStorageAmount)
            if (root.existingProfile && root.currentPruneTargetMiB === 0) {
                root.settingsModel.keepCurrentStorage()
            } else {
                root.settingsModel.selectStorage(false, root.pruneSetting.value)
            }
        }
    }
    Loader {
        Layout.fillWidth: true
        active: root.showCustomOption
        visible: active
        sourceComponent: OptionButton {
            objectName: "storageCustomOption"
            ButtonGroup.group: group
            surfaceSelectionStyle: true
            checked: !root.keepingCurrentStorage && root.pruneSetting.enabled && root.pruneSetting.value === root.effectiveCustomStorageAmount
            text: qsTr("Custom")
            description: qsTr("Storing recent blocks up to %1 GB. Uses about %2 GB in total.")
                .arg(root.effectiveCustomStorageAmount).arg(root.customRequiredGB)
            enabled: root.storageOptionsEditable && root.hasEnoughStorage(root.customRequiredGB)
            onClicked: {
                root.storageSelectionChanged(true, root.effectiveCustomStorageAmount)
                root.settingsModel.selectStorage(true, root.effectiveCustomStorageAmount)
            }
        }
    }
}
