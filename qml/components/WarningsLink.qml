// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

import org.bitcoincore.qt 1.0

import "../controls"

AbstractButton {
    id: root
    objectName: "warningsLink"

    property NodeStatus status: NodeStatus {}
    property var textStyle: Theme.text.caption

    readonly property bool shown: status.warningsText.length > 0
    visible: shown
    text: status.warningsText
    hoverEnabled: AppMode.isDesktop
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
        spacing: Theme.space.ml
        CoreText {
            text: "·"
            textStyle: root.textStyle
            wrap: false
            color: Theme.color.neutral6
            Accessible.ignored: true
        }
        CoreText {
            objectName: "warningsLinkText"
            text: root.text
            textStyle: root.textStyle
            wrap: false
            color: Theme.color.warningText
        }
    }

    background: FocusBorder {
        visible: root.visualFocus
        borderRadius: 6
    }
}
