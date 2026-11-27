// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

LabeledTextField {
    id: root

    property bool passwordVisible: false
    property string visibilityToggleObjectName: root.objectName.length > 0
        ? root.objectName + "VisibilityToggle"
        : ""

    // For a new passphrase only. A line ending cannot be typed into this
    // field, so one that arrives by paste is dropped instead of becoming a
    // character the user cannot see or type again. Fields that unlock an
    // existing wallet leave this off: a wallet encrypted with a pasted line
    // ending must still open with exactly that text.
    property bool dropsLineEndings: false

    function dropLineEndings() {
        const text = root.field.text
        if (!root.dropsLineEndings || !/[\r\n]/.test(text)) return
        const caret = text.slice(0, root.field.cursorPosition).replace(/[\r\n]/g, "").length
        root.field.text = text.replace(/[\r\n]/g, "")
        root.field.cursorPosition = caret
    }

    Connections {
        target: root.field
        function onTextChanged() { root.dropLineEndings() }
    }
    onDropsLineEndingsChanged: dropLineEndings()

    echoMode: passwordVisible ? TextInput.Normal : TextInput.Password
    // Qt never exposes a name for a password field, so the label also goes
    // into the description, which VoiceOver still reads.
    accessibleDescription: [label, errorText.length > 0 ? errorText : supportingText]
        .filter(function(part) { return part.length > 0 }).join(". ")
    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText

    trailingItem: IconButton {
        id: visibilityToggle
        objectName: root.visibilityToggleObjectName
        size: 30
        iconSize: 22
        iconSource: root.passwordVisible ? "qrc:/icons/hidden" : "qrc:/icons/visible"
        iconColor: Theme.color.neutral7
        activeColor: Theme.color.neutral9
        enabled: root.enabled
        // Clicking keeps focus in the field; Tab still reaches the toggle.
        focusPolicy: Qt.TabFocus
        accessibleName: root.passwordVisible ? qsTr("Hide password") : qsTr("Show password")
        background: null
        onClicked: root.passwordVisible = !root.passwordVisible
    }
}
