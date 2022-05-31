// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"

Page {
    background: null
    Layout.fillWidth: true
    ColumnLayout {
        width: 800
        spacing: 0
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        Image {
            Layout.topMargin: 20
            Layout.alignment: Qt.AlignCenter
            source: "image://images/app"
            sourceSize.width: 200
            sourceSize.height: 200
        }
        Header {
            Layout.fillWidth: true
            bold: true
            header: qsTr("The block clock")
            headerMargin: 30
            description: qsTr("The Bitcoin network targets a new block every 10 minutes. Sometimes it's faster and sometimes slower.\n\nThe block clock indicates each block on a dial that represents the current day.")
            descriptionMargin: 20
        }
        ContinueButton {
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 40
            text: "Next"
            onClicked: swipeView.incrementCurrentIndex()
        }
    }
}
