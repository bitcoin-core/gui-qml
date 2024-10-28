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
        source: Theme.image.network
        sourceSize.width: 200
        sourceSize.height: 200
    }
    bold: true
    headerText: qsTr("Strengthen bitcoin")
    description: qsTr("Bitcoin Core runs a full Bitcoin node which verifies " +
        "the rules of the network are being followed.\n\nUsers running nodes " +
        "is what makes bitcoin so resilient and trustworthy.")
    buttonText: qsTr("Next")
}
