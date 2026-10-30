// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

// The app's Tooltip in a ToolTip popup, so hover hints inside clipped rows
// still draw above them, in the theme's font and colors.
ToolTip {
    id: root

    delay: Theme.motion.tooltipDelay
    padding: 0
    background: null

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.normal; easing.type: Theme.motion.easing.enter }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; to: 0; duration: Theme.motion.fast; easing.type: Theme.motion.easing.exit }
    }

    contentItem: Tooltip {
        width: implicitWidth
        height: implicitHeight
        text: root.text
        motionEnabled: false
    }
}
