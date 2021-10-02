// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// The Onboarding00Welcome page.

import QtQuick 2.12
import QtQuick.Controls 2.12

Column {
    width: appTitle.width
    spacing: 16

    Image {
        anchors.horizontalCenter: parent.horizontalCenter
        source: "image://images/app"
        sourceSize.width: 128
        sourceSize.height: 128
    }

    Label {
        id: appTitle
        anchors.horizontalCenter: parent.horizontalCenter
        text: "Bitcoin Core TnG"
        font.pointSize: 36
        color: "white"
    }

    Label {
        width: parent.width
        text: "Be part of the Bitcoin network."
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        font.pointSize: 24
        color: "white"
    }

    Button {
        id: startButton
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width
        text: "Start"
        font.pointSize: 20

        contentItem: Text {
            text: parent.text
            font: parent.font
            color: "white"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width
            implicitHeight: 60
            color: "orange"
            radius: 4
        }
    }

    Label {
        anchors.horizontalCenter: parent.horizontalCenter
        text: "100% open-source & open-design"
        font.pointSize: 15
        color: "white"
    }
}
