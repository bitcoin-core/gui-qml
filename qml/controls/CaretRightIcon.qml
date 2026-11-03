// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

// Disclosure caret: points right, and down when `expanded`.
Icon {
    property bool expanded: false

    source: "image://images/caret-right"
    size: Theme.size.caret
    rotation: expanded ? 90 : 0

    Behavior on rotation {
        enabled: Theme.motion.animated
        NumberAnimation { duration: Theme.motion.normal; easing.type: Theme.motion.easing.enter }
    }
}
