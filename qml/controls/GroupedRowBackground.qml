// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

// Background of a row in a virtualised grouped list (a ListView section),
// which cannot be wrapped in a FormSection card: the first and last rows of
// a group round their outer corners, and the rows in between are square and
// divided like FormRows.
StateBackground {
    id: root

    property bool firstInGroup: true
    property bool lastInGroup: true
    property bool hovered: false
    property bool pressed: false
    property bool selected: false
    property bool showFocus: false
    property int surfaceLevel: 0
    property int cornerRadius: Theme.radius.listRow
    property bool showDivider: !lastInGroup
    property real dividerLeftInset: Theme.space.cardInset
    property real dividerRightInset: Theme.space.cardInset
    property string focusBorderObjectName: ""
    readonly property color restingColor: Theme.color.surface(surfaceLevel + 1)

    // Opaque, so the square corner patches do not double the state tint.
    stateColor: pressed ? Qt.tint(restingColor, Theme.color.surfacePressed)
        : selected ? Qt.tint(restingColor, Theme.color.surfaceSelected)
        : hovered ? Qt.tint(restingColor, Theme.color.surfaceHover)
        : restingColor
    radius: cornerRadius

    Rectangle {
        visible: !root.firstInGroup
        anchors.top: parent.top
        width: parent.width
        height: Math.min(root.cornerRadius, parent.height / 2)
        color: root.color
    }

    Rectangle {
        visible: !root.lastInGroup
        anchors.bottom: parent.bottom
        width: parent.width
        height: Math.min(root.cornerRadius, parent.height / 2)
        color: root.color
    }

    Separator {
        visible: root.showDivider
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.dividerLeftInset
        anchors.rightMargin: root.dividerRightInset
    }

    // Inset, so the clipping list does not cut the ring off.
    FocusBorder {
        objectName: root.focusBorderObjectName
        visible: root.showFocus
        targetRadius: root.firstInGroup || root.lastInGroup ? root.cornerRadius : Theme.radius.s
        topMargin: Theme.space.xxs
        bottomMargin: Theme.space.xxs
        leftMargin: Theme.space.xxs
        rightMargin: Theme.space.xxs
    }
}
