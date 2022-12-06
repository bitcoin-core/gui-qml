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
        onClicked: swipeView.decrementCurrentIndex()
    }
    bannerItem: Image {
        source: Theme.image.blocktime
        sourceSize.width: 200
        sourceSize.height: 200
    }
    bold: true
    headerText: qsTr("The block clock")
    description: qsTr("The Bitcoin network targets a new block every\n10 minutes. Sometimes it's faster and sometimes slower.\n\nThe block clock indicates each block on a dial\nthat represents the current day.")
    buttonText: qsTr("Next")
}
