// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

AlertPopup {
    id: root

    property string popupObjectName: ""
    property string titleText: qsTr("Enter wallet password")
    property string descriptionText: ""
    property string confirmText: qsTr("Continue")
    property string busyConfirmText: qsTr("Working…")
    property string passphraseFieldObjectName: ""
    property string cancelButtonObjectName: ""
    property string confirmButtonObjectName: ""
    readonly property bool canSubmit: !busy && passphraseField.text.length > 0

    signal submitted(string passphrase)

    objectName: popupObjectName
    title: titleText
    message: descriptionText
    initialFocusItem: passphraseField.field
    implicitWidth: Math.max(420, horizontalActionsWidth + 40)

    function clearPassphraseField() {
        if (passphraseField.text.length > 0) {
            passphraseField.text = Array(passphraseField.text.length + 1).join(" ")
            passphraseField.text = ""
        }
    }

    function submitPassphrase() {
        const passphrase = passphraseField.text
        clearPassphraseField()
        root.submitted(passphrase)
    }

    onAboutToShow: clearPassphraseField()
    onAboutToHide: clearPassphraseField()
    onClosed: clearPassphraseField()
    Component.onDestruction: clearPassphraseField()

    content: PasswordTextField {
        id: passphraseField
        Layout.fillWidth: true
        label: qsTr("Password")
        fieldObjectName: root.passphraseFieldObjectName
        placeholderText: qsTr("Enter password…")
        fieldBackgroundColor: Theme.color.surface2
    }

    AlertAction {
        text: qsTr("Cancel")
        role: AlertAction.Cancel
        enabled: !root.busy
        buttonObjectName: root.cancelButtonObjectName
    }

    AlertAction {
        text: root.busy ? root.busyConfirmText : root.confirmText
        enabled: root.canSubmit
        closesPopup: false
        buttonObjectName: root.confirmButtonObjectName
        onTriggered: root.submitPassphrase()
    }
}
