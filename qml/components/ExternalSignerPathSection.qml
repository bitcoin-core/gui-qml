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
    readonly property bool editable: root.signerStatus.canEdit !== false
    readonly property string pathText: pathInput.text
    readonly property string pathError: optionsModel.externalSignerPathValidationError(pathInput.text)
    readonly property bool pathChanged: pathInput.text.trim() !== optionsModel.externalSignerPath
    // True from a check request until its result is in.
    property bool checking: false
    // The status below describes the path in the field.
    readonly property bool checked: !root.pathChanged && walletController.externalSignerPathConfigured
    readonly property bool commandResponding: root.checked && !root.checking
        && walletController.externalSignerResponding
    readonly property bool deviceReady: root.commandResponding && walletController.canCreateExternalSignerWallet

    // The status icons' images pad their shapes differently: the cross fills
    // its image, the check and the alert leave a wide margin. Each gets the
    // box that shows its shape at the cross's size.
    readonly property int statusIconSize: 18
    function statusIconBox(source) {
        if (source.indexOf("check") >= 0) return Math.round(root.statusIconSize / 0.56)
        if (source.indexOf("alert-filled") >= 0) return Math.round(root.statusIconSize / 0.6)
        return root.statusIconSize
    }

    function commitPath() {
        if (root.pathError.length > 0) return false
        const path = pathInput.text.trim()
        if (path !== optionsModel.externalSignerPath) optionsModel.externalSignerPath = path
        // The model normalizes the path; show what it stored.
        pathInput.text = optionsModel.externalSignerPath
        root.showPathStart()
        return true
    }

    // Checks only run when asked for. The results clear for a moment first,
    // so a check that finds the same thing still visibly happened.
    function check() {
        if (root.checking || !root.commitPath()) return
        root.checking = true
        checkTimer.restart()
    }

    // A long path scrolls to its end after editing; show where it starts
    // whenever it isn't being edited.
    function showPathStart() {
        if (!pathInput.field.activeFocus) pathInput.field.cursorPosition = 0
    }

    function choosePath() {
        pathDialog.currentFolder = optionsModel.externalSignerPathFolder(pathInput.text)
        pathDialog.open()
    }

    Component.onCompleted: root.showPathStart()

    Timer {
        id: checkTimer
        interval: Theme.motion.animated ? 300 : 0
        onTriggered: {
            walletController.refreshExternalSignerStatus()
            root.checking = false
        }
    }
    // The path is saved when the check starts. A check still waiting when the
    // section goes away runs now, so the next section doesn't show the old
    // path's result as the new path's.
    Component.onDestruction: if (checkTimer.running) walletController.refreshExternalSignerStatus()

    Connections {
        target: pathInput.field
        function onActiveFocusChanged() { root.showPathStart() }
    }

    AppFileDialog {
        id: pathDialog
        objectName: root.namePrefix + "PathDialog"
        title: root.pathDialogTitle
        fileMode: AppFileDialog.OpenFile
        onAccepted: {
            if (selectedFile.toString().length === 0) return
            // A local path, not a file URL. Checking it is a separate step.
            pathInput.text = walletController.normalizeWalletPath(selectedFile.toString())
            root.showPathStart()
        }
    }

    FormRow {
        objectName: root.namePrefix + "PathRow"
        Layout.fillWidth: true
        enabled: root.editable
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
                text: optionsModel.externalSignerPath
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
            //: Signer command status: no signer path is configured.
            if (optionsModel.externalSignerPath.length === 0 && !root.pathChanged) return qsTr("Not set")
            //: Signer command status while the signer command and the device are being checked.
            if (root.checking) return qsTr("Checking…")
            //: Signer command status: the path was edited and has not been checked yet.
            if (!root.checked) return qsTr("Not checked")
            //: Signer command status: the command ran and returned a list of devices.
            if (root.commandResponding) return qsTr("Responding")
            //: Signer command status: the command could not run or did not return a list of devices.
            return qsTr("Not responding")
        }
        valueIconSource: !root.checked || root.checking ? ""
            : root.commandResponding ? "image://images/check" : "image://images/cross-circle-filled"
        valueIconSize: root.statusIconBox(valueIconSource.toString())
        valueIconColor: root.commandResponding ? Theme.color.successText : Theme.color.dangerText
        errorText: root.checked && !root.checking && !root.commandResponding
            ? walletController.externalSignerError : ""
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
            //: Device status: more than one hardware device is connected; only one can be used.
            if (walletController.externalSignerCount > 1) return qsTr("More than one")
            //: Device status: a device was found but cannot be used yet, for example because it is locked.
            if (walletController.externalSignerError.length > 0) return qsTr("Not ready")
            //: Device status: the signer command works but found no device.
            return qsTr("None found")
        }
        // A missing, locked or extra device is fixed at the device, not in
        // the path, so it reads as guidance with a warning, not as an error.
        valueIconSource: root.deviceReady ? "image://images/check" : "image://images/alert-filled"
        valueIconSize: root.statusIconBox(valueIconSource.toString())
        valueIconColor: root.deviceReady ? Theme.color.successText : Theme.color.warningText
        supportingText: {
            if (root.deviceReady) return ""
            if (walletController.externalSignerError.length > 0) return walletController.externalSignerError
            //: Shown when the external signer command works but no hardware device was found.
            return qsTr("Connect and unlock your device, then check again.")
        }
    }
}
