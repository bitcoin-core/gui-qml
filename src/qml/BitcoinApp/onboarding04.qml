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
        Header {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            bold: true
            header: qsTr("Storage location")
            description: qsTr("Where do you want to store the downloaded block data?")
            descriptionMargin: 20
        }
        StorageLocations {
            Layout.maximumWidth: 450
            Layout.topMargin: 30
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            Layout.alignment: Qt.AlignCenter
        }
    }
    ContinueButton {
        id: continueButton
        anchors.topMargin: 40
        anchors.bottomMargin: 60
        anchors.rightMargin: 20
        anchors.leftMargin: 20
        text: "Next"
        onClicked: swipeView.incrementCurrentIndex()
    }

    state: AppMode.state

    states: [
        State {
            name: "MOBILE"
            AnchorChanges {
                target: continueButton
                anchors.top: undefined
                anchors.bottom: continueButton.parent.bottom
                anchors.left: continueButton.parent.left
                anchors.right: continueButton.parent.right
                anchors.horizontalCenter: undefined
            }
        },
        State {
            name: "DESKTOP"
            AnchorChanges {
                target: continueButton
                anchors.top: selections.bottom
                anchors.bottom: undefined
                anchors.left: undefined
                anchors.right: undefined
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    ]
}
