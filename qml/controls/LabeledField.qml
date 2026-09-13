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
    property bool showCopyButton: false
    property bool copyButtonEnabled: true
    property string copyButtonObjectName: ""
    property string copyButtonText: qsTr("Copy")
    property string supportingText: ""
    property string errorText: ""
    property int labelSpacing: 6
    property int messageSpacing: 6
    property color labelColor: enabled ? Theme.color.neutral8 : Theme.color.neutral4
    property color supportingTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
    property color errorTextColor: enabled ? Theme.color.red : Theme.color.neutral4
    property var labelTextStyle: Theme.text.description
    property var messageTextStyle: Theme.text.caption

    signal copyRequested()

    Accessible.name: label
    Accessible.description: errorText.length > 0 ? errorText : supportingText
    padding: 0
    implicitWidth: Math.max(320, contentItem.implicitWidth)
    implicitHeight: contentItem.implicitHeight
    background: null

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            visible: root.label.length > 0 || root.showCopyButton
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
        }

        ColumnLayout {
            id: fieldContainer
            Layout.fillWidth: true
            spacing: 0
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
