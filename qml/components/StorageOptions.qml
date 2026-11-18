// Copyright (c) 2022-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick
import QtQuick.Layouts
import org.bitcoincore.qt 1.0
import "../controls"
ColumnLayout {
    id: root
    required property var settingsModel
    readonly property CoreSettingsModel coreSettingsModel: settingsModel.coreSettings
    property int assumedBlockchainSize: 0
    property int assumedChainstateSize: 0
    readonly property bool customStorage: coreSettingsModel.showCustomStorage
    readonly property int customStorageAmount: coreSettingsModel.customStorageSizeGB
    readonly property var pruneSetting: coreSettingsModel.entry("prune")
    readonly property bool storageOptionsEditable: pruneSetting.canEdit
    spacing: 10
    CoreText {
        objectName: "storagePruneCommandLineInfo"
        Layout.fillWidth: true
        visible: !root.pruneSetting.canEdit && text.length > 0
        text: root.pruneSetting.infoText
        color: Theme.color.neutral7
        textStyle: Theme.text.description
        horizontalAlignment: Text.AlignLeft
        wrap: true
    }
    OptionButton {
        objectName: "storageReduceOption"
        Layout.fillWidth: true
        surfaceSelectionStyle: true
        text: qsTr("Reduce storage")
        description: qsTr("Uses about %1 GB. For regular wallet use.").arg(root.settingsModel.reducedStorageRequiredGB)
        enabled: root.pruneSetting.canEdit && root.settingsModel.storageEnoughForReduced === true
        recommended: !root.settingsModel.storageEnoughForFull && enabled
        selected: root.coreSettingsModel.storageChoice === CoreSettingsModel.ReducedStorage
        onClicked: root.coreSettingsModel.chooseStorage(CoreSettingsModel.ReducedStorage)
    }
    OptionButton {
        objectName: "storageFullOption"
        Layout.fillWidth: true
        surfaceSelectionStyle: true
        text: qsTr("Store all data")
        description: qsTr("Uses about %1 GB. Support the network.").arg(root.settingsModel.fullStorageRequiredGB)
        enabled: root.pruneSetting.canEdit && root.settingsModel.storageEnoughForFull === true
        recommended: enabled
        selected: root.coreSettingsModel.storageChoice === CoreSettingsModel.FullStorage
        onClicked: root.coreSettingsModel.chooseStorage(CoreSettingsModel.FullStorage)
    }
    OptionButton {
        objectName: "storageCustomOption"
        Layout.fillWidth: true
        surfaceSelectionStyle: true
        visible: root.coreSettingsModel.showCustomStorage
        text: qsTr("Custom")
        description: root.coreSettingsModel.customStorageDescription
        enabled: root.pruneSetting.canEdit && root.settingsModel.storageEnoughForCustom === true
        selected: root.coreSettingsModel.storageChoice === CoreSettingsModel.CustomStorage
        onClicked: root.coreSettingsModel.chooseStorage(CoreSettingsModel.CustomStorage)
    }
}
