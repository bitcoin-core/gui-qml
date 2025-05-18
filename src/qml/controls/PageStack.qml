// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15

StackView {
    property bool vertical: false

    pushEnter: Transition {
        NumberAnimation {
            property: vertical ? "y" : "x"
            from: vertical ? parent.height : parent.width
            to: 0
            duration: 500
            easing.type: Easing.InOutCubic
        }
    }
    pushExit: Transition {
        NumberAnimation {
            property: vertical ? "y" : "x"
            from: 0
            to: vertical ? -parent.height : -parent.width
            duration: 500
            easing.type: Easing.InOutCubic
        }
    }
    popEnter: Transition {
        NumberAnimation {
            property: vertical ? "y" : "x"
            from: vertical ? -parent.height : -parent.width
            to: 0
            duration: 500
            easing.type: Easing.InOutCubic
        }
    }
    popExit: Transition {
        NumberAnimation {
            property: vertical ? "y" : "x"
            from: 0
            to: vertical ? parent.height : parent.width
            duration: 500
            easing.type: Easing.InOutCubic
        }
    }
}
