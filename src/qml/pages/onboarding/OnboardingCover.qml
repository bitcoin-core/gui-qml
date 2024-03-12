// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    background: null
    clip: true
    SwipeView {
        id: introductions
        anchors.fill: parent
        interactive: false
        orientation: Qt.Horizontal
        InformationPage {
            // TODO: reinstate the info button
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
        // TODO: resintate the info button linking to settings
    }
}
