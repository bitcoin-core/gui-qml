// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

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
    property int fieldHeight: 32
    property int textAlignment: Text.AlignRight
    property color fieldColor: enabled ? Theme.color.neutral9 : Theme.color.disabledContent
    property color placeholderColor: enabled ? Theme.color.placeholder : Theme.color.disabledContent
    property color focusBorderColor: Theme.color.neutral6
    property color fieldBackgroundColor: "transparent"
    property var fieldTextStyle: Theme.text.description
    // Shown after the field, such as a unit button.
    property Component trailingAccessory: null
    readonly property alias field: input
    readonly property alias fieldFocusRing: focusRing
    readonly property alias loadedTrailingAccessory: accessoryLoader.item
    readonly property real trailingAccessoryWidth: accessoryLoader.visible ? accessoryLoader.width : 0

    // With tracksValue, the field shows `value` until the user edits it;
    // `modified` reports an edit, and reset() discards it.
    property bool tracksValue: false
    property string value: ""
    property bool modified: false
    function reset() {
        modified = false
        text = value
    }

    signal textEdited(string text)
    signal editingFinished()
    signal accepted()

    onValueChanged: if (tracksValue && !modified) text = value
    // Also covers a caller that assigned field.text and so replaced its binding.
    onTextChanged: if (input.text !== text) input.text = text
    Component.onCompleted: if (tracksValue) reset()

    trailingItem: RowLayout {
        spacing: Theme.space.xs

        TextField {
            id: input
            objectName: root.fieldObjectName.length > 0
                ? root.fieldObjectName
                : root.objectName.length > 0 ? root.objectName + "Field" : ""
            Layout.preferredWidth: root.fieldWidth
            implicitHeight: root.fieldHeight
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
            leftPadding: Theme.space.m
            rightPadding: Theme.space.m
            color: root.fieldColor
            font: root.fieldTextStyle.font
            horizontalAlignment: root.textAlignment
            verticalAlignment: TextInput.AlignVCenter
            Accessible.name: root.title
            Accessible.description: root.description

            background: Rectangle {
                color: input.readOnly ? "transparent" : root.fieldBackgroundColor
                radius: Theme.radius.s

                FieldBorderRings {
                    id: focusRing
                    anchors.fill: parent
                    visible: input.activeFocus
                    cornerRadius: Theme.radius.s
                    ringColor: root.focusBorderColor
                }
            }

            onTextChanged: {
                if (root.text !== text) root.text = text
            }
            onTextEdited: {
                if (root.tracksValue) root.modified = text !== root.value
                root.textEdited(text)
            }
            onEditingFinished: root.editingFinished()
            onAccepted: {
                root.accepted()
                input.focus = false
            }
        }

        Loader {
            id: accessoryLoader
            visible: active
            active: root.trailingAccessory !== null
            sourceComponent: root.trailingAccessory
        }
    }
}
