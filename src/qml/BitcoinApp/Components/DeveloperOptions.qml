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
        header: qsTr("Developer documentation")
        actionItem: ExternalLink {
            iconSource: ":/qt/qml/BitcoinApp/res/icons/export"
            link: "https://bitcoin.org/en/bitcoin-core/contribute/documentation"
        }
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Storage limit")
        actionItem: OptionSwitch {}
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Network")
        actionItem: ValueInput {
            description: qsTr("Mainnet")
        }
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Other option...")
        actionItem: ValueInput {
            description: qsTr("42")
        }
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Other option...")
        actionItem: ValueInput {
            description: qsTr("Description...")
        }
    }
}
