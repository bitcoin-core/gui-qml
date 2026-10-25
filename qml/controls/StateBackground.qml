// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

// Background for hover, pressed and selected states, which all change
// color over Theme.motion.normal. Set stateColor rather than color.
Rectangle {
    id: root
    property color stateColor: "transparent"
    // Fading to or from "transparent" keeps the last visible hue, so the
    // fade does not pass through translucent black.
    property color fadeHue: Theme.color.surfaceHover

    color: stateColor.a > 0 ? stateColor : Qt.rgba(fadeHue.r, fadeHue.g, fadeHue.b, 0)
    onStateColorChanged: if (stateColor.a > 0) fadeHue = stateColor
    Component.onCompleted: if (stateColor.a > 0) fadeHue = stateColor

    Behavior on color {
        enabled: Theme.motion.animated
        ColorAnimation {
            duration: Theme.motion.normal
            easing.type: Theme.motion.easing.enter
        }
    }
}
