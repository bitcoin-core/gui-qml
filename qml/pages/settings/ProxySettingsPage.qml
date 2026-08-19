pragma ComponentBehavior: Bound

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
    objectName: "proxySettingsPage"
    title: qsTr("Proxy settings")
    backButtonObjectName: "proxySettingsBackButton"

    property var settingsModel: optionsModel
    property var coreSettingsModel: settingsModel.coreSettings
    readonly property var proxySetting: coreSettingsModel.entry("proxy")
    readonly property var onionSetting: coreSettingsModel.entry("onion")
    property bool draftProxyEnabled: false
    property string draftProxyAddress: ""
    property string draftProxyValidationError: ""
    property bool draftTorEnabled: false
    property string draftTorAddress: ""
    property string draftTorValidationError: ""
    readonly property bool proxyDraftDirty: draftProxyEnabled !== proxySetting.enabled
        || draftProxyAddress !== displayAddress(proxySetting)
        || draftTorEnabled !== onionSetting.enabled
        || draftTorAddress !== displayAddress(onionSetting)
    readonly property bool proxyDraftValid: !draftProxyEnabled || draftProxyValidationError.length === 0
    readonly property bool torDraftValid: !draftTorEnabled || draftTorValidationError.length === 0
    readonly property bool canSaveProxyDraft: proxyDraftDirty && proxyDraftValid && torDraftValid

    signal closeRequested()

    function displayAddress(setting) {
        return setting.address.length > 0 ? setting.address : setting.defaultAddress()
    }

    function validateAddress(setting, address) {
        return setting.validate(address.trim())
    }

    function resetProxyDraft() {
        root.draftProxyEnabled = root.proxySetting.enabled
        root.draftProxyAddress = root.displayAddress(root.proxySetting)
        root.draftProxyValidationError = root.validateAddress(root.proxySetting, root.draftProxyAddress)
        root.draftTorEnabled = root.onionSetting.enabled
        root.draftTorAddress = root.displayAddress(root.onionSetting)
        root.draftTorValidationError = root.validateAddress(root.onionSetting, root.draftTorAddress)
    }

    function updateProxyAddress(address) {
        root.draftProxyAddress = address
        root.draftProxyValidationError = root.validateAddress(root.proxySetting, address)
    }

    function updateTorAddress(address) {
        root.draftTorAddress = address
        root.draftTorValidationError = root.validateAddress(root.onionSetting, address)
    }

    function commitProxyDraftEntry(setting, enabled, address, validationError) {
        const trimmedAddress = address.trim()
        if (!setting.canEdit) return true
        if (validationError.length === 0 && trimmedAddress !== setting.address) {
            if (!setting.commitAddress(trimmedAddress)) return false
        }
        if (setting.enabled !== enabled) {
            setting.enabled = enabled
            if (setting.enabled !== enabled) return false
        }
        return true
    }

    function commitProxyDraft() {
        if (!root.canSaveProxyDraft) return false
        if (!root.commitProxyDraftEntry(
                root.proxySetting,
                root.draftProxyEnabled,
                root.draftProxyAddress,
                root.draftProxyValidationError)) return false
        if (!root.commitProxyDraftEntry(
                root.onionSetting,
                root.draftTorEnabled,
                root.draftTorAddress,
                root.draftTorValidationError)) return false
        root.resetProxyDraft()
        return true
    }

    function save() {
        if (!root.commitProxyDraft()) return
        root.closeRequested()
    }

    function requestBack() {
        if (root.proxyDraftDirty) {
            discardProxyChangesPopup.open()
            return
        }
        root.closeRequested()
    }

    onBack: root.requestBack()
    Component.onCompleted: root.resetProxyDraft()

    rightItem: NavButton {
        objectName: "proxySettingsSaveButton"
        text: qsTr("Save")
        enabled: root.canSaveProxyDraft
        onClicked: root.save()
    }

    SettingsRestartNotice {
        objectName: "proxyRestartNotice"
        visible: root.settingsModel.proxySettingsDirty
        Layout.fillWidth: true
    }

    FormSection {
        objectName: "defaultProxySection"
        Layout.fillWidth: true
        title: qsTr("Default proxy")
        description: qsTr("Route peer connections through a SOCKS5 proxy. IPv4, IPv6, and Tor connections are supported.")

        FormRow {
            Layout.fillWidth: true
            title: qsTr("Enable")
            supportingText: root.proxySetting.infoText
            enabled: root.proxySetting.canEdit
            trailingItem: OptionSwitch {
                objectName: "proxyEnableSwitch"
                checked: root.draftProxyEnabled
                onToggled: root.draftProxyEnabled = checked
            }
        }

        TextFieldRow {
            id: proxyAddressRow
            objectName: "proxyAddressRow"
            Layout.fillWidth: true
            title: qsTr("Proxy location")
            enabled: root.draftProxyEnabled && root.proxySetting.canEdit
            fieldObjectName: "proxyAddressInput"
            fieldWidth: 220
            text: root.draftProxyAddress
            placeholderText: root.proxySetting.defaultAddress()
            errorText: root.draftProxyEnabled ? root.draftProxyValidationError : ""
            showDivider: false
            onTextEdited: function(text) { root.updateProxyAddress(text) }
            onEditingFinished: {
                root.updateProxyAddress(proxyAddressRow.text)
                if (root.draftProxyValidationError.length === 0) {
                    root.draftProxyAddress = proxyAddressRow.text.trim()
                }
            }
        }
    }

    FormSection {
        objectName: "torProxySection"
        Layout.fillWidth: true
        title: qsTr("Tor proxy")
        description: qsTr("Route Tor connections through a dedicated SOCKS5 proxy.")

        FormRow {
            Layout.fillWidth: true
            title: qsTr("Enable")
            supportingText: root.onionSetting.infoText
            enabled: root.onionSetting.canEdit
            trailingItem: OptionSwitch {
                objectName: "torEnableSwitch"
                checked: root.draftTorEnabled
                onToggled: root.draftTorEnabled = checked
            }
        }

        TextFieldRow {
            id: torAddressRow
            objectName: "torAddressRow"
            Layout.fillWidth: true
            title: qsTr("Proxy location")
            enabled: root.draftTorEnabled && root.onionSetting.canEdit
            fieldObjectName: "torAddressInput"
            fieldWidth: 220
            text: root.draftTorAddress
            placeholderText: root.onionSetting.defaultAddress()
            errorText: root.draftTorEnabled ? root.draftTorValidationError : ""
            showDivider: false
            onTextEdited: function(text) { root.updateTorAddress(text) }
            onEditingFinished: {
                root.updateTorAddress(torAddressRow.text)
                if (root.draftTorValidationError.length === 0) {
                    root.draftTorAddress = torAddressRow.text.trim()
                }
            }
        }
    }

    AlertPopup {
        id: discardProxyChangesPopup
        objectName: "discardProxyChangesPopup"
        parent: Overlay.overlay
        title: qsTr("Discard changes?")
        message: qsTr("This will discard your proxy settings changes.")
        messageObjectName: "discardProxyChangesMessage"

        AlertAction {
            text: qsTr("Cancel")
            role: AlertAction.Cancel
            buttonObjectName: "discardProxyChangesCancelButton"
        }

        AlertAction {
            text: qsTr("Discard")
            role: AlertAction.Destructive
            buttonObjectName: "discardProxyChangesConfirmButton"
            onTriggered: {
                root.resetProxyDraft()
                root.closeRequested()
            }
        }
    }
}
