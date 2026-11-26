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
    // Keeps the dialog reachable to restore warnings once all are dismissed.
    property bool showDismissed: false

    readonly property int dismissedCount: status.dismissedWarnings.length
    readonly property bool dismissedOnly: status.warningCount === 0 && showDismissed && dismissedCount > 0
    readonly property bool shown: status.warningsText.length > 0 || dismissedOnly
    visible: shown
    text: {
        if (!dismissedOnly) return status.warningsText
        if (dismissedCount === 1) {
            //: Link that reopens the node warnings dialog after the user dismissed the only current warning.
            return qsTr("1 dismissed warning")
        }
        //: Link that reopens the node warnings dialog after the user dismissed every current warning. %1 is the count.
        return qsTr("%1 dismissed warnings").arg(dismissedCount)
    }
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
            color: root.dismissedOnly ? Theme.color.neutral7 : Theme.color.warningText
        }
    }

    background: FocusBorder {
        visible: root.visualFocus
        borderRadius: 6
    }
}
