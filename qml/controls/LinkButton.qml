// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

AbstractButton {
    id: root

    property url iconSource: ""
    property int iconSize: Theme.size.icon.s
    property var textStyle: Theme.text.captionStrong
    property color textColor: !enabled ? Theme.color.disabledContent
        : down ? Theme.color.accentPressed
        : hovered ? Theme.color.accentHover
        : Theme.color.accentText

    implicitHeight: Theme.size.controlSmall
    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    leftPadding: Theme.space.xs
    rightPadding: Theme.space.xs
    hoverEnabled: enabled && AppMode.isDesktop
    focusPolicy: Qt.StrongFocus
    Accessible.name: text

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    contentItem: RowLayout {
        spacing: Theme.space.s
        Icon {
            visible: root.iconSource.toString().length > 0
            source: root.iconSource
            size: root.iconSize
            color: root.textColor
        }
        CoreText {
            Layout.fillWidth: true
            text: root.text
            textStyle: root.textStyle
            color: root.textColor
            horizontalAlignment: Text.AlignHCenter
        }
    }

    background: FocusBorder {
        visible: root.visualFocus
        borderRadius: 6
    }

}
