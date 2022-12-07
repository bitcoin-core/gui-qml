// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls

ColumnLayout {
    spacing: 20
    Setting {
        Layout.fillWidth: true
        header: qsTr("Website")
        actionItem: ExternalLink {
            description: qsTr("bitcoincore.org")
            link: "https://bitcoincore.org"
            iconSource: "image://images/caret-right"
        }
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Source code")
        actionItem: ExternalLink {
            description: qsTr("github.com/bitcoin/bitcoin")
            link: "https://github.com/bitcoin/bitcoin"
            iconSource: "image://images/caret-right"
        }
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("License")
        actionItem: ExternalLink {
            description: qsTr("MIT")
            link: "https://opensource.org/licenses/MIT"
            iconSource: "image://images/caret-right"
        }
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Version")
        actionItem: ExternalLink {
            description: qsTr("v22.99.0-1e7564eca8a6")
            link: "https://bitcoin.org/en/download"
            iconSource: "image://images/caret-right"
        }
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Developer options")
        description: qsTr("Only use these if you have development experience")
        actionItem: Button {
            icon.source: "image://images/caret-right"
            icon.color: Theme.color.neutral9
            icon.height: 18
            icon.width: 18
            background: null
            onClicked: {
                introductions.incrementCurrentIndex()
            }
        }
    }
}
