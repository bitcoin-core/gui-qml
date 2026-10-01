// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"

SettingsPage {
    id: root
    objectName: "connectionSettingsPage"
    title: qsTr("Connection")
    showBackButton: false

    property var settingsModel: optionsModel
    property bool onboardingModal: false
    property var coreSettingsModel: settingsModel.coreSettings
    readonly property var listenSetting: coreSettingsModel.entry("listen")
    readonly property var natpmpSetting: coreSettingsModel.entry("natpmp")
    readonly property var serverSetting: coreSettingsModel.entry("server")

    SettingsRestartNotice {
        visible: !root.onboardingModal && root.settingsModel.connectionSettingsDirty
        Layout.fillWidth: true
    }

    FormSection {
        Layout.fillWidth: true
        title: qsTr("Incoming connections")
        isOnSurface: root.onboardingModal
        showGradientBorder: false

        FormRow {
            Layout.fillWidth: true
            title: qsTr("Enable listening")
            dividerColor: root.onboardingModal ? Theme.color.neutral3 : Theme.color.neutral2
            description: qsTr("Allow incoming peer connections.")
            supportingText: root.listenSetting.infoText
            enabled: root.listenSetting.canEdit
            trailingItem: OptionSwitch {
                objectName: "listenSwitch"
                checked: root.listenSetting.value
                onToggled: root.listenSetting.value = checked
            }
        }

        FormRow {
            Layout.fillWidth: true
            title: qsTr("Map port using NAT-PMP")
            dividerColor: root.onboardingModal ? Theme.color.neutral3 : Theme.color.neutral2
            supportingText: root.natpmpSetting.infoText
            enabled: root.natpmpSetting.canEdit
            trailingItem: OptionSwitch {
                objectName: "natpmpSwitch"
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
                objectName: "serverSwitch"
                checked: root.serverSetting.value
                onToggled: root.serverSetting.value = checked
            }
        }
    }

    FormSection {
        Layout.fillWidth: true
        title: qsTr("Privacy")
        isOnSurface: root.onboardingModal
        showGradientBorder: false

        ListRow {
            objectName: "proxySettingsRow"
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
            onboardingModal: root.onboardingModal
            onCloseRequested: root.StackView.view.pop()
        }
    }
}
