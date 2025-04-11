// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.11
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

InformationPage {
    Layout.fillWidth: true
    navLeftDetail: NavButton {
        iconSource: "image://images/caret-left"
        text: qsTr("Back")
        onClicked: swipeView.currentIndex -= 1
    }
    bannerActive: false
    bold: true
    headerText: qsTr("Storage")
    headerMargin: 0
    description: qsTr("Data retrieved from the Bitcoin network is stored on your device.\nYou have 500GB of storage available.")
    descriptionMargin: 10
    detailActive: true
    detailItem: ColumnLayout {
        spacing: 0
        StorageOptions {
            Layout.maximumWidth: 450
            Layout.alignment: Qt.AlignCenter
        }
        TextButton {
            Layout.topMargin: 30
            Layout.fillWidth: true
            text: qsTr("Detailed settings")
            textSize: 18
            textColor: "#F7931A"
            onClicked: {
              storages.incrementCurrentIndex()
              swipeView.inSubPage = true
            }
        }
    }
    buttonText: qsTr("Next")
}
