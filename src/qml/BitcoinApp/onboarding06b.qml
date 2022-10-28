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
    ColumnLayout {
        width: Math.min(parent.width, 490)
        spacing: 0
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        Header {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            bold: true
            header: "Connection settings"
        }
        ConnectionSettings {
            Layout.topMargin: 30
            Layout.leftMargin: 20
            Layout.rightMargin: 20
        }
    }
}
