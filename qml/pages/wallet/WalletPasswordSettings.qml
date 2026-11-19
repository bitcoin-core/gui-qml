// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

SettingsPage {
    id: root
    objectName: "walletPasswordSettingsPage"

    required property bool updating
    property WalletQmlModel wallet: null
    readonly property WalletSecurityModel security: wallet ? wallet.security : null
    property SecurityOperationModel actionOperation: null
    property string admissionError: ""
    readonly property bool waitingForSave: actionOperation !== null && actionOperation.busy
    readonly property bool securityBusy: security !== null && security.activeOperation !== null && security.activeOperation.busy
    readonly property string errorText: admissionError.length > 0 ? admissionError
        : actionOperation && (actionOperation.phase === SecurityOperationModel.Failed || actionOperation.phase === SecurityOperationModel.Uncertain)
            ? actionOperation.error : ""
    readonly property string successText: actionOperation && actionOperation.phase === SecurityOperationModel.Succeeded
        && actionOperation.kind === SecurityOperationModel.ChangePassphrase ? qsTr("Password updated successfully.") : ""
    readonly property string resultText: successText.length > 0 ? successText : errorText
    readonly property bool resultSuccess: successText.length > 0

    signal saved()

    title: updating ? qsTr("Update password") : qsTr("Set password")
    backButtonObjectName: "walletPasswordBackButton"
    maximumContentWidth: 640
    contentSpacing: 24

    function clearField(field) {
        if (!field || field.text.length === 0) return
        field.text = Array(field.text.length + 1).join(" ")
        field.text = ""
    }

    function clearPasswordFields() {
        clearField(currentPassword)
        clearField(newPassword)
        clearField(confirmPassword)
    }

    function clearResult() {
        admissionError = ""
        if (actionOperation && !actionOperation.busy) actionOperation = null
    }

    function detachOperation() {
        if (actionOperation && actionOperation.canCancel) actionOperation.cancel()
        actionOperation = null
        admissionError = ""
        clearPasswordFields()
    }

    onWalletChanged: root.detachOperation()

    function focusInitialPasswordField() {
        const entry = root.updating ? currentPassword : newPassword
        if (root.visible && entry && entry.field) entry.field.forceActiveFocus()
    }

    Component.onCompleted: {
        root.clearResult()
        Qt.callLater(root.focusInitialPasswordField)
    }
    StackView.onActivated: Qt.callLater(root.focusInitialPasswordField)
    Component.onDestruction: root.detachOperation()
    onVisibleChanged: {
        if (!visible) root.detachOperation()
        else Qt.callLater(root.focusInitialPasswordField)
    }

    Connections {
        target: root.wallet
        function onAvailableChanged() {
            if (!root.wallet || !root.wallet.available) root.detachOperation()
        }
    }

    Connections {
        target: root.actionOperation
        function onChanged() {
            if (!root.actionOperation || root.actionOperation.phase !== SecurityOperationModel.Succeeded) return
            root.clearPasswordFields()
            if (root.actionOperation.kind === SecurityOperationModel.Encrypt) root.saved()
        }
    }

    PageHeading {
        objectName: "walletPasswordIntroText"
        Layout.fillWidth: true
        description: root.updating
            ? qsTr("Enter your current password, then choose a new password for this wallet.\n\nA secure password consists of ten or more random characters, or eight or more words.")
            : qsTr("The password encrypts the wallet on your hard drive and helps prevent unwanted access.\n\nA secure password consists of ten or more random characters, or eight or more words.\n\nSetting a password creates new receiving and change keys. Your earlier backups may not cover later payments. Make a fresh backup afterward and keep your older backups.")
    }

    FormSection {
        id: passwordSection
        enabled: root.wallet !== null && root.wallet.available && !root.securityBusy
        objectName: "walletPasswordFormSection"
        Layout.fillWidth: true

        // Qt 6.4 caches the checkbox's unwrapped height in nested layouts.
        // A positioner follows its height as the text wraps and scales.
        Column {
            Layout.fillWidth: true
            Layout.margins: passwordSection.contentInset
            spacing: 20

            PasswordTextField {
                id: currentPassword
                objectName: "walletPasswordCurrentEntry"
                visible: root.updating
                width: parent.width
                label: qsTr("Current password")
                labelObjectName: "walletPasswordCurrentLabel"
                fieldObjectName: "walletPasswordCurrentField"
                visibilityToggleObjectName: "walletPasswordCurrentVisibilityToggle"
                placeholderText: qsTr("Enter current password…")
                fieldBackgroundColor: Theme.color.surface2
                onTextEdited: root.clearResult()
                onAccepted: newPassword.field.forceActiveFocus()
            }

            PasswordTextField {
                id: newPassword
                dropsLineEndings: true
                objectName: "walletPasswordNewEntry"
                width: parent.width
                label: root.updating ? qsTr("New password") : qsTr("Choose a password")
                labelObjectName: "walletPasswordNewLabel"
                fieldObjectName: "walletPasswordNewField"
                visibilityToggleObjectName: "walletPasswordNewVisibilityToggle"
                placeholderText: root.updating ? qsTr("Enter new password…") : qsTr("Enter password…")
                fieldBackgroundColor: Theme.color.surface2
                onTextEdited: root.clearResult()
                onAccepted: confirmPassword.field.forceActiveFocus()
            }

            PasswordTextField {
                id: confirmPassword
                dropsLineEndings: true
                objectName: "walletPasswordConfirmEntry"
                width: parent.width
                label: root.updating ? qsTr("Confirm new password") : qsTr("Confirm password")
                labelObjectName: "walletPasswordConfirmLabel"
                fieldObjectName: "walletPasswordConfirmField"
                visibilityToggleObjectName: "walletPasswordConfirmVisibilityToggle"
                placeholderText: root.updating ? qsTr("Enter new password again…") : qsTr("Enter password again…")
                fieldBackgroundColor: Theme.color.surface2
                onTextEdited: root.clearResult()
            }

            // Consent in a form is a checkbox.
            CheckBox {
                id: acknowledgement
                objectName: "walletPasswordConfirmToggle"
                visible: !root.updating
                width: parent.width
                text: qsTr("I understand that if I lose or forget this password I might lose access to the bitcoin stored in this wallet.")
            }

            ToastBanner {
                objectName: "walletPasswordResultBanner"
                shown: root.resultText.length > 0
                width: parent.width
                tone: root.resultSuccess ? ToastBanner.Success : ToastBanner.Error
                text: root.resultText
                textObjectName: "walletPasswordErrorText"
            }

            PrimaryButton {
                objectName: "walletPasswordSaveButton"
                width: Math.min(320, parent.width)
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.updating ? qsTr("Update password") : qsTr("Set password")
                enabled: ApplicationModels.walletController.initialized
                    && root.wallet !== null && root.wallet.available && root.wallet.canManagePassphrase
                    && root.security !== null && (root.updating ? root.security.canChangePassphrase : root.security.canEncrypt)
                    && !root.securityBusy
                    && newPassword.text !== ""
                    && confirmPassword.text !== ""
                    && newPassword.text === confirmPassword.text
                    && (root.updating
                        ? currentPassword.text !== ""
                        : acknowledgement.checked)
                onClicked: {
                    root.clearResult()
                    root.actionOperation = root.updating ? root.security.beginPassphraseChange() : root.security.beginEncryption()
                    if (!root.actionOperation) {
                        root.admissionError = qsTr("The wallet operation could not be started.")
                        return
                    }
                    const admitted = root.actionOperation.submitCredentials(root.actionOperation.promptId,
                        currentPassword.text, newPassword.text, confirmPassword.text)
                    if (!admitted && root.actionOperation.canSubmit) {
                        root.actionOperation.cancel()
                        root.admissionError = qsTr("The wallet operation could not be started.")
                    }
                }
            }
        }
    }
}
