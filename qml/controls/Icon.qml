// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
    id: root
    width: icon.width
    height: icon.height
    required property color color
    required property url source
    property int size: 32
    focusPolicy: Qt.NoFocus
    padding: 0
    icon.source: root.source
    icon.color: root.shownColor
    icon.height: root.size
    icon.width: root.size
    enabled: false
    background: null
    // Decorative: the control that shows the icon carries the name.
    Accessible.ignored: true

    // The animated color. An animation of icon.color itself keeps writing it
    // after a removed delegate's context is gone, and Qt 6.2 then reloads the
    // icon without an engine and crashes. This binding stops with the context.
    property color shownColor: root.color
    Behavior on shownColor {
        enabled: Theme.motion.animated
        ColorAnimation {
            duration: Theme.motion.normal
        }
    }
}
