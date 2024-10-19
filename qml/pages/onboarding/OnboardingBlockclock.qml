// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"

InformationPage {
    id: root
    navLeftDetail: NavButton {
        iconSource: "image://images/caret-left"
        text: qsTr("Back")
        onClicked: root.back()
    }
    bannerItem: Image {
        source: Theme.image.blocktime
        sourceSize.width: 200
        sourceSize.height: 200
    }
    bold: true
    headerText: qsTr("The block clock")
    description: qsTr("The Bitcoin network targets a new block every 10 minutes. " +
        "Sometimes it's faster and sometimes slower.\n\nThe block clock indicates each " +
        "block on a dial that represents the current day.")
    buttonText: qsTr("Next")
}
