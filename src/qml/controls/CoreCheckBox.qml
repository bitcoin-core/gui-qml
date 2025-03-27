// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

AbstractButton {
    id: root
    implicitWidth: 20
    implicitHeight: 20

    property color borderColor: Theme.color.neutral9
    property color fillColor: Theme.color.neutral9

    background: null

    checkable: true
    hoverEnabled: AppMode.isDesktop

    contentItem: Rectangle {
        radius: 3
        border.color: root.checked ? root.fillColor : root.borderColor
        border.width: 1
        color: root.checked ? root.fillColor : "transparent"
    }
}
