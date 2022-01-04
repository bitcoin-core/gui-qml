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
        width: 600
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
            header: qsTr("Strengthen the Bitcoin network")
            headerMargin: 30
            description: qsTr("This application runs a Bitcoin node optimized for mobile devices. It connects to the Bitcoin network and verifies transactions activity. This is what makes Bitcoin so trustworthy.")
        }
        ContinueButton {
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 40
            text: "Next"
            onClicked: wizard.incrementCurrentIndex()
        }
    }
}
