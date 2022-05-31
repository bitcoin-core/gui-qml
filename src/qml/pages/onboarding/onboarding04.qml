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
    Layout.fillWidth: true
    clip: true
    ColumnLayout {
        width: 800
        spacing: 0
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        Header {
            Layout.fillWidth: true
            bold: true
            header: qsTr("Storage location")
            description: qsTr("Data retrieved from the Bitcoin network is stored on your device.\nYou have 500GB of storage available.")
        }
        StorageLocations {
            Layout.topMargin: 30
            Layout.alignment: Qt.AlignCenter
        }
        ContinueButton {
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 40
            text: "Next"
            onClicked: swipeView.incrementCurrentIndex()
        }
    }
}