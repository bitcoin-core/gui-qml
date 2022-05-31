// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls

ColumnLayout {
    spacing: 20
    Information {
        Layout.fillWidth: true
        header:  qsTr("Website")
        description: qsTr("bitcoincore.org >")
        link: "https://bitcoincore.org"
    }
    Information {
        Layout.fillWidth: true
        header:  qsTr("Source code")
        description: qsTr("github.com/bitcoin/bitcoin >")
        link: "https://github.com/bitcoin/bitcoin"
    }
    Information {
        Layout.fillWidth: true
        header:  qsTr("License")
        description: qsTr("MIT >")
        link: "https://opensource.org/licenses/MIT"
    }
    Information {
        Layout.fillWidth: true
        header:  qsTr("Version")
        description: qsTr("v22.99.0-1e7564eca8a6 >")
        link: "https://bitcoin.org/en/download"
    }
    RowLayout {
        Header {
            Layout.fillWidth: true
            center: false
            header: qsTr("Developer options")
            headerSize: 18
            description: qsTr("Only use these if you have development experience")
            descriptionSize: 15
            descriptionMargin: 10
            wrap: false
            }
        Loader {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            Layout.alignment: Qt.AlignRight
            Layout.rightMargin: 5
            active: true
            visible: active
            sourceComponent: TextButton {
                text: ">"
                bold: false
                rightalign: true
                onClicked: {
                    introductions.incrementCurrentIndex()
                    swipeView.inSubPage = true
                }
            }
        }
    }
}
