// Copyright (c) 2026 The Bitcoin Core developers
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

    implicitHeight: Math.max(72, content.implicitHeight + topPadding + bottomPadding)
    padding: 16
    opacity: enabled ? 1 : 0.4
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: title
    Accessible.description: description

    background: Rectangle {
        radius: 12
        color: root.down || root.hovered ? Theme.color.neutral3 : Theme.color.neutral2
        border.width: 0
        FocusBorder {
            visible: root.visualFocus
            borderRadius: 14
        }
    }

    contentItem: RowLayout {
        id: content
        spacing: 16

        Icon {
            source: root.iconSource
            color: Theme.color.neutral9
            size: 24
            Layout.alignment: Qt.AlignVCenter
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            CoreText {
                Layout.fillWidth: true
                text: root.title
                font: Theme.text.subheading.font
                color: Theme.color.neutral9
                horizontalAlignment: Text.AlignLeft
            }
            CoreText {
                Layout.fillWidth: true
                text: root.description
                font: Theme.text.caption.font
                color: Theme.color.neutral7
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
            }
        }
    }
}
