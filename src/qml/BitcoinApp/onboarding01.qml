// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls
import BitcoinApp.Components

Page {
    background: null
    Layout.fillWidth: true
    clip: true
    SwipeView {
        id: introductions
        anchors.fill: parent
        interactive: false
        orientation: Qt.Horizontal
        Loader {
            source:"onboarding01a.qml"
        }
        Loader {
            source:"onboarding01b.qml"
        }
        Loader {
            source:"onboarding01c.qml"
        }
    }
}
