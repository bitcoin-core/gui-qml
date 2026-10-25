// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

StackView {
    id: root
    property bool vertical: false
    readonly property bool canGoBack: depth > 1 && currentItem && currentItem.navigationBackEnabled !== false
    // Under reduced motion pages crossfade in place instead of sliding.
    readonly property real slideDistance: Theme.motion.reduced || !parent ? 0 : vertical ? parent.height : parent.width
    readonly property real exitOpacity: Theme.motion.reduced ? 0 : 1

    function goBack() {
        if (canGoBack) {
            pop()
        }
    }

    pushEnter: Transition {
        NumberAnimation {
            property: root.vertical ? "y" : "x"
            from: root.slideDistance
            to: 0
            duration: Theme.motion.travel(Theme.motion.navigation)
            easing.type: Theme.motion.easing.move
        }
        NumberAnimation {
            property: "opacity"
            from: root.exitOpacity
            to: 1
            duration: Theme.motion.fade(Theme.motion.navigation)
            easing.type: Theme.motion.easing.enter
        }
    }
    pushExit: Transition {
        NumberAnimation {
            property: root.vertical ? "y" : "x"
            from: 0
            to: -root.slideDistance
            duration: Theme.motion.travel(Theme.motion.navigation)
            easing.type: Theme.motion.easing.move
        }
        NumberAnimation {
            property: "opacity"
            to: root.exitOpacity
            duration: Theme.motion.fade(Theme.motion.navigation)
            easing.type: Theme.motion.easing.exit
        }
    }
    popEnter: Transition {
        NumberAnimation {
            property: root.vertical ? "y" : "x"
            from: -root.slideDistance
            to: 0
            duration: Theme.motion.travel(Theme.motion.navigation)
            easing.type: Theme.motion.easing.move
        }
        NumberAnimation {
            property: "opacity"
            from: root.exitOpacity
            to: 1
            duration: Theme.motion.fade(Theme.motion.navigation)
            easing.type: Theme.motion.easing.enter
        }
    }
    popExit: Transition {
        NumberAnimation {
            property: root.vertical ? "y" : "x"
            from: 0
            to: root.slideDistance
            duration: Theme.motion.travel(Theme.motion.navigation)
            easing.type: Theme.motion.easing.move
        }
        NumberAnimation {
            property: "opacity"
            to: root.exitOpacity
            duration: Theme.motion.fade(Theme.motion.navigation)
            easing.type: Theme.motion.easing.exit
        }
    }
}
