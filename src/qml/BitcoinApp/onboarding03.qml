// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls

Page {
    background: null
    clip: true
    Layout.fillWidth: true
    header: OnboardingNav {
        leftDetail: NavButton {
            iconSource: "image://images/caret-left"
            text: "Back"
            onClicked: swipeView.currentIndex -= 1
        }
    }
    OnboardingInfo {
        height: parent.height
        width: Math.min(parent.width, 600)
        anchors.horizontalCenter: parent.horizontalCenter
        banner: Image {
            source: Theme.image.blocktime
            sourceSize.width: 200
            sourceSize.height: 200
        }
        bold: true
        header: qsTr("The block clock")
        description: qsTr("The Bitcoin network targets a new block every\n10 minutes. Sometimes it's faster and sometimes slower.\n\nThe block clock indicates each block on a dial\nthat represents the current day.")
        buttonText: "Next"
    }
}
