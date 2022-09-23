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
    header: OnboardingNav {
        navButton: NavButton {
            iconSource: "image://images/caret-left"
            text: "Back"
            onClicked: swipeView.currentIndex -= 1
        }
    }
    ColumnLayout {
        id: selections
        width: Math.min(parent.width, 600)
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
            Layout.leftMargin: 20
            Layout.rightMargin: 20

            bold: true
            header: qsTr("Starting initial download")
            headerMargin: 30
            description: qsTr("The application will connect to the Bitcoin network and start downloading and verifying transactions.\n\nThis may take several hours, or even days, based on your connection.")
            descriptionMargin: 20
        }
        TextButton {
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 30
            Layout.leftMargin: 20
            Layout.rightMargin: 20

            text: "Connection settings"
            textSize: 18
            textColor: Theme.color.orange
            onClicked: {
              connections.incrementCurrentIndex()
              swipeView.inSubPage = true
            }
        }
    }
    ContinueButton {
        id: continueButton
        anchors.topMargin: 40
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        anchors.bottomMargin: 60
        anchors.horizontalCenter: parent.horizontalCenter
        text: "Next"
        onClicked: swipeView.finished = true
    }

    state: AppMode.state

    states: [
        State {
            name: "MOBILE"
            AnchorChanges {
                target: continueButton
                anchors.top: undefined
                anchors.bottom: continueButton.parent.bottom
                anchors.right: continueButton.parent.right
                anchors.left: continueButton.parent.left
            }
        },
        State {
            name: "DESKTOP"
            AnchorChanges {
                target: continueButton
                anchors.top: selections.bottom
                anchors.bottom: undefined
                anchors.right: undefined
                anchors.left: undefined
            }
        }
    ]
}
