// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

import "../controls"

// "· 1 warning" after a node status label. Clicking opens the warnings list.
AbstractButton {
    id: root
    objectName: "warningsLink"

    property NodeStatus status: NodeStatus {}
    property int pixelSize: 14

    // Whether there is anything to show; visible also depends on the parents.
    readonly property bool shown: status.warningsText.length > 0
    visible: shown
    text: status.warningsText
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: 0
    implicitWidth: content.implicitWidth
    implicitHeight: content.implicitHeight
    opacity: down ? 0.7 : hovered ? 0.85 : 1
    Accessible.role: Accessible.Link
    Accessible.name: text
    Accessible.description: qsTr("Shows the warnings your node reported")

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    contentItem: Row {
        id: content
        spacing: 10
        CoreText {
            text: "·"
            font.pixelSize: root.pixelSize
            wrap: false
            color: Theme.color.neutral5
        }
        CoreText {
            objectName: "warningsLinkText"
            text: root.text
            font.pixelSize: root.pixelSize
            wrap: false
            color: Theme.color.amber
        }
    }

    background: FocusBorder {
        visible: root.visualFocus
        borderRadius: 6
    }
}
