// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

ContextMenuButton {
    id: root

    autoClose: false
    checkable: true

    Accessible.checkable: true
    Accessible.checked: checked

    contentItem: RowLayout {
        spacing: Theme.space.m

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

        OptionSwitch {
            id: _switch
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: 40
            Layout.preferredHeight: 24
            checked: root.checked
            enabled: false
        }
    }
}
