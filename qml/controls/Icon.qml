// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "utils.js" as Utils

Button {
    id: root
    width: icon.width
    height: icon.height
    required property color color
    required property url source
    property int size: 32
    property bool mirrorInRightToLeft: Utils.isDirectionalIcon(root.source)
    focusPolicy: Qt.NoFocus
    padding: 0
    icon.source: root.source
    icon.color: root.color
    icon.height: root.size
    icon.width: root.size
    enabled: false
    background: null
    // Decorative: the control that shows the icon carries the name.
    Accessible.ignored: true
    transform: Scale {
        origin.x: root.width / 2
        xScale: root.mirrorInRightToLeft && root.mirrored ? -1 : 1
    }

    Behavior on icon.color {
        enabled: Theme.motion.animated
        ColorAnimation {
            duration: Theme.motion.normal
        }
    }
}
