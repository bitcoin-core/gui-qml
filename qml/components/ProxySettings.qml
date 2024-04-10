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
        description: qsTr("Run peer connections through a proxy (SOCKS5) for improved privacy. The default proxy supports connections via IPv4, IPv6 and Tor.")
        descriptionSize: 15
        Layout.bottomMargin: 10
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: defaultProxyEnable
        Layout.fillWidth: true
        header: qsTr("Enable")
        actionItem: OptionSwitch {
            onCheckedChanged: {
                if (checked == false) {
                    defaultProxy.state = "DISABLED"
                } else {
                    defaultProxy.state = "FILLED"
                }
            }
        }
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
        errorText: qsTr("Invalid IP address or port format. Please use the format '255.255.255.255:65535'.")
        state: !defaultProxyEnable.loadedItem.checked ? "DISABLED" : "FILLED"
        showErrorText: !defaultProxy.loadedItem.validInput && defaultProxyEnable.loadedItem.checked
        actionItem: IPAddressValueInput {
            parentState: defaultProxy.state
            description: "127.0.0.1:9050"
            activeFocusOnTab: true
        }
        onClicked: {
            loadedItem.filled = true
            loadedItem.forceActiveFocus()
        }
    }
    Separator { Layout.fillWidth: true }
    Header {
        headerBold: true
        center: false
        header: qsTr("Tor Proxy")
        headerSize: 24
        description: qsTr("Run Tor connections through a dedicated proxy.")
        descriptionSize: 15
        Layout.topMargin: 35
        Layout.bottomMargin: 10
    }
    Separator { Layout.fillWidth: true }
    Setting {
        id: torProxyEnable
        Layout.fillWidth: true
        header: qsTr("Enable")
        actionItem: OptionSwitch {
            onCheckedChanged: {
                if (checked == false) {
                    torProxy.state = "DISABLED"
                } else {
                    torProxy.state = "FILLED"
                }
            }
        }
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
        errorText: qsTr("Invalid IP address or port format. Please use the format '255.255.255.255:65535'.")
        state: !torProxyEnable.loadedItem.checked ? "DISABLED" : "FILLED"
        showErrorText: !torProxy.loadedItem.validInput && torProxyEnable.loadedItem.checked
        actionItem: IPAddressValueInput {
            parentState: torProxy.state
            description: "127.0.0.1:9050"
            activeFocusOnTab: true
        }
        onClicked: {
            loadedItem.filled = true
            loadedItem.forceActiveFocus()
        }
    }
    Separator { Layout.fillWidth: true }
}
