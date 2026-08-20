pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Control {
    id: root

    property string label: ""
    property string labelObjectName: ""
    property string fieldObjectName: ""
    property alias text: input.text
    property alias placeholderText: input.placeholderText
    property alias readOnly: input.readOnly
    property alias validator: input.validator
    property alias maximumLength: input.maximumLength
    property alias inputMethodHints: input.inputMethodHints
    property int echoMode: TextInput.Normal
    property string supportingText: ""
    property string errorText: ""
    property alias trailingItem: trailingLoader.sourceComponent
    readonly property alias field: input
    readonly property alias loadedTrailingItem: trailingLoader.item
    property int fieldHeight: 52
    property int fieldCornerRadius: 10
    property int fieldHorizontalPadding: 16
    property int labelSpacing: 6
    property int messageSpacing: 6
    property color fieldBackgroundColor: Theme.color.neutral1
    property color focusBorderColor: Theme.color.orange
    property color fieldTextColor: enabled ? Theme.color.neutral9 : Theme.color.neutral4
    property color placeholderColor: enabled ? Theme.color.neutral6 : Theme.color.neutral4
    property color labelColor: enabled ? Theme.color.neutral8 : Theme.color.neutral4
    property color supportingTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
    property color errorTextColor: enabled ? Theme.color.red : Theme.color.neutral4
    property var labelTextStyle: Theme.text.description
    property var fieldTextStyle: Theme.text.description
    property var messageTextStyle: Theme.text.caption

    signal textEdited(string text)
    signal editingFinished()
    signal accepted()

    Accessible.name: label
    Accessible.description: errorText.length > 0 ? errorText : supportingText
    padding: 0
    implicitWidth: Math.max(320, contentItem.implicitWidth)
    implicitHeight: contentItem.implicitHeight
    background: null

    contentItem: ColumnLayout {
        spacing: 0

        CoreText {
            objectName: root.labelObjectName.length > 0
                ? root.labelObjectName
                : root.objectName.length > 0 ? root.objectName + "Label" : ""
            visible: root.label.length > 0
            Layout.fillWidth: true
            Layout.bottomMargin: visible ? root.labelSpacing : 0
            text: root.label
            color: root.labelColor
            font: root.labelTextStyle.font
            lineHeight: root.labelTextStyle.lineHeight
            lineHeightMode: Text.FixedHeight
            horizontalAlignment: Text.AlignLeft
            wrap: false
            elide: Text.ElideRight
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.fieldHeight

            TextField {
                id: input
                objectName: root.fieldObjectName.length > 0
                    ? root.fieldObjectName
                    : root.objectName.length > 0 ? root.objectName + "Field" : ""
                anchors.fill: parent
                enabled: root.enabled
                echoMode: root.echoMode
                color: root.fieldTextColor
                placeholderTextColor: root.placeholderColor
                font: root.fieldTextStyle.font
                selectByMouse: true
                verticalAlignment: TextInput.AlignVCenter
                leftPadding: root.fieldHorizontalPadding
                rightPadding: trailingLoader.active
                    ? trailingLoader.width + root.fieldHorizontalPadding * 2
                    : root.fieldHorizontalPadding
                Accessible.name: root.label
                Accessible.description: root.errorText.length > 0 ? root.errorText : root.supportingText

                background: Rectangle {
                    color: root.fieldBackgroundColor
                    radius: root.fieldCornerRadius
                    border.width: input.activeFocus ? 2 : 0
                    border.color: root.focusBorderColor
                }

                onTextEdited: root.textEdited(text)
                onEditingFinished: root.editingFinished()
                onAccepted: {
                    root.accepted()
                    input.focus = false
                }
            }

            Loader {
                id: trailingLoader
                active: sourceComponent !== null
                visible: item !== null
                enabled: root.enabled
                anchors.right: parent.right
                anchors.rightMargin: root.fieldHorizontalPadding
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        CoreText {
            objectName: root.objectName.length > 0 ? root.objectName + "Message" : ""
            visible: root.errorText.length > 0 || root.supportingText.length > 0
            Layout.fillWidth: true
            Layout.topMargin: visible ? root.messageSpacing : 0
            text: root.errorText.length > 0 ? root.errorText : root.supportingText
            color: root.errorText.length > 0 ? root.errorTextColor : root.supportingTextColor
            font: root.messageTextStyle.font
            lineHeight: root.messageTextStyle.lineHeight
            lineHeightMode: Text.FixedHeight
            horizontalAlignment: Text.AlignLeft
            wrap: true
        }
    }
}
