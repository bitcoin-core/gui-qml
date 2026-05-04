// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

AbstractButton {
    id: root

    property string title: ""
    property string description: ""
    property string iconSource: ""
    property color iconColor: Theme.color.neutral9

    padding: 15
    implicitWidth: 450
    opacity: enabled ? 1.0 : 0.4

    background: Rectangle {
        border.width: 1
        border.color: root.hovered && root.enabled ? Theme.color.neutral9 : Theme.color.neutral5
        radius: 10
        color: "transparent"
        FocusBorder {
            visible: root.visualFocus
            borderRadius: 14
        }
    }

    contentItem: RowLayout {
        spacing: 10
        Icon {
            source: root.iconSource
            color: root.iconColor
            size: 24
        }
        ColumnLayout {
            spacing: 2
            Layout.fillWidth: true
            CoreText {
                Layout.fillWidth: true
                text: root.title
                font.pixelSize: 18
                bold: true
                color: Theme.color.neutral9
                horizontalAlignment: Text.AlignLeft
            }
            CoreText {
                Layout.fillWidth: true
                text: root.description
                font.pixelSize: 15
                color: Theme.color.neutral7
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
            }
        }
        CaretRightIcon {
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
