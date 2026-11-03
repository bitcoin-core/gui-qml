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

    ToastBanner {
        shown: !root.onboardingModal && root.settingsModel.connectionSettingsDirty
        Layout.fillWidth: true
        text: qsTr("Restart the application for these changes to take effect.")
    }

    FormSection {
        Layout.fillWidth: true
        title: qsTr("Incoming connections")
        surfaceLevel: root.onboardingModal ? 1 : 0

        SwitchRow {
            objectName: "listenSettingRow"
            Layout.fillWidth: true
            title: qsTr("Enable listening")
            dividerColor: Theme.color.surfaceDivider
            description: qsTr("Allow incoming peer connections.")
            supportingText: root.listenSetting.infoText
            enabled: root.listenSetting.canEdit
            switchObjectName: "listenSwitch"
            checked: root.listenSetting.value
            onToggled: root.listenSetting.value = checked
        }

        SwitchRow {
            Layout.fillWidth: true
            title: qsTr("Map port using NAT-PMP")
            dividerColor: Theme.color.surfaceDivider
            supportingText: root.natpmpSetting.infoText
            enabled: root.natpmpSetting.canEdit
            switchObjectName: "natpmpSwitch"
            checked: root.natpmpSetting.value
            onToggled: root.natpmpSetting.value = checked
        }

        SwitchRow {
            Layout.fillWidth: true
            title: qsTr("Enable RPC server")
            supportingText: root.serverSetting.infoText
            enabled: root.serverSetting.canEdit
            showDivider: false
            switchObjectName: "serverSwitch"
            checked: root.serverSetting.value
            onToggled: root.serverSetting.value = checked
        }
    }

    FormSection {
        Layout.fillWidth: true
        title: qsTr("Privacy")
        surfaceLevel: root.onboardingModal ? 1 : 0

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
