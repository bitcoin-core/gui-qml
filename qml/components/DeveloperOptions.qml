// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

ColumnLayout {
    spacing: 20
    Setting {
        Layout.fillWidth: true
        header: qsTr("Developer documentation")
        actionItem: ExternalLink {
            iconSource: "qrc:/icons/export"
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
        header: qsTr("Dark Mode")
        actionItem: OptionSwitch {
            onToggled: Theme.toggleDark()
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
