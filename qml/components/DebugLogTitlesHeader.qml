// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Control {
    id: root

    property int typeColumnWidth: 48
    property int timeColumnWidth: Math.max(80, Math.ceil(timeMetrics.advanceWidth("00:00:00")) + Theme.space.m)
    property FontMetrics timeMetrics: FontMetrics { font: Theme.text.monoCaption.font }
    property int cornerRadius: Theme.radius.card

    implicitHeight: 44
    padding: 0

    background: Rectangle {
        objectName: "debugLogTitlesHeaderBackground"
        color: Theme.color.surface1
        radius: root.cornerRadius

        Rectangle {
            objectName: "debugLogTitlesHeaderBottomFill"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: parent.radius
            color: parent.color
        }

        Separator {
            objectName: "debugLogTitlesHeaderDivider"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
        }
    }

    contentItem: RowLayout {
        spacing: 0

        Item { Layout.preferredWidth: 12 }

        CoreText {
            text: qsTr("Type")
            color: Theme.color.neutral7
            textStyle: Theme.text.captionStrong
            wrap: false
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignLeft
            Layout.preferredWidth: root.typeColumnWidth
        }

        CoreText {
            text: qsTr("Time")
            color: Theme.color.neutral7
            textStyle: Theme.text.captionStrong
            wrap: false
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignLeft
            Layout.preferredWidth: root.timeColumnWidth
        }

        CoreText {
            text: qsTr("Message")
            color: Theme.color.neutral7
            textStyle: Theme.text.captionStrong
            wrap: false
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignLeft
            Layout.fillWidth: true
            Layout.minimumWidth: 0
        }

        Item { Layout.preferredWidth: 16 }
    }
}
