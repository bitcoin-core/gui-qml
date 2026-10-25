// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Control {
    id: root

    property string timestamp: ""
    property string message: ""
    property bool isError: false
    property bool isWarning: false
    property bool alternate: false

    property int typeColumnWidth: 48
    property int timeColumnWidth: 80

    readonly property color indicatorColor: isError
        ? Theme.color.danger
        : isWarning ? Theme.color.warning : "transparent"
    readonly property string typeLabel: isError
        ? qsTr("Error")
        : isWarning ? qsTr("Warning") : qsTr("Regular")

    Accessible.role: Accessible.ListItem
    Accessible.name: typeLabel + " " + timestamp + " " + message

    implicitHeight: Math.max(48, messageText.contentHeight + 24)
    padding: 0

    background: Rectangle {
        color: root.alternate ? Theme.color.surface2 : Theme.color.surface1

        Behavior on color { enabled: Theme.motion.animated; ColorAnimation { duration: Theme.motion.normal } }
    }

    contentItem: RowLayout {
        spacing: 0

        Item { Layout.preferredWidth: 12 }

        Item {
            Layout.preferredWidth: root.typeColumnWidth
            Layout.fillHeight: true

            Rectangle {
                id: typeIndicator
                objectName: root.objectName.length > 0 ? root.objectName + "TypeIndicator" : ""
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 8
                radius: width / 2
                color: root.indicatorColor
            }
        }

        CoreText {
            id: timeText
            objectName: root.objectName.length > 0 ? root.objectName + "Time" : ""
            text: root.timestamp.length > 0 ? root.timestamp : "—"
            color: Theme.color.neutral7
            textStyle: Theme.text.monoCaption
            wrap: false
            horizontalAlignment: Text.AlignLeft
            elide: Text.ElideRight
            Layout.preferredWidth: root.timeColumnWidth
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 14
        }

        TextEdit {
            id: messageText
            objectName: root.objectName.length > 0 ? root.objectName + "Message" : ""
            text: root.message
            readOnly: true
            selectByMouse: true
            persistentSelection: false
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: Theme.color.neutral9
            selectionColor: Theme.color.selection
            selectedTextColor: Theme.color.selectedText
            font: Theme.text.monoCaption.font
            horizontalAlignment: Text.AlignLeft
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 14
        }

        Item { Layout.preferredWidth: 16 }
    }
}
