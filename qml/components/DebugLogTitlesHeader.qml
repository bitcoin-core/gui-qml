// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Control {
    id: root

    property int typeColumnWidth: 32
    property int timeColumnWidth: 80
    property int cornerRadius: 16

    implicitHeight: 44
    padding: 0

    background: Rectangle {
        objectName: "debugLogTitlesHeaderBackground"
        color: Theme.color.neutral3
        radius: root.cornerRadius

        Rectangle {
            objectName: "debugLogTitlesHeaderBottomFill"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.radius
            color: parent.color
        }
    }

    contentItem: RowLayout {
        spacing: 0

        Item { Layout.preferredWidth: 12 }

        CoreText {
            text: qsTr("Type")
            color: Theme.color.neutral7
            font.family: Theme.text.family
            font.pixelSize: 11
            fontStyleName: "Semi Bold"
            horizontalAlignment: Text.AlignLeft
            Layout.preferredWidth: root.typeColumnWidth
        }

        CoreText {
            text: qsTr("Time")
            color: Theme.color.neutral7
            font.family: Theme.text.family
            font.pixelSize: 11
            fontStyleName: "Semi Bold"
            horizontalAlignment: Text.AlignLeft
            Layout.preferredWidth: root.timeColumnWidth
        }

        CoreText {
            text: qsTr("Message")
            color: Theme.color.neutral7
            font.family: Theme.text.family
            font.pixelSize: 11
            fontStyleName: "Semi Bold"
            horizontalAlignment: Text.AlignLeft
            Layout.fillWidth: true
            Layout.minimumWidth: 0
        }

        Item { Layout.preferredWidth: 16 }
    }
}
