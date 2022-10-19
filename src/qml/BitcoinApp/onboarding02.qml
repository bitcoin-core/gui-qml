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
            source: Theme.image.network
            sourceSize.width: 200
            sourceSize.height: 200
        }
        bold: true
        header: qsTr("Strengthen bitcoin")
        description: qsTr("Bitcoin Core runs a full Bitcoin node which verifies the rules of the network are being followed.\n\nUsers running nodes is what makes bitcoin\nso resilient and trustworthy.")
        buttonText: "Next"
    }
}
