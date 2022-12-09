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
    clip: true
    SwipeView {
        id: introductions
        anchors.fill: parent
        interactive: false
        orientation: Qt.Horizontal
        InformationPage {
            navRightDetail: NavButton {
                iconSource: "image://images/info"
                iconHeight: 24
                onClicked: {
                    introductions.incrementCurrentIndex()
                }
            }
            bannerItem: Image {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignCenter
                source: "image://images/app"
                // Bitcoin icon has ~11% padding
                sourceSize.width: 112
                sourceSize.height: 112
            }
            bannerMargin: 0
            bold: true
            headerText: qsTr("Bitcoin Core App")
            headerSize: 36
            description: qsTr("Be part of the Bitcoin network.")
            descriptionMargin: 10
            descriptionSize: 24
            subtext: qsTr("100% open-source & open-design")
            buttonText: qsTr("Start")
        }
        SettingsAbout {
            navLeftDetail: NavButton {
                iconSource: "image://images/caret-left"
                text: qsTr("Back")
                onClicked: {
                    introductions.decrementCurrentIndex()
                }
            }
        }
    }
}
