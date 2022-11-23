// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls

InformationPage {
    Layout.fillWidth: true
    navLeftDetail: NavButton {
        iconSource: "image://images/caret-left"
        text: qsTr("Back")
        onClicked: swipeView.currentIndex -= 1
    }
    bannerItem: Image {
        source: Theme.image.network
        sourceSize.width: 200
        sourceSize.height: 200
    }
    bold: true
    headerText: qsTr("Strengthen bitcoin")
    description: qsTr("Bitcoin Core runs a full Bitcoin node which verifies the rules of the network are being followed.\n\nUsers running nodes is what makes bitcoin\nso resilient and trustworthy.")
    buttonText: qsTr("Next")
}
