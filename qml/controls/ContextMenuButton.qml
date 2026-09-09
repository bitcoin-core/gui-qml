// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

AbstractButton {
    id: root

    enum Role {
        Normal,
        Destructive
    }

    property url iconSource
    property int role: ContextMenuButton.Normal
    property bool autoClose: true
    property color hoverBackgroundColor: Theme.color.neutral3

    readonly property bool _destructive: role === ContextMenuButton.Destructive
    readonly property bool _highlighted: enabled && (hovered || down || visualFocus)
    readonly property color _idleColor: _destructive ? Theme.color.red : Theme.color.neutral8
    readonly property color _hoverColor: _destructive ? Theme.color.red : Theme.color.neutral9

    signal triggered()

    Accessible.role: Accessible.MenuItem
    Accessible.name: text
    hoverEnabled: AppMode.isDesktop
    focusPolicy: Qt.StrongFocus
    leftPadding: 10
    rightPadding: 8
    topPadding: 0
    bottomPadding: 0
    opacity: enabled ? 1.0 : 0.5

    Layout.fillWidth: true
    Layout.preferredHeight: 36
    Layout.minimumHeight: 36
    implicitHeight: 36

    onClicked: {
        if (root.autoClose) root._closeEnclosingMenu()
        root.triggered()
    }

    function _closeEnclosingMenu() {
        let p = root.parent
        while (p) {
            // qmllint disable missing-property
            if (p["_contextMenuMarker"] === true && typeof p["_closeMenu"] === "function") {
                p["_closeMenu"]()
                return
            }
            // qmllint enable missing-property
            p = p.parent
        }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: RowLayout {
        id: _row
        spacing: 7

        Item {
            visible: root.iconSource.toString() !== ""
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 18
            Layout.preferredHeight: 18

            Icon {
                anchors.centerIn: parent
                source: root.iconSource
                color: root._highlighted ? root._hoverColor : root._idleColor
                size: 18
            }
        }

        CoreText {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            text: root.text
            horizontalAlignment: Text.AlignLeft
            font: Theme.text.menuItem.font
            lineHeight: Theme.text.menuItem.lineHeight
            lineHeightMode: Text.FixedHeight
            wrap: false
            color: root._highlighted ? root._hoverColor : root._idleColor
        }
    }

    background: Rectangle {
        color: root._highlighted ? root.hoverBackgroundColor : "transparent"
        radius: 6
    }
}
