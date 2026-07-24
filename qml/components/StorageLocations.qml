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
    property int minimumStorageRequiredGB: 0
    property string validationError: ""
    readonly property string storageAvailableText: root.settingsModel.storageAvailableText || ""
    readonly property int storageAvailableGB: root.settingsModel.storageAvailableGB || 0
    readonly property string storageErrorText: root.settingsModel.storageErrorText || ""
    readonly property bool storageCheckPending: root.settingsModel.storageCheckPending || false
    readonly property bool hasStorageResult: root.storageAvailableText.length > 0 && root.storageErrorText.length === 0 && !root.storageCheckPending
    readonly property bool selectedLocationBelowMinimum: root.validationError.length === 0 && root.minimumStorageRequiredGB > 0 && root.hasStorageResult && root.storageAvailableGB < root.minimumStorageRequiredGB
    readonly property string selectedLocationStorageError: root.selectedLocationBelowMinimum ? qsTr("Not enough storage available.") : ""
    readonly property bool validSelection: validationError.length === 0 && storageErrorText.length === 0 && !storageCheckPending && !selectedLocationBelowMinimum

    function locationDescription(baseDescription, selected) {
        if (!selected || !root.hasStorageResult) return baseDescription
        return qsTr("%1\n%2.").arg(baseDescription).arg(root.storageAvailableText)
    }

    function updateValidation() {
        if (root.settingsModel.dataDir === root.settingsModel.getDefaultDataDirString) {
            root.validationError = ""
        } else {
            root.validationError = root.settingsModel.validateCustomDataDir(root.settingsModel.dataDir)
        }
    }

    Component.onCompleted: updateValidation()

    Connections {
        target: root.settingsModel
        function onDataDirChanged() {
            root.updateValidation()
        }
    }

    ButtonGroup {
        id: group
    }
    spacing: 10
    OptionButton {
        id: defaultDirOption
        objectName: "storageDefaultLocationOption"
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Default")
        checked: root.settingsModel.dataDir === root.settingsModel.getDefaultDataDirString
        description: root.locationDescription(qsTr("Your application directory."), checked)
        errorText: checked ? root.selectedLocationStorageError : ""
        showErrorText: checked && root.selectedLocationStorageError.length > 0
        onClicked: {
            root.validationError = ""
            root.settingsModel.useDefaultDataDir()
        }
    }
    OptionButton {
        id: customDirOption
        objectName: "storageCustomLocationOption"
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Custom")
        description: root.locationDescription(qsTr("Choose the directory and storage device."), checked)
        customDir: checked && root.settingsModel.dataDir !== root.settingsModel.getDefaultDataDirString ? root.settingsModel.dataDir : ""
        checked: root.settingsModel.dataDir !== root.settingsModel.getDefaultDataDirString
        errorText: checked ? root.selectedLocationStorageError : ""
        showErrorText: checked && root.selectedLocationStorageError.length > 0
        onClicked: folderDialog.open()
    }
    AppFolderDialog {
        id: folderDialog
        objectName: "customDataDirFolderDialog"
        onAccepted: {
            var customDataDir = folderDialog.selectedFolder.toString();
            if (customDataDir !== "") {
                root.validationError = root.settingsModel.validateCustomDataDir(customDataDir)
                if (root.validationError === "" && root.settingsModel.selectCustomDataDir(customDataDir)) {
                } else if (root.validationError === "") {
                    root.validationError = qsTr("The selected data directory could not be created.")
                }
            }
        }
        onRejected: {
            console.log("Custom datadir selection canceled")
        }
    }
    CoreText {
        Layout.fillWidth: true
        visible: root.validationError.length > 0
        text: root.validationError
        color: Theme.color.blue
        horizontalAlignment: Text.AlignLeft
        font.pixelSize: 15
    }
    CoreText {
        Layout.fillWidth: true
        visible: root.validationError.length === 0 && root.storageErrorText.length > 0
        text: root.storageErrorText
        color: Theme.color.blue
        horizontalAlignment: Text.AlignLeft
        font.pixelSize: 15
    }
    CoreText {
        Layout.fillWidth: true
        visible: root.validationError.length === 0 && root.storageErrorText.length === 0 && root.storageCheckPending
        text: qsTr("Checking available storage...")
        color: Theme.color.neutral7
        horizontalAlignment: Text.AlignLeft
        font.pixelSize: 15
    }
}
