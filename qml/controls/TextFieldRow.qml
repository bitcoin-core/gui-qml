pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

FormRow {
    id: root

    property string fieldObjectName: ""
    property string text: ""
    property string placeholderText: ""
    property bool readOnly: false
    property var validator: null
    property int maximumLength: 32767
    property int inputMethodHints: Qt.ImhNone
    property int echoMode: TextInput.Normal
    property int fieldWidth: 180
    property int textAlignment: Text.AlignRight
    property color fieldColor: enabled ? Theme.color.neutral9 : Theme.color.neutral4
    property color placeholderColor: enabled ? Theme.color.neutral5 : Theme.color.neutral4
    property color focusBorderColor: Theme.color.orange
    property var fieldTextStyle: Theme.text.description
    readonly property var field: loadedTrailingItem

    signal textEdited(string text)
    signal editingFinished()
    signal accepted()

    trailingItem: TextField {
        id: input
        objectName: root.fieldObjectName.length > 0
            ? root.fieldObjectName
            : root.objectName.length > 0 ? root.objectName + "Field" : ""
        implicitWidth: root.fieldWidth
        implicitHeight: 32
        enabled: root.enabled
        readOnly: root.readOnly
        text: root.text
        placeholderText: root.placeholderText
        placeholderTextColor: root.placeholderColor
        validator: root.validator
        maximumLength: root.maximumLength
        inputMethodHints: root.inputMethodHints
        echoMode: root.echoMode
        selectByMouse: true
        leftPadding: 4
        rightPadding: 4
        color: root.fieldColor
        font: root.fieldTextStyle.font
        horizontalAlignment: root.textAlignment
        verticalAlignment: TextInput.AlignVCenter
        Accessible.name: root.title
        Accessible.description: root.description

        background: FocusBorder {
            visible: input.activeFocus
            border.color: root.focusBorderColor
            borderRadius: 6
            topMargin: -2
            bottomMargin: -2
            leftMargin: -2
            rightMargin: -2
        }

        onTextChanged: {
            if (root.text !== text) root.text = text
        }
        onTextEdited: root.textEdited(text)
        onEditingFinished: root.editingFinished()
        onAccepted: {
            root.accepted()
            input.focus = false
        }
    }
}
