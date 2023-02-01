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
            iconWidth: 30
            iconHeight: 30
            link: "https://bitcoin.org/en/bitcoin-core/contribute/documentation"
        }
        onClicked: loadedItem.clicked()
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Database cache size")
        actionItem: ValueInput {
            description: ("450 MiB")
        }
        onClicked: loadedItem.forceActiveFocus()
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Script verification threads")
        actionItem: ValueInput {
            description: ("0")
        }
        onClicked: loadedItem.forceActiveFocus()
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Dark Mode")
        actionItem: OptionSwitch {
            checked: Theme.dark
            onToggled: Theme.toggleDark()
        }
        onClicked: loadedItem.toggled()
    }
}
