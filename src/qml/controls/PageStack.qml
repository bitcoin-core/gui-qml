// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

StackView {
    property bool vertical: false
    readonly property bool canGoBack: depth > 1 && currentItem && currentItem.navigationBackEnabled !== false

    function goBack() {
        if (canGoBack) {
            pop()
        }
    }

    pushEnter: Transition {
        NumberAnimation {
            property: vertical ? "y" : "x"
            from: parent ? (vertical ? parent.height : parent.width) : 0
            to: 0
            duration: 500
            easing.type: Easing.InOutCubic
        }
    }
    pushExit: Transition {
        NumberAnimation {
            property: vertical ? "y" : "x"
            from: 0
            to: parent ? (vertical ? -parent.height : -parent.width) : 0
            duration: 500
            easing.type: Easing.InOutCubic
        }
    }
    popEnter: Transition {
        NumberAnimation {
            property: vertical ? "y" : "x"
            from: parent ? (vertical ? -parent.height : -parent.width) : 0
            to: 0
            duration: 500
            easing.type: Easing.InOutCubic
        }
    }
    popExit: Transition {
        NumberAnimation {
            property: vertical ? "y" : "x"
            from: 0
            to: parent ? (vertical ? parent.height : parent.width) : 0
            duration: 500
            easing.type: Easing.InOutCubic
        }
    }
}
