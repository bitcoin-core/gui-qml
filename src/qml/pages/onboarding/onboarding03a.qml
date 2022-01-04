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
    ColumnLayout {
        width: 600
        spacing: 0
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        Header {
            Layout.alignment: Qt.AlignCenter
            header: "Connection"
            description: "Communicating with the Bitcoin network can use a lot of data."
        }
        ConnectionOptions {
            Layout.topMargin: 30
            Layout.alignment: Qt.AlignCenter
        }
        TextButton {
            Layout.topMargin: 30
            Layout.alignment: Qt.AlignCenter
            text: "Detailed Settings"
            textSize: 18
            textColor: "#F7931A"
            onClicked: {
              connections.incrementCurrentIndex()
              wizard.inSubPage = true
            }
        }
        ContinueButton {
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 40
            text: "Next"
            onClicked: wizard.incrementCurrentIndex()
        }
    }
}
