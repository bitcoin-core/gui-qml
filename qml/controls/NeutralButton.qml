// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

AbstractButton {
    id: root

    property url iconSource: ""
    property int iconSize: 18
    property var textStyle: Theme.text.subheading
    property color textColor: enabled ? Theme.color.neutral9 : Theme.color.neutral5
    property color backgroundColor: Theme.color.neutral2
    property color hoverBackgroundColor: Theme.color.neutral3

    implicitHeight: 40
    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    leftPadding: 16
    rightPadding: 16
    hoverEnabled: enabled && AppMode.isDesktop
    focusPolicy: Qt.StrongFocus
    scale: enabled && down ? 0.98 : 1
    Accessible.name: text

    Behavior on scale {
        NumberAnimation { duration: 100; easing.type: Easing.OutCubic }
    }

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight

        RowLayout {
            id: contentRow
            anchors.centerIn: parent
            spacing: root.text.length > 0 && root.iconSource.toString().length > 0 ? 8 : 0
            Icon {
                visible: root.iconSource.toString().length > 0
                source: root.iconSource
                size: root.iconSize
                color: root.textColor
            }
            CoreText {
                visible: root.text.length > 0
                text: root.text
                font: root.textStyle.font
                color: root.textColor
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }

    background: Rectangle {
        radius: 5
        color: root.hovered || root.down ? root.hoverBackgroundColor : root.backgroundColor
        border.width: root.visualFocus ? 2 : 0
        border.color: Theme.color.orange
        Behavior on color { ColorAnimation { duration: 150 } }
    }
}
