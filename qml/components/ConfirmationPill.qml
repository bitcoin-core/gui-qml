// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

Control {
    id: root
    property int confirmations: 0
    property bool statusKnown: true
    property bool inactive: false
    readonly property color accentColor: !statusKnown || inactive || confirmations <= 0 ? Theme.color.neutral7
        : confirmations < 6 ? Theme.color.amber : Theme.color.green
    readonly property string text: !statusKnown ? qsTr("Status unavailable")
        : !inactive && confirmations === 0 ? qsTr("Unconfirmed")
        : confirmations === 1 ? qsTr("1 confirmation") : qsTr("%1 confirmations").arg(Math.max(0, confirmations))

    leftPadding: 12
    rightPadding: 12
    topPadding: 5
    bottomPadding: 5
    implicitHeight: 30
    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    Accessible.role: Accessible.StaticText
    Accessible.name: text
    background: Rectangle {
        radius: height / 2
        color: !root.statusKnown || root.inactive || root.confirmations <= 0 ? Theme.color.neutral2
            : Qt.rgba(root.accentColor.r, root.accentColor.g, root.accentColor.b, 0.16)
    }
    contentItem: RowLayout {
        spacing: 7
        Rectangle { width: 6; height: 6; radius: 3; color: root.accentColor }
        CoreText { text: root.text; color: root.accentColor; font: Theme.text.captionStrong.font }
    }
}
