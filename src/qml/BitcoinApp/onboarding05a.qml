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
        header: qsTr("Storage")
        headerMargin: 0
        description: qsTr("Data retrieved from the Bitcoin network is stored on your device.\nYou have 500GB of storage available.")
        descriptionMargin: 10
        detailActive: true
        detailItem: ColumnLayout {
            spacing: 0
            StorageOptions {
                Layout.maximumWidth: 450
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
            }
            TextButton {
                Layout.alignment: Qt.AlignCenter
                Layout.topMargin: 30
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                text: "Detailed settings"
                textSize: 18
                textColor: "#F7931A"
                onClicked: {
                  storages.incrementCurrentIndex()
                  swipeView.inSubPage = true
                }
            }
        }
        buttonText: qsTr("Next")
    }
}
