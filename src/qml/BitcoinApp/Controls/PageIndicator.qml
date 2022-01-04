// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls

PageIndicator {
    id: root
    delegate: Rectangle {
        implicitWidth: 10
        implicitHeight: 10
        radius: Math.round(width / 2)
        color: "white"
        opacity: index === root.currentIndex ? 0.95 : pressed ? 0.7 : 0.45
        Behavior on opacity {
            OpacityAnimator {
                duration: 100
            }
        }
    }
}
