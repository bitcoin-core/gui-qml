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
    header: NavigationBar {
        rightDetail: NavButton {
            text: "Done"
            onClicked: {
                connections.decrementCurrentIndex()
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
        header: "Connection settings"
        headerMargin: 0
        detailActive: true
        detailItem: ColumnLayout {
            spacing: 0
            ConnectionSettings {
                Layout.maximumWidth: 450
                Layout.alignment: Qt.AlignCenter
            }
        }
    }
}
