// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

InformationPage {
    id: root
    navLeftDetail: NavButton {
        iconSource: "image://images/caret-left"
        text: qsTr("Back")
        onClicked: root.back()
    }
    bannerActive: false
    bold: true
    headerText: qsTr("Storage location")
    headerMargin: 0
    description: qsTr("Where do you want to store the downloaded block data?\nYou need a minimum of %1GB of storage.").arg(chainModel.assumedChainstateSize + 1)
    descriptionMargin: 20
    detailActive: true
    detailItem: StorageLocations {}
    buttonText: qsTr("Next")
}
