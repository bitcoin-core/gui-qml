// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls

ApplicationWindow {
    id: root
    visible: true
    title: "Bitcoin Core App"
    minimumWidth: 500
    minimumHeight: 250
    color: Theme.color.background
    ColumnLayout {
        spacing: 30
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        width: 340

        Header {
            Layout.topMargin: 30
            Layout.fillWidth: true
            bold: true
            header: qsTr("There was an issue starting up.")
            headerSize: 21
            description: message
            descriptionMargin: 3
        }
        OutlineButton {
            Layout.alignment: Qt.AlignCenter
            text: qsTr("Close")
            onClicked: Qt.quit()
        }
    }
}
