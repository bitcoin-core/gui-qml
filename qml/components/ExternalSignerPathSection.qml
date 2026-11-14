// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

// The signer path and what checking it found. Whether the signer command
// responds and whether a device was found are separate results: a working
// HWI with no device connected needs a different fix than a broken path.
FormSection {
    id: root

    // Prefix for the objectNames of the rows, so two instances stay apart.
    property string namePrefix: "externalSigner"
    //: Title of the file dialog for choosing the external signer executable, such as HWI.
    property string pathDialogTitle: qsTr("Select signer executable")

    // The field and Browse share one height, which grows with larger text
    // so neither label outgrows its control.
    readonly property int controlHeight: Math.round(Theme.size.control * Math.max(1, Theme.textScale))
    readonly property var signerStatus: (optionsModel.coreSettingStatuses || ({})).signer || ({})
    // The path and the check results live in externalSignerModel, shared by
    // every page that shows them; the section binds to it and forwards input.
    readonly property bool editable: externalSignerModel.canEdit
    readonly property bool canCheck: externalSignerModel.canCheck
    readonly property string pathText: externalSignerModel.draftPath
    readonly property string pathError: externalSignerModel.pathError
    readonly property bool checking: externalSignerModel.checking
    readonly property int status: externalSignerModel.status
    readonly property bool commandResponding: externalSignerModel.commandResponding
    readonly property bool deviceReady: externalSignerModel.deviceReady

    // The status icons' images pad their shapes differently: the cross fills
    // its image, the check and the alert leave a wide margin. Each gets the
    // box that shows its shape at the cross's size.
    readonly property int statusIconSize: 18
    function statusIconBox(source) {
        if (source.indexOf("check") >= 0) return Math.round(root.statusIconSize / 0.56)
        if (source.indexOf("alert-filled") >= 0) return Math.round(root.statusIconSize / 0.6)
        return root.statusIconSize
    }

    // Checks only run when asked for. The model saves an edited path first,
    // then runs the signer command off the GUI thread.
    function check() {
        externalSignerModel.check()
    }

    // A long path scrolls to its end after editing; show where it starts
    // whenever it isn't being edited.
    function showPathStart() {
        if (!pathInput.field.activeFocus) pathInput.field.cursorPosition = 0
    }

    function choosePath() {
        pathDialog.currentFolder = optionsModel.externalSignerPathFolder(externalSignerModel.draftPath)
        pathDialog.open()
    }

    Component.onCompleted: root.showPathStart()

    Connections {
        target: pathInput.field
        function onActiveFocusChanged() { root.showPathStart() }
    }

    // The path can change from elsewhere: another page, Browse, or the saved
    // path loading. Typing into the field replaces its binding, so follow
    // the model's change signal.
    Connections {
        target: externalSignerModel
        function onDraftPathChanged() {
            if (pathInput.text !== externalSignerModel.draftPath) pathInput.text = externalSignerModel.draftPath
            root.showPathStart()
        }
    }

    AppFileDialog {
        id: pathDialog
        objectName: root.namePrefix + "PathDialog"
        title: root.pathDialogTitle
        fileMode: AppFileDialog.OpenFile
        onAccepted: {
            if (selectedFile.toString().length === 0) return
            // A local path, not a file URL. Checking it is a separate step.
            externalSignerModel.draftPath = walletController.normalizeWalletPath(selectedFile.toString())
        }
    }

    FormRow {
        objectName: root.namePrefix + "PathRow"
        Layout.fillWidth: true
        // A check saves and runs the path it started with, so the path
        // stays as it is until the result is in.
        enabled: root.editable && !root.checking
        bodySpacing: 0
        // Browse sits beside the field it fills, at the same height. A plain
        // row never reflows, so narrowing the page only narrows the field.
        bodyItem: RowLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: Theme.space.m

            LabeledTextField {
                id: pathInput
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignTop
                // The section title is the visible label.
                accessibleName: root.title
                fieldObjectName: root.namePrefix + "PathInput"
                fieldHeight: root.controlHeight
                fieldBackgroundColor: Theme.color.surface(root.surfaceLevel + 2)
                fieldTextStyle: Theme.text.monoDescription
                text: externalSignerModel.draftPath
                // An example path, not an instruction: the section title names the field.
                placeholderText: Qt.platform.os === "windows"
                    //: Example path shown in the empty signer executable field on Windows. Keep it a file path.
                    ? qsTr("C:\\Downloads\\hwi.exe")
                    : Qt.platform.os === "osx"
                        //: Example path shown in the empty signer executable field on macOS. Keep it a file path.
                        ? qsTr("/Users/you/Downloads/hwi")
                        //: Example path shown in the empty signer executable field on Linux. Keep it a file path.
                        : qsTr("/home/you/Downloads/hwi")
                errorText: root.pathError
                supportingText: root.signerStatus.infoText || ""
                onTextChanged: externalSignerModel.draftPath = text
                onAccepted: root.check()
            }

            NeutralButton {
                objectName: root.namePrefix + "BrowseButton"
                Layout.alignment: Qt.AlignTop
                Layout.preferredHeight: root.controlHeight
                surfaceLevel: root.surfaceLevel + 1
                //: Opens a file dialog to choose the external signer executable.
                text: qsTr("Browse…")
                enabled: root.editable
                onClicked: root.choosePath()
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
            switch (root.status) {
            //: Signer command status: no signer path is configured.
            case ExternalSignerModel.NotSet: return qsTr("Not set")
            //: Signer command status while the signer command and the device are being checked.
            case ExternalSignerModel.Checking: return qsTr("Checking…")
            //: Signer command status: the path was edited and has not been checked yet.
            case ExternalSignerModel.NotChecked: return qsTr("Not checked")
            //: Signer command status: the command could not run or did not return a list of devices.
            case ExternalSignerModel.NotResponding: return qsTr("Not responding")
            //: Signer command status: the command ran and returned a list of devices.
            default: return qsTr("Responding")
            }
        }
        valueIconSource: root.commandResponding ? "image://images/check"
            : root.status === ExternalSignerModel.NotResponding ? "image://images/cross-circle-filled" : ""
        valueIconSize: root.statusIconBox(valueIconSource.toString())
        valueIconColor: root.commandResponding ? Theme.color.successText : Theme.color.dangerText
        errorText: root.status === ExternalSignerModel.NotResponding ? externalSignerModel.error : ""
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
            switch (root.status) {
            case ExternalSignerModel.Ready:
                return externalSignerModel.deviceName.length > 0
                    ? externalSignerModel.deviceName
                    //: Device status: one device was found, but it did not report a name.
                    : qsTr("Found")
            //: Device status: more than one hardware device is connected; only one can be used.
            case ExternalSignerModel.MultipleDevices: return qsTr("More than one")
            //: Device status: a device was found but cannot be used yet, for example because it is locked.
            case ExternalSignerModel.DeviceNotReady: return qsTr("Not ready")
            //: Device status: the signer command works but found no device.
            default: return qsTr("None found")
            }
        }
        // A missing, locked or extra device is fixed at the device, not in
        // the path, so it reads as guidance with a warning, not as an error.
        valueIconSource: root.deviceReady ? "image://images/check" : "image://images/alert-filled"
        valueIconSize: root.statusIconBox(valueIconSource.toString())
        valueIconColor: root.deviceReady ? Theme.color.successText : Theme.color.warningIcon
        supportingText: {
            if (root.deviceReady) return ""
            if (externalSignerModel.error.length > 0) return externalSignerModel.error
            //: Shown when the external signer command works but no hardware device was found.
            return qsTr("Connect and unlock your device, then check again.")
        }
    }
}
