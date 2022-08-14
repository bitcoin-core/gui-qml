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
    header: OnboardingNav {
        navButton: NavButton {
            iconSource: "image://images/caret-left"
            text: "Back"
            onClicked: swipeView.currentIndex -= 1
        }
    }
    ColumnLayout {
        width: 600
        spacing: 0
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        Image {
            Layout.topMargin: 20
            Layout.alignment: Qt.AlignCenter
            source: Theme.image.storage
            sourceSize.width: 200
            sourceSize.height: 200
        }
        Header {
            Layout.fillWidth: true
            bold: true
            header: qsTr("Starting initial download")
            headerMargin: 30
            description: qsTr("The application will connect to the Bitcoin network and start downloading and verifying transactions.\n\nThis may take several hours, or even days, based on your connection.")
            descriptionMargin: 20
        }
        TextButton {
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 30
            text: "Connection Settings"
            textSize: 18
            textColor: Theme.color.orange
            onClicked: {
              connections.incrementCurrentIndex()
              swipeView.inSubPage = true
            }
        }
        ContinueButton {
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 40
            text: "Next"
            onClicked: swipeView.finished = true
        }
    }
}
