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
    property WalletQmlModel wallet: walletController.selectedWallet
    property string errorText: ""
    property string successText: ""
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
        root.errorText = ""
        root.successText = ""
    }

    function handleSaveResult(ok) {
        if (ok) {
            root.errorText = ""
            root.clearPasswordFields()
            if (root.updating) {
                root.successText = qsTr("Password updated successfully.")
            } else {
                if (acknowledgement.loadedTrailingItem) {
                    acknowledgement.loadedTrailingItem.checked = false
                }
                root.saved()
            }
        } else {
            root.successText = ""
            root.errorText = root.wallet ? root.wallet.settingsError : ""
            if (!root.updating) {
                root.clearPasswordFields()
            }
        }
    }

    function focusInitialPasswordField() {
        const entry = root.updating ? currentPassword : newPassword
        if (root.visible && entry && entry.field) entry.field.forceActiveFocus()
    }

    Component.onCompleted: {
        if (root.wallet) root.wallet.clearSettingsError()
        root.clearResult()
        Qt.callLater(root.focusInitialPasswordField)
    }
    StackView.onActivated: Qt.callLater(root.focusInitialPasswordField)
    Component.onDestruction: root.clearPasswordFields()
    onVisibleChanged: {
        if (!visible) {
            root.clearPasswordFields()
        } else {
            Qt.callLater(root.focusInitialPasswordField)
        }
    }

    Connections {
        target: root.wallet

        function onSettingsErrorChanged() {
            root.successText = ""
            root.errorText = root.wallet ? root.wallet.settingsError : ""
        }
    }

    PageHeading {
        objectName: "walletPasswordIntroText"
        Layout.fillWidth: true
        description: root.updating
            ? qsTr("Enter your current password, then choose a new password for this wallet.\n\nA secure password consists of ten or more random characters, or eight or more words.")
            : qsTr("The password encrypts the wallet on your hard drive and helps prevent unwanted access.\n\nA secure password consists of ten or more random characters, or eight or more words.")
    }

    FormSection {
        objectName: "walletPasswordFormSection"
        Layout.fillWidth: true

        ColumnLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            spacing: 20

            PasswordTextField {
                id: currentPassword
                objectName: "walletPasswordCurrentEntry"
                visible: root.updating
                Layout.fillWidth: true
                label: qsTr("Current password")
                labelObjectName: "walletPasswordCurrentLabel"
                fieldObjectName: "walletPasswordCurrentField"
                visibilityToggleObjectName: "walletPasswordCurrentVisibilityToggle"
                placeholderText: qsTr("Enter current password...")
                fieldBackgroundColor: Theme.color.neutral2
                onTextEdited: root.clearResult()
                onAccepted: newPassword.field.forceActiveFocus()
            }

            PasswordTextField {
                id: newPassword
                objectName: "walletPasswordNewEntry"
                Layout.fillWidth: true
                label: root.updating ? qsTr("New password") : qsTr("Choose a password")
                labelObjectName: "walletPasswordNewLabel"
                fieldObjectName: "walletPasswordNewField"
                visibilityToggleObjectName: "walletPasswordNewVisibilityToggle"
                placeholderText: root.updating ? qsTr("Enter new password...") : qsTr("Enter password...")
                fieldBackgroundColor: Theme.color.neutral2
                onTextEdited: root.clearResult()
                onAccepted: confirmPassword.field.forceActiveFocus()
            }

            PasswordTextField {
                id: confirmPassword
                objectName: "walletPasswordConfirmEntry"
                Layout.fillWidth: true
                label: root.updating ? qsTr("Confirm new password") : qsTr("Confirm password")
                labelObjectName: "walletPasswordConfirmLabel"
                fieldObjectName: "walletPasswordConfirmField"
                visibilityToggleObjectName: "walletPasswordConfirmVisibilityToggle"
                placeholderText: root.updating ? qsTr("Enter new password again...") : qsTr("Enter password again...")
                fieldBackgroundColor: Theme.color.neutral2
                onTextEdited: root.clearResult()
            }

            ListRow {
                id: acknowledgement
                objectName: "walletPasswordConfirmToggle"
                visible: !root.updating
                Layout.fillWidth: true
                description: qsTr("I understand that if I lose or forget this password I might lose access to the bitcoin stored in this wallet.")
                showDivider: false
                accessibleRole: Accessible.CheckBox
                trailingItem: OptionSwitch { }
                onClicked: loadedTrailingItem.toggle()
            }

            ToastBanner {
                objectName: "walletPasswordResultBanner"
                visible: root.resultText.length > 0
                Layout.fillWidth: true
                readonly property color resultColor: root.resultSuccess ? Theme.color.green : Theme.color.red
                backgroundColor: Qt.rgba(resultColor.r, resultColor.g, resultColor.b, 0.25)
                iconSource: root.resultSuccess ? "image://images/check" : "image://images/info-filled"
                iconColor: resultColor
                text: root.resultText
                textObjectName: "walletPasswordErrorText"
                textColor: resultColor
            }

            ContinueButton {
                objectName: "walletPasswordSaveButton"
                Layout.preferredWidth: Math.min(320, parent.width)
                Layout.alignment: Qt.AlignHCenter
                text: root.updating ? qsTr("Update password") : qsTr("Set password")
                enabled: walletController.initialized
                    && newPassword.text !== ""
                    && confirmPassword.text !== ""
                    && newPassword.text === confirmPassword.text
                    && (root.updating
                        ? currentPassword.text !== ""
                        : acknowledgement.loadedTrailingItem && acknowledgement.loadedTrailingItem.checked)
                onClicked: {
                    root.clearResult()
                    const ok = root.updating
                        ? root.wallet.changeWalletPassphrase(currentPassword.text, newPassword.text)
                        : root.wallet.encryptWallet(newPassword.text)
                    root.handleSaveResult(ok)
                }
            }
        }
    }
}
