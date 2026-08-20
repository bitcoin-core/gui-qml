pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

LabeledField {
    id: root

    property string fieldObjectName: ""
    property alias text: input.text
    property alias placeholderText: input.placeholderText
    property alias readOnly: input.readOnly
    property alias inputMethodHints: input.inputMethodHints
    property alias wrapMode: input.wrapMode
    readonly property alias field: input
    property int fieldHeight: 132
    property int fieldCornerRadius: 10
    property int fieldPadding: 16
    property color fieldBackgroundColor: Theme.color.neutral1
    property color focusBorderColor: Theme.color.orange
    property color fieldTextColor: enabled ? Theme.color.neutral9 : Theme.color.neutral4
    property color placeholderColor: enabled ? Theme.color.neutral6 : Theme.color.neutral4
    property var fieldTextStyle: Theme.text.description

    signal textEdited(string text)

    TextArea {
        id: input
        objectName: root.fieldObjectName.length > 0
            ? root.fieldObjectName
            : root.objectName.length > 0 ? root.objectName + "Field" : ""
        Layout.fillWidth: true
        Layout.preferredHeight: root.fieldHeight
        enabled: root.enabled
        color: root.fieldTextColor
        placeholderTextColor: root.placeholderColor
        font: root.fieldTextStyle.font
        selectByMouse: true
        wrapMode: TextEdit.Wrap
        leftPadding: root.fieldPadding
        rightPadding: root.fieldPadding
        topPadding: root.fieldPadding
        bottomPadding: root.fieldPadding
        Accessible.name: root.label
        Accessible.description: root.errorText.length > 0 ? root.errorText : root.supportingText

        background: Rectangle {
            color: root.fieldBackgroundColor
            radius: root.fieldCornerRadius
            border.width: input.activeFocus ? 2 : 0
            border.color: root.focusBorderColor
        }

        onTextChanged: root.textEdited(text)
    }
}
