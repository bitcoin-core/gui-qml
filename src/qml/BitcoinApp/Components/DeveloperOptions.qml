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
        header: qsTr("Database cache size")
        actionItem: ValueInput {
            description: ("450 MiB")
        }
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Script verification threads")
        actionItem: ValueInput {
            description: ("0")
        }
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Dark Mode")
        actionItem: OptionSwitch {
            checked: Theme.dark
            onToggled: Theme.toggleDark()
        }
    }
}
