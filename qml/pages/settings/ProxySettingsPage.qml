// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

SettingsPage {
    id: root
    objectName: "proxySettingsPage"
    title: qsTr("Proxy settings")
    backButtonObjectName: "proxySettingsBackButton"

    required property var settingsModel
    property bool onboardingModal: false
    property var coreSettingsModel: settingsModel.coreSettings
    readonly property var proxySetting: coreSettingsModel.entry("proxy")
    readonly property var onionSetting: coreSettingsModel.entry("onion")
    readonly property CoreProxyDraft proxyDraft: CoreProxyDraft { source: root.coreSettingsModel }
    readonly property bool draftProxyEnabled: proxyDraft.proxyEnabled
    readonly property string draftProxyAddress: proxyDraft.proxyAddress
    readonly property string draftProxyValidationError: proxyDraft.proxyError
    readonly property bool draftTorEnabled: proxyDraft.torEnabled
    readonly property string draftTorAddress: proxyDraft.torAddress
    readonly property string draftTorValidationError: proxyDraft.torError
    readonly property bool proxyDraftDirty: proxyDraft.dirty
    readonly property bool canSaveProxyDraft: proxyDraft.canApply && proxyDraft.dirty
    readonly property bool awaitingApply: proxyDraft.operationState === CoreSettingsModel.Pending
    Connections {
        target: root.proxyDraft
        function onApplied() {
            root.exitAllowed()
            root.closeRequested()
        }
    }

    signal closeRequested()

    CoreText {
        Layout.fillWidth: true
        text: root.proxyDraft.error
        visible: text.length > 0
        wrapMode: Text.WordWrap
    }

    function resetProxyDraft() { proxyDraft.reset() }
    function updateProxyAddress(address) { proxyDraft.proxyAddress = address }
    function updateTorAddress(address) { proxyDraft.torAddress = address }
    function commitProxyDraft() {
        return canSaveProxyDraft && !awaitingApply && proxyDraft.requestApply()
    }
    function save() { commitProxyDraft() }

    function requestExit() { requestBack() }

    function requestBack() {
        if (awaitingApply) return
        if (root.proxyDraftDirty) {
            discardProxyChangesPopup.open()
            return
        }
        root.exitAllowed()
        root.closeRequested()
    }

    onBack: root.requestBack()
    Component.onCompleted: root.resetProxyDraft()

    ToastBanner {
        objectName: "proxyRestartNotice"
        shown: !root.onboardingModal && root.settingsModel.proxySettingsDirty
        Layout.fillWidth: true
        text: qsTr("Restart the application for these changes to take effect.")
    }

    FormSection {
        objectName: "defaultProxySection"
        Layout.fillWidth: true
        surfaceLevel: root.onboardingModal ? 1 : 0
        title: qsTr("Default proxy")
        description: qsTr("Route peer connections through a SOCKS5 proxy. IPv4, IPv6, and Tor connections are supported.")

        SwitchRow {
            Layout.fillWidth: true
            dividerColor: Theme.color.surfaceDivider
            title: qsTr("Enable")
            supportingText: root.proxySetting.infoText
            enabled: root.proxySetting.canEdit && root.proxyDraft.ready && !root.awaitingApply
            switchObjectName: "proxyEnableSwitch"
            checked: root.draftProxyEnabled
            onToggled: root.proxyDraft.proxyEnabled = checked
        }

        TextFieldRow {
            id: proxyAddressRow
            objectName: "proxyAddressRow"
            Layout.fillWidth: true
            title: qsTr("Proxy location")
            enabled: root.draftProxyEnabled && root.proxySetting.canEdit && root.proxyDraft.ready && !root.awaitingApply
            fieldObjectName: "proxyAddressInput"
            fieldWidth: 220
            text: root.draftProxyAddress
            placeholderText: root.proxySetting.defaultAddress()
            errorText: root.draftProxyEnabled ? root.draftProxyValidationError : ""
            showDivider: false
            onTextEdited: function(text) { root.updateProxyAddress(text) }
            onEditingFinished: {
                root.updateProxyAddress(proxyAddressRow.text)
            }
        }
    }

    FormSection {
        objectName: "torProxySection"
        Layout.fillWidth: true
        surfaceLevel: root.onboardingModal ? 1 : 0
        title: qsTr("Tor proxy")
        description: qsTr("Route Tor connections through a dedicated SOCKS5 proxy.")

        SwitchRow {
            Layout.fillWidth: true
            dividerColor: Theme.color.surfaceDivider
            title: qsTr("Enable")
            supportingText: root.onionSetting.infoText
            enabled: root.onionSetting.canEdit && root.proxyDraft.ready && !root.awaitingApply
            switchObjectName: "torEnableSwitch"
            checked: root.draftTorEnabled
            onToggled: root.proxyDraft.torEnabled = checked
        }

        TextFieldRow {
            id: torAddressRow
            objectName: "torAddressRow"
            Layout.fillWidth: true
            title: qsTr("Proxy location")
            enabled: root.draftTorEnabled && root.onionSetting.canEdit && root.proxyDraft.ready && !root.awaitingApply
            fieldObjectName: "torAddressInput"
            fieldWidth: 220
            text: root.draftTorAddress
            placeholderText: root.onionSetting.defaultAddress()
            errorText: root.draftTorEnabled ? root.draftTorValidationError : ""
            showDivider: false
            onTextEdited: function(text) { root.updateTorAddress(text) }
            onEditingFinished: {
                root.updateTorAddress(torAddressRow.text)
            }
        }
    }

    PrimaryButton {
        objectName: "proxySettingsSaveButton"
        Layout.alignment: Qt.AlignRight
        Layout.preferredWidth: 140
        text: qsTr("Save")
        enabled: root.canSaveProxyDraft
        onClicked: root.save()
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
            onTriggered: root.exitCancelled()
        }

        AlertAction {
            text: qsTr("Save")
            role: AlertAction.Normal
            buttonObjectName: "discardProxyChangesSaveButton"
            enabled: root.canSaveProxyDraft
            onTriggered: root.commitProxyDraft()
        }

        AlertAction {
            text: qsTr("Discard")
            role: AlertAction.Destructive
            buttonObjectName: "discardProxyChangesConfirmButton"
            onTriggered: {
                root.resetProxyDraft()
                root.exitAllowed()
                root.closeRequested()
            }
        }
    }
}
