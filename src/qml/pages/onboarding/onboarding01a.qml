// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"

Page {
    background: null
    clip: true
    Layout.fillWidth: true
    header: OnboardingNav {
        alignLeft: false
        navButton: NavButton {
            iconSource: "image://images/info"
            iconHeight: 24
            onClicked: {
                introductions.incrementCurrentIndex()
                swipeView.inSubPage = true
            }
        }
    }
    OnboardingInfo {
        height: parent.height
        width: Math.min(parent.width, 600)
        anchors.horizontalCenter: parent.horizontalCenter
        banner: Image {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignCenter
            source: "image://images/app"
            // Bitcoin icon has ~11% padding
            sourceSize.width: 112
            sourceSize.height: 112
        }
        bannerMargin: 0
        bold: true
        header: qsTr("Bitcoin Core App")
        headerSize: 36
        description: qsTr("Be part of the Bitcoin network.")
        descriptionMargin: 10
        descriptionSize: 24
        subtext: qsTr("100% open-source & open-design")
        buttonText: "Start"
    }
}
