// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"

Page {
    background: null
    clip: true
    Layout.fillWidth: true
    header: OnboardingNav {
        navButton: NavButton {
            iconSource: "image://images/caret-left"
            text: "Back"
            onClicked: swipeView.currentIndex -= 1
        }
    }
    OnboardingInfo {
      anchors.top: parent.top
      anchors.horizontalCenter: parent.horizontalCenter
      banner: Image {
          source: "image://images/app"
          sourceSize.width: 200
          sourceSize.height: 200
      }
      bold: true
      header: qsTr("Strengthen bitcoin")
      description: qsTr("Bitcoin Core runs a full Bitcoin node which verifies the rules of the network are being followed.\n\nUsers running nodes is what makes bitcoin\nso resilient and trustworthy.")
      buttonText: "Next"
    }
}
