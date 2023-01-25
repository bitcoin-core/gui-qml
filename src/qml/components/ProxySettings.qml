// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

ColumnLayout {
    spacing: 4
    Header {
        headerBold: true
        center: false
        header: qsTr("Default Proxy")
        headerSize: 24
        description: qsTr("Run peer connections through a proxy (SOCKS5) for improved privacy. The default proxy supports connections via IPv4, IPv6 and Tor. Tor connections can also be run through a separate Tor proxy.")
        descriptionSize: 15
        Layout.bottomMargin: 10
    }
    Separator { Layout.fillWidth: true }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Enable")
        actionItem: OptionSwitch {}
        onClicked: {
            loadedItem.toggle()
            loadedItem.toggled()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: defaultProxy
        Layout.fillWidth: true
        header: qsTr("IP and Port")
        actionItem: ValueInput {
            parentState: defaultProxy.state
            description: "127.0.0.1:9050"
            onEditingFinished: {
                defaultProxy.forceActiveFocus()
            }
        }
        onClicked: loadedItem.forceActiveFocus()
    }
    Separator { Layout.fillWidth: true }
    Header {
        headerBold: true
        center: false
        header: qsTr("Tor Proxy")
        headerSize: 24
        description: qsTr("Enable to run Tor connections through a dedicated proxy.")
        descriptionSize: 15
        Layout.topMargin: 35
        Layout.bottomMargin: 10
    }
    Separator { Layout.fillWidth: true }
    Setting {
        Layout.fillWidth: true
        header: qsTr("Enable")
        actionItem: OptionSwitch {}
        description: qsTr("When disabled, Tor connections will use the default proxy (if enabled).")
        onClicked: {
            loadedItem.toggle()
            loadedItem.toggled()
        }
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: torProxy
        Layout.fillWidth: true
        header: qsTr("IP and Port")
        actionItem: ValueInput {
            parentState: torProxy.state
            description: "127.0.0.1:9050"
            onEditingFinished: {
                torProxy.forceActiveFocus()
            }
        }
        onClicked: loadedItem.forceActiveFocus()
    }
    Separator { Layout.fillWidth: true }
}
