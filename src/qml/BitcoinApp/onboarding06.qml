// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls
import BitcoinApp.Components

Page {
    background: null
    Layout.fillWidth: true
    clip: true
    SwipeView {
        id: connections
        anchors.fill: parent
        interactive: false
        orientation: Qt.Vertical
        InformationPage {
            Layout.fillWidth: true
            navLeftDetail: NavButton {
                iconSource: "image://images/caret-left"
                text: qsTr("Back")
                onClicked: swipeView.decrementCurrentIndex()
            }
            bannerItem: Image {
                Layout.topMargin: 20
                Layout.alignment: Qt.AlignCenter
                source: Theme.image.storage
                sourceSize.width: 200
                sourceSize.height: 200
            }
            bold: true
            headerText: qsTr("Starting initial download")
            headerMargin: 30
            description: qsTr("The application will connect to the Bitcoin network and start downloading and verifying transactions.\n\nThis may take several hours, or even days, based on your connection.")
            descriptionMargin: 20
            detailActive: true
            detailItem: TextButton {
                text: qsTr("Connection settings")
                textSize: 18
                textColor: Theme.color.orange
                onClicked: {
                    connections.incrementCurrentIndex()
                    swipeView.inSubPage = true
                }
            }
            lastPage: true
            buttonText: qsTr("Next")
        }
        SettingsConnection {}
    }
}
