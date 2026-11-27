// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    signal back

    id: root
    objectName: "settingsProxy"

    property var settingsModel: optionsModel
    property var coreSettingsModel: settingsModel.coreSettings
    property bool onboarding: false
    readonly property bool proxySettingsDirty: settingsModel.proxySettingsDirty
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
    readonly property bool canSaveProxyDraft: proxyDraftValid && torDraftValid

    background: null

    Component.onCompleted: resetProxyDraft()

    function displayAddress(setting) {
        return setting.address.length > 0 ? setting.address : setting.defaultAddress()
    }

    function resetProxyDraft() {
        draftProxyEnabled = proxySetting.enabled
        draftProxyAddress = displayAddress(proxySetting)
        draftProxyValidationError = proxySetting.validate(draftProxyAddress)
        draftTorEnabled = onionSetting.enabled
        draftTorAddress = displayAddress(onionSetting)
        draftTorValidationError = onionSetting.validate(draftTorAddress)
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
        if (!canSaveProxyDraft) return false
        if (!commitProxyDraftEntry(proxySetting, draftProxyEnabled, draftProxyAddress, draftProxyValidationError)) return false
        if (!commitProxyDraftEntry(onionSetting, draftTorEnabled, draftTorAddress, draftTorValidationError)) return false
        resetProxyDraft()
        return true
    }

    function done() {
        if (proxyDraftDirty && !commitProxyDraft()) return
        root.back()
    }

    function requestBack() {
        if (proxyDraftDirty) {
            discardProxyChangesPopup.open()
            return
        }
        root.back()
    }

    header: SettingsHeader {
        title: qsTr("Proxy settings")
        backButtonObjectName: "settingsProxyBack"
        onBack: root.requestBack()
        rightItem: NavButton {
            objectName: "settingsProxyDone"
            text: qsTr("Done")
            enabled: root.canSaveProxyDraft
            onClicked: root.done()
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: width
        clip: true

        ColumnLayout {
            width: Math.min(parent.width, 450)
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 0

            SettingsRestartNotice {
                Layout.fillWidth: true
                Layout.topMargin: 10
                Layout.bottomMargin: 20
                visible: !root.onboarding && root.settingsModel.proxySettingsDirty
            }

            ProxySettings {
                settingsModel: root.settingsModel
                proxyEnabled: root.draftProxyEnabled
                proxyAddress: root.draftProxyAddress
                proxyValidationError: root.draftProxyValidationError
                torEnabled: root.draftTorEnabled
                torAddress: root.draftTorAddress
                torValidationError: root.draftTorValidationError
                onProxyEnabledEdited: (enabled) => root.draftProxyEnabled = enabled
                onProxyAddressEdited: (address, validationError) => {
                    root.draftProxyAddress = address
                    root.draftProxyValidationError = validationError
                }
                onTorEnabledEdited: (enabled) => root.draftTorEnabled = enabled
                onTorAddressEdited: (address, validationError) => {
                    root.draftTorAddress = address
                    root.draftTorValidationError = validationError
                }
                Layout.fillWidth: true
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
                root.back()
            }
        }
    }
}
