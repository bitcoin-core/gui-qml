// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

Control {
    id: root
    property string text: ""
    property color accentColor: Theme.color.orange
    property color backgroundColor: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.16)
    property bool showDot: true

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
        color: root.backgroundColor
    }
    contentItem: RowLayout {
        spacing: root.showDot ? 7 : 0
        Rectangle {
            visible: root.showDot
            width: root.showDot ? 6 : 0
            height: root.showDot ? 6 : 0
            radius: 3
            color: root.accentColor
        }
        CoreText {
            text: root.text
            color: root.accentColor
            font: Theme.text.captionStrong.font
        }
    }
}
