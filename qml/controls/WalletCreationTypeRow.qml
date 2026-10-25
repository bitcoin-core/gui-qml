// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

// A choice tile: one card per choice, like OptionButton.
AbstractButton {
    id: root
    property string title: ""
    property string description: ""
    property string iconSource: ""

    implicitHeight: Math.max(72, content.implicitHeight + topPadding + bottomPadding)
    padding: Theme.space.l
    opacity: enabled ? 1 : 0.4
    hoverEnabled: enabled && AppMode.isDesktop
    focusPolicy: Qt.StrongFocus
    Accessible.name: title
    Accessible.description: description

    HoverHandler {
        enabled: root.enabled && AppMode.isDesktop
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        radius: Theme.radius.cardInset
        color: Qt.tint(Theme.color.surface2, root.down ? Theme.color.surfacePressed
            : root.hovered ? Theme.color.surfaceHover : "transparent")
        border.width: 0

        Behavior on color {
            enabled: Theme.motion.animated
            ColorAnimation { duration: Theme.motion.normal }
        }

        FocusBorder {
            visible: root.visualFocus
            targetRadius: parent.radius
        }
    }

    contentItem: RowLayout {
        id: content
        spacing: Theme.space.l

        Icon {
            source: root.iconSource
            color: Theme.color.neutral9
            size: Theme.size.icon.xl
            Layout.alignment: Qt.AlignVCenter
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space.xxs
            CoreText {
                Layout.fillWidth: true
                text: root.title
                textStyle: Theme.text.subheading
                color: Theme.color.neutral9
                horizontalAlignment: Text.AlignLeft
            }
            CoreText {
                Layout.fillWidth: true
                text: root.description
                textStyle: Theme.text.caption
                color: Theme.color.neutral7
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
            }
        }
    }
}
