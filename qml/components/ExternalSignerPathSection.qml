// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

// The signer path and what checking it found. Whether the signer command
// responds and whether a device was found are separate results: a working
// HWI with no device connected needs a different fix than a broken path.
FormSection {
    id: root

    // Prefix for the objectNames of the rows, so two instances stay apart.
    property string namePrefix: "externalSigner"
    property bool showCheckButton: true

    readonly property var signerStatus: (optionsModel.coreSettingStatuses || ({})).signer || ({})
    readonly property bool editable: root.signerStatus.canEdit !== false
    readonly property string pathError: optionsModel.externalSignerPathValidationError(pathInput.text)
    readonly property bool pathChanged: pathInput.text.trim() !== optionsModel.externalSignerPath
    // The status below describes the path in the field.
    readonly property bool checked: !root.pathChanged && walletController.externalSignerPathConfigured
    readonly property bool commandResponding: root.checked && walletController.externalSignerResponding
    readonly property bool deviceReady: root.commandResponding && walletController.canCreateExternalSignerWallet

    function commitPath() {
        if (root.pathError.length > 0) return false
        const path = pathInput.text.trim()
        if (path !== optionsModel.externalSignerPath) optionsModel.externalSignerPath = path
        // The model normalizes the path; show what it stored.
        pathInput.text = optionsModel.externalSignerPath
        return true
    }

    function check() {
        if (root.commitPath()) walletController.refreshExternalSignerStatus()
    }

    FormRow {
        objectName: root.namePrefix + "PathRow"
        Layout.fillWidth: true
        enabled: root.editable
        bodySpacing: 0
        topPadding: 16
        bottomPadding: 16
        bodyItem: RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 16

            LabeledTextField {
                id: pathInput
                Layout.fillWidth: true
                // The section title is the visible label.
                accessibleName: root.title
                fieldObjectName: root.namePrefix + "PathInput"
                fieldHeight: Theme.size.control
                fieldBackgroundColor: Theme.color.surface2
                fieldTextStyle: Theme.text.monoDescription
                text: optionsModel.externalSignerPath
                //: Placeholder of the field for the external signer command path.
                placeholderText: qsTr("Enter external signer path")
                errorText: root.pathError
                supportingText: root.signerStatus.infoText || ""
                onEditingFinished: root.check()
            }

            PrimaryButton {
                objectName: root.namePrefix + "CheckDeviceButton"
                visible: root.showCheckButton
                Layout.preferredWidth: Math.max(140, implicitWidth)
                Layout.preferredHeight: Math.max(40, implicitHeight)
                Layout.alignment: Qt.AlignVCenter
                //: Button that saves the signer path and checks the command and the connected device.
                text: qsTr("Check device")
                textStyle: Theme.text.subheading
                enabled: root.pathError.length === 0 && root.editable
                onClicked: root.check()
            }
        }
    }

    ValueRow {
        objectName: root.namePrefix + "CommandRow"
        Layout.fillWidth: true
        showDivider: deviceRow.visible
        //: Title of the row showing whether the configured external signer command (such as HWI) runs and answers.
        title: qsTr("Signer command")
        value: {
            //: Signer command status: no signer path is configured.
            if (optionsModel.externalSignerPath.length === 0 && !root.pathChanged) return qsTr("Not set")
            //: Signer command status: the path was edited and has not been checked yet.
            if (!root.checked) return qsTr("Not checked")
            //: Signer command status: the command ran and returned a list of devices.
            if (root.commandResponding) return qsTr("Responding")
            //: Signer command status: the command could not run or did not return a list of devices.
            return qsTr("Not responding")
        }
        valueIconSource: !root.checked ? ""
            : root.commandResponding ? "image://images/check" : "image://images/cross-circle-filled"
        valueIconColor: root.commandResponding ? Theme.color.successText : Theme.color.dangerText
        errorText: root.checked && !root.commandResponding ? walletController.externalSignerError : ""
    }

    ValueRow {
        id: deviceRow
        objectName: root.namePrefix + "DeviceRow"
        Layout.fillWidth: true
        visible: root.commandResponding
        showDivider: false
        //: Title of the row showing which hardware device the external signer command found.
        title: qsTr("Device")
        value: {
            if (root.deviceReady) {
                return walletController.externalSignerName.length > 0
                    ? walletController.externalSignerName
                    //: Device status: one device was found, but it did not report a name.
                    : qsTr("Found")
            }
            //: Device status: a device was found but cannot be used yet, for example because it is locked.
            if (walletController.externalSignerError.length > 0) return qsTr("Not ready")
            //: Device status: the signer command works but found no device.
            return qsTr("None found")
        }
        valueIconSource: root.deviceReady ? "image://images/check"
            : walletController.externalSignerError.length > 0 ? "image://images/alert-filled" : ""
        valueIconColor: root.deviceReady ? Theme.color.successText : Theme.color.warningText
        errorText: !root.deviceReady ? walletController.externalSignerError : ""
        //: Shown when the external signer command works but no hardware device was found.
        supportingText: !root.deviceReady && walletController.externalSignerError.length === 0
            ? qsTr("Connect and unlock your device, then check again.") : ""
    }
}
