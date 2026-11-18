// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
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
    // False once the wallet, draft or operation this prompt answers is gone.
    property bool contextValid: true
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
        // A repeated failure can report the same error text again.
        errorDismissed = false
        root.submitted(passphrase)
    }

    // Closing can change the state the check depends on; defer it so the
    // change notification can finish.
    // The prompt can be destroyed before the deferred call runs, for example
    // when its wallet view goes away; then there is nothing left to close.
    // A bound prompt closes with its owner's state instead.
    onContextValidChanged: if (!contextValid && !bound) Qt.callLater(function() { if (root && !root.contextValid) root.close() })
    onAboutToShow: {
        clearPassphraseField()
        errorDismissed = false
    }
    onAboutToHide: clearPassphraseField()
    onClosed: clearPassphraseField()

    content: PasswordTextField {
        id: passphraseField
        // The popup's JavaScript methods can already be gone during
        // destruction on older Qt versions. Wipe the field itself.
        Component.onDestruction: {
            if (text.length > 0) {
                text = Array(text.length + 1).join(" ")
                text = ""
            }
        }
        Layout.fillWidth: true
        label: qsTr("Password")
        fieldObjectName: root.passphraseFieldObjectName
        placeholderText: qsTr("Enter password…")
        fieldBackgroundColor: Theme.color.surface2
        onTextEdited: if (root.errorText.length > 0) root.errorDismissed = true
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
