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
    property WalletModel wallet: null
    readonly property WalletSecurityInterface security: wallet ? wallet.security : null
    readonly property bool waitingForSave: password.saving
    readonly property bool securityBusy: security !== null && security.busy
    readonly property string errorText: password.errorText
    readonly property string successText: password.passwordUpdated ? qsTr("Password updated successfully.") : ""
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

    WalletPasswordController {
        id: password
        objectName: "walletPasswordController"
        security: root.security
        active: root.visible && root.wallet !== null && root.wallet.available
        onSucceeded: function(kind) {
            root.clearPasswordFields()
            if (kind === SecurityOperation.Encrypt) root.saved()
        }
    }

    onWalletChanged: root.clearPasswordFields()

    function focusInitialPasswordField() {
        const entry = root.updating ? currentPassword : newPassword
        if (root.visible && entry && entry.field) entry.field.forceActiveFocus()
    }

    Component.onCompleted: Qt.callLater(root.focusInitialPasswordField)
    StackView.onActivated: Qt.callLater(root.focusInitialPasswordField)
    Component.onDestruction: root.clearPasswordFields()
    onVisibleChanged: {
        if (!visible) root.clearPasswordFields()
        else Qt.callLater(root.focusInitialPasswordField)
    }

    Connections {
        target: root.wallet
        function onAvailableChanged() {
            if (!root.wallet || !root.wallet.available) root.clearPasswordFields()
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
                onTextEdited: password.clearResult()
                onAccepted: newPassword.field.forceActiveFocus()
            }

            PasswordTextField {
                id: newPassword
                objectName: "walletPasswordNewEntry"
                width: parent.width
                label: root.updating ? qsTr("New password") : qsTr("Choose a password")
                labelObjectName: "walletPasswordNewLabel"
                fieldObjectName: "walletPasswordNewField"
                visibilityToggleObjectName: "walletPasswordNewVisibilityToggle"
                placeholderText: root.updating ? qsTr("Enter new password…") : qsTr("Enter password…")
                fieldBackgroundColor: Theme.color.surface2
                onTextEdited: password.clearResult()
                onAccepted: confirmPassword.field.forceActiveFocus()
            }

            PasswordTextField {
                id: confirmPassword
                objectName: "walletPasswordConfirmEntry"
                width: parent.width
                label: root.updating ? qsTr("Confirm new password") : qsTr("Confirm password")
                labelObjectName: "walletPasswordConfirmLabel"
                fieldObjectName: "walletPasswordConfirmField"
                visibilityToggleObjectName: "walletPasswordConfirmVisibilityToggle"
                placeholderText: root.updating ? qsTr("Enter new password again…") : qsTr("Enter password again…")
                fieldBackgroundColor: Theme.color.surface2
                onTextEdited: password.clearResult()
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
                    if (root.updating) password.changePassphrase(currentPassword.text, newPassword.text, confirmPassword.text)
                    else password.encrypt(newPassword.text, confirmPassword.text)
                }
            }
        }
    }
}
