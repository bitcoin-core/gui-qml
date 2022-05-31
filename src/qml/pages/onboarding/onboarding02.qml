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
            header: qsTr("Strengthen bitcoin")
            headerMargin: 30
            description: qsTr("Bitcoin Core runs a full Bitcoin node which verifies the rules of the network are being followed.\n\nUsers running nodes is what makes bitcoin so resilient and trustworthy.")
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
