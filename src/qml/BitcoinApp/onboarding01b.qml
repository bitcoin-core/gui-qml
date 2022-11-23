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
    Layout.fillWidth: true
    header: NavigationBar {
        leftDetail: NavButton {
            iconSource: "image://images/caret-left"
            text: "Back"
            onClicked: {
                introductions.decrementCurrentIndex()
                swipeView.inSubPage = false
            }
        }
    }
    OnboardingInfo {
        height: parent.height
        width: Math.min(parent.width, 600)
        anchors.horizontalCenter: parent.horizontalCenter
        bannerActive: false
        bold: true
        header: "About"
        headerMargin: 0
        description: qsTr("Bitcoin Core is an open source project.\nIf you find it useful, please contribute.\n\n This is experimental software.")
        descriptionMargin: 20
        detailActive: true
        detailItem: ColumnLayout {
            spacing: 0
            AboutOptions {
                Layout.maximumWidth: 450
                Layout.alignment: Qt.AlignCenter
            }
        }
    }
}
