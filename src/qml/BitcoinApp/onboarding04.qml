// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls
import BitcoinApp.Components
import org.bitcoincore.qt

InformationPage {
    Layout.fillWidth: true
    navLeftDetail: NavButton {
        iconSource: "image://images/caret-left"
        text: "Back"
        onClicked: swipeView.currentIndex -= 1
    }
    bannerActive: false
    bold: true
    headerText: qsTr("Storage location")
    headerMargin: 0
    description: qsTr("Where do you want to store the downloaded block data?")
    descriptionMargin: 20
    detailActive: true
    detailItem: ColumnLayout {
        spacing: 0
        StorageLocations {
            Layout.maximumWidth: 450
            Layout.alignment: Qt.AlignCenter
        }
    }
    buttonText: qsTr("Next")
}
