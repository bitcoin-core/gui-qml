// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls
import BitcoinApp.Components
import org.bitcoincore.qt

Page {
    background: null
    Layout.fillWidth: true
    clip: true
    header: NavigationBar {
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
        bannerActive: false
        bold: true
        header: qsTr("Storage location")
        headerMargin: 0
        description: qsTr("Where do you want to store the downloaded block data?")
        descriptionMargin: 20
        detailActive: true
        detailItem: StorageLocations {
            Layout.maximumWidth: 450
        }
        buttonText: qsTr("Next")
    }
}
