// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.11
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
            Layout.fillWidth: true
            header: qsTr("Storage")
            description: qsTr("Data retrieved from the Bitcoin network is stored on your device. You have 500GB of storage available.")
        }
        StorageOptions {
            Layout.topMargin: 30
            Layout.alignment: Qt.AlignCenter
        }
        TextButton {
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 30
            text: "Detailed settings"
            textSize: 18
            textColor: "#F7931A"
        }
        ContinueButton {
            Layout.alignment: Qt.AlignCenter
            Layout.topMargin: 40
            text: "Next"
            onClicked: wizard.finished = true
        }
    }
}
