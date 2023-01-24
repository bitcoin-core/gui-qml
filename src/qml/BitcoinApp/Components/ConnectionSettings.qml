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
        header: qsTr("Enable listening")
        description: qsTr("Allows incoming connections")
        actionItem: OptionSwitch {
            checked: optionsModel.listen
            onToggled: optionsModel.listen = checked
        }
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Map port using UPnP")
        actionItem: OptionSwitch {
            checked: optionsModel.upnp
            onToggled: optionsModel.upnp = checked
        }
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Map port using NAT-PMP")
        actionItem: OptionSwitch {
            checked: optionsModel.natpmp
            onToggled: optionsModel.natpmp = checked
        }
    }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Enable RPC server")
        actionItem: OptionSwitch {
            checked: optionsModel.server
            onToggled: optionsModel.server = checked
        }
    }
    Setting {
        last: true
        Layout.fillWidth: true
        header: qsTr("Proxy settings")
        actionItem: Button {
            icon.source: "image://images/caret-right"
            icon.color: Theme.color.neutral9
            icon.height: 18
            icon.width: 18
            background: null
        }
    }
}
