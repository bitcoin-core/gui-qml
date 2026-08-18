pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../../controls"
import "../../../components"

SettingsPage {
    id: root
    objectName: "settingsv2ConnectionSettingsPage"
    title: qsTr("Connection")
    showBackButton: false

    property var settingsModel: optionsModel
    property var coreSettingsModel: settingsModel.coreSettings
    readonly property var listenSetting: coreSettingsModel.entry("listen")
    readonly property var natpmpSetting: coreSettingsModel.entry("natpmp")
    readonly property var serverSetting: coreSettingsModel.entry("server")

    SettingsRestartNotice {
        visible: root.settingsModel.connectionSettingsDirty
        Layout.fillWidth: true
    }

    FormSection {
        Layout.fillWidth: true
        title: qsTr("Incoming connections")

        FormRow {
            Layout.fillWidth: true
            title: qsTr("Enable listening")
            description: qsTr("Allow incoming peer connections.")
            supportingText: root.listenSetting.infoText
            enabled: root.listenSetting.canEdit
            trailingItem: OptionSwitch {
                objectName: "settingsv2ListenSwitch"
                checked: root.listenSetting.value
                onToggled: root.listenSetting.value = checked
            }
        }

        FormRow {
            Layout.fillWidth: true
            title: qsTr("Map port using NAT-PMP")
            supportingText: root.natpmpSetting.infoText
            enabled: root.natpmpSetting.canEdit
            trailingItem: OptionSwitch {
                objectName: "settingsv2NatpmpSwitch"
                checked: root.natpmpSetting.value
                onToggled: root.natpmpSetting.value = checked
            }
        }

        FormRow {
            Layout.fillWidth: true
            title: qsTr("Enable RPC server")
            supportingText: root.serverSetting.infoText
            enabled: root.serverSetting.canEdit
            showDivider: false
            trailingItem: OptionSwitch {
                objectName: "settingsv2ServerSwitch"
                checked: root.serverSetting.value
                onToggled: root.serverSetting.value = checked
            }
        }
    }

    FormSection {
        Layout.fillWidth: true
        title: qsTr("Privacy")

        ListRow {
            objectName: "settingsv2ProxySettingsRow"
            Layout.fillWidth: true
            title: qsTr("Proxy settings")
            description: qsTr("Route peer and Tor connections through SOCKS5 proxies.")
            showDivider: false
            showsDisclosureIndicator: true
            onClicked: root.StackView.view.push(proxyPage)
        }
    }

    Component {
        id: proxyPage

        ProxySettingsPage {
            settingsModel: root.settingsModel
            onCloseRequested: root.StackView.view.pop()
        }
    }
}
