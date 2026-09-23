// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Control {
    id: root

    default property alias fieldContent: fieldContainer.data
    property string label: ""
    property string labelObjectName: ""
    property string messageObjectName: ""
    property bool showCopyButton: false
    property bool copyButtonEnabled: true
    property string copyButtonObjectName: ""
    property string copyButtonText: qsTr("Copy")
    property string actionText: ""
    property url actionIconSource: ""
    property string actionObjectName: ""
    property string supportingText: ""
    property string errorText: ""
    property int labelSpacing: 6
    property int messageSpacing: 6
    property color labelColor: enabled ? Theme.color.neutral8 : Theme.color.neutral4
    property color supportingTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
    property color errorTextColor: Theme.color.red
    property var labelTextStyle: Theme.text.description
    property var messageTextStyle: Theme.text.caption
    property bool fieldSurface: false
    property bool fieldFocused: false
    property color fieldBackgroundColor: Theme.color.neutral2
    property color focusBorderColor: Theme.color.neutral6
    property color errorBorderColor: Theme.color.red
    readonly property color activeBorderColor: errorText.length > 0 ? errorBorderColor : focusBorderColor
    readonly property bool showFieldBorder: fieldSurface && (errorText.length > 0 || fieldFocused)
    property int fieldCornerRadius: 10
    property int fieldHorizontalPadding: 14

    signal copyRequested()
    signal actionRequested()

    Accessible.name: label
    Accessible.description: errorText.length > 0 ? errorText : supportingText
    padding: 0
    implicitWidth: Math.max(320, contentItem.implicitWidth)
    implicitHeight: contentItem.implicitHeight
    background: null

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            visible: root.label.length > 0 || root.showCopyButton || root.actionText.length > 0
            Layout.fillWidth: true
            Layout.bottomMargin: visible ? root.labelSpacing : 0
            spacing: 8

            CoreText {
                objectName: root.labelObjectName.length > 0
                    ? root.labelObjectName
                    : root.objectName.length > 0 ? root.objectName + "Label" : ""
                Layout.fillWidth: true
                text: root.label
                color: root.labelColor
                font: root.labelTextStyle.font
                lineHeight: root.labelTextStyle.lineHeight
                lineHeightMode: Text.FixedHeight
                horizontalAlignment: Text.AlignLeft
                wrap: false
                elide: Text.ElideRight
            }

            CopyButton {
                id: copyButton
                objectName: root.copyButtonObjectName.length > 0
                    ? root.copyButtonObjectName
                    : root.objectName.length > 0 ? root.objectName + "CopyButton" : ""
                visible: root.showCopyButton
                enabled: root.enabled && root.copyButtonEnabled
                copyText: root.copyButtonText
                onCopyRequested: root.copyRequested()
            }

            LinkButton {
                objectName: root.actionObjectName.length > 0
                    ? root.actionObjectName
                    : root.objectName.length > 0 ? root.objectName + "ActionButton" : ""
                visible: root.actionText.length > 0
                enabled: root.enabled
                text: root.actionText
                iconSource: root.actionIconSource
                onClicked: root.actionRequested()
            }
        }

        Item {
            Layout.fillWidth: true
            implicitHeight: fieldContainer.implicitHeight

            Rectangle {
                objectName: root.objectName.length > 0 ? root.objectName + "Surface" : ""
                anchors.fill: parent
                visible: root.fieldSurface
                radius: root.fieldCornerRadius
                color: root.fieldBackgroundColor
            }

            FieldBorderRings {
                anchors.fill: parent
                visible: root.showFieldBorder
                cornerRadius: root.fieldCornerRadius
                ringColor: root.activeBorderColor
                outerObjectName: root.objectName.length > 0 ? root.objectName + "OuterBorder" : ""
                innerObjectName: root.objectName.length > 0 ? root.objectName + "FieldBorder" : ""
            }

            ColumnLayout {
                id: fieldContainer
                anchors.fill: parent
                anchors.leftMargin: root.fieldSurface ? root.fieldHorizontalPadding : 0
                anchors.rightMargin: root.fieldSurface ? root.fieldHorizontalPadding : 0
                spacing: 0
            }
        }

        CoreText {
            objectName: root.messageObjectName.length > 0
                ? root.messageObjectName
                : root.objectName.length > 0 ? root.objectName + "Message" : ""
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
