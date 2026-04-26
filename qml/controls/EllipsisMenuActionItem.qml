// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

AbstractButton {
    id: root

    property url leftIconSource
    property color bgDefaultColor: "transparent"
    property color bgHoverColor: Theme.color.neutral2
    property color textColor: Theme.color.neutral7
    property color textHoverColor: Theme.color.neutral9

    hoverEnabled: AppMode.isDesktop
    padding: 0

    implicitWidth: 280
    implicitHeight: 33

    MouseArea {
        anchors.fill: parent
        enabled: false
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 5
        spacing: 5

        RowLayout {
            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true
            spacing: 7

            Item {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18

                Icon {
                    anchors.centerIn: parent
                    source: root.leftIconSource
                    color: root.hovered ? root.textHoverColor : root.textColor
                    size: 18
                }
            }

            CoreText {
                id: buttonText
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: root.text
                horizontalAlignment: Text.AlignLeft
                font.pixelSize: 15
                color: root.hovered ? root.textHoverColor : root.textColor
            }
        }

        Item {
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18
        }
    }

    background: Rectangle {
        color: root.hovered ? root.bgHoverColor : root.bgDefaultColor
        radius: 0

        Behavior on color {
            ColorAnimation { duration: 150 }
        }
    }
}
