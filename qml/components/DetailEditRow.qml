// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Item {
    id: root

    property string label: ""
    property string value: ""

    signal editClicked()

    Layout.fillWidth: true
    implicitHeight: col.implicitHeight + 20

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: editIcon.left
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 4

        CoreText {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            text: root.label
            font.pixelSize: 13
            color: Theme.color.neutral7
        }
        CoreText {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            text: root.value
            font.pixelSize: 18
            color: Theme.color.neutral9
            wrapMode: Text.WordWrap
        }
    }

    Icon {
        id: editIcon
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        source: "qrc:/icons/edit"
        color: Theme.color.neutral9
        size: 24
    }

    MouseArea {
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: 44
        height: 44
        cursorShape: Qt.PointingHandCursor
        onClicked: root.editClicked()
    }
}
