pragma ComponentBehavior: Bound

// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs
import QtQuick.Layouts 1.15

import "../../../controls"

SettingsPage {
    id: root
    objectName: "settingsv2WalletSettingsPage"
    title: qsTr("Wallet settings")
    showBackButton: false

    property var wallet: walletController.selectedWallet
    property string errorText: ""
    property string pendingDisplayName: root.wallet ? root.wallet.displayName : ""
    readonly property bool walletLoaded: walletController.isWalletLoaded
    readonly property bool canManagePassphrase: root.wallet !== null && root.wallet.canManagePassphrase

    signal selectWalletRequested()
    signal passwordRequested()
    signal signVerifyMessageRequested()
    signal addressesRequested()

    function backupFileName() {
        const walletName = root.wallet && root.wallet.name.length > 0
            ? root.wallet.name.replace(/[\\/]/g, "_")
            : "wallet"
        return walletName + ".bak"
    }

    function backupDefaultFileUrl() {
        return "file://" + walletController.homePath() + "/" + root.backupFileName()
    }

    function resolvedBackupPath(rawPath) {
        let normalized = walletController.normalizeWalletPath(rawPath)
        if (normalized.length === 0) return ""

        const hasKnownSuffix = /\.(bak|dat)$/i.test(normalized)
        if (walletController.walletPathExists(normalized) && !hasKnownSuffix) {
            normalized += "/" + root.backupFileName()
        } else if (!hasKnownSuffix) {
            normalized += ".bak"
        }
        return normalized
    }

    function startBackup() {
        if (!root.wallet) return
        root.errorText = ""
        root.wallet.clearSettingsError()
        if (backupAutomationPath.text.length > 0) {
            const automatedPath = root.resolvedBackupPath(backupAutomationPath.text)
            backupAutomationPath.text = ""
            if (!root.wallet.backupWallet(automatedPath)) root.errorText = root.wallet.settingsError
            return
        }
        backupDialog.open()
    }

    FileDialog {
        id: backupDialog
        fileMode: FileDialog.SaveFile
        currentFolder: "file://" + walletController.homePath()
        selectedFile: root.backupDefaultFileUrl()
        defaultSuffix: "bak"
        nameFilters: [qsTr("Wallet backup files (*.bak *.dat)"), qsTr("All files (*)")]
        onAccepted: {
            if (backupDialog.selectedFile.toString().length === 0) return
            const normalized = root.resolvedBackupPath(backupDialog.selectedFile.toString())
            if (!root.wallet.backupWallet(normalized)) root.errorText = root.wallet.settingsError
            else root.errorText = ""
        }
    }

    TextField {
        id: backupAutomationPath
        objectName: "settingsv2WalletSettingsBackupPathField"
        visible: false
    }

    Connections {
        target: root.wallet

        function onSettingsErrorChanged() {
            root.errorText = root.wallet ? root.wallet.settingsError : ""
        }

        function onDisplayNameChanged() {
            root.pendingDisplayName = root.wallet ? root.wallet.displayName : ""
        }
    }

    Connections {
        target: walletController

        function onSelectedWalletChanged() {
            root.pendingDisplayName = root.wallet ? root.wallet.displayName : ""
            root.errorText = ""
        }
    }

    PageHeading {
        visible: !root.walletLoaded
        Layout.fillWidth: true
        title: qsTr("No wallet selected")
        description: qsTr("Select a wallet to manage wallet-specific settings.")
    }

    OutlineButton {
        visible: !root.walletLoaded
        Layout.preferredWidth: 220
        Layout.alignment: Qt.AlignHCenter
        text: qsTr("Select wallet")
        onClicked: root.selectWalletRequested()
    }

    FormSection {
        objectName: "settingsv2WalletInfoSection"
        visible: root.walletLoaded
        Layout.fillWidth: true
        title: qsTr("Wallet info")

        TextFieldRow {
            Layout.fillWidth: true
            title: qsTr("Name")
            fieldObjectName: "settingsv2WalletNameInput"
            fieldWidth: 220
            text: root.pendingDisplayName
            onTextEdited: function(text) { root.pendingDisplayName = text }
            onEditingFinished: {
                if (!root.wallet) return
                if (!walletController.setWalletDisplayName(root.wallet.name, root.pendingDisplayName)) {
                    root.errorText = root.wallet.settingsError
                }
            }
        }

        ValueRow {
            Layout.fillWidth: true
            title: qsTr("Key scheme")
            value: root.wallet ? root.wallet.keyScheme : ""
        }

        ValueRow {
            Layout.fillWidth: true
            title: qsTr("Private keys")
            value: root.wallet ? root.wallet.privateKeysStatus : ""
        }

        ValueRow {
            Layout.fillWidth: true
            title: qsTr("External signer")
            value: root.wallet ? root.wallet.externalSignerStatus : ""
            showDivider: false
        }
    }

    FormSection {
        objectName: "settingsv2WalletActionsSection"
        visible: root.walletLoaded
        Layout.fillWidth: true
        title: qsTr("Wallet actions")

        ListRow {
            Layout.fillWidth: true
            title: qsTr("Addresses")
            showsDisclosureIndicator: true
            onClicked: root.addressesRequested()
        }

        ListRow {
            visible: root.canManagePassphrase
            Layout.fillWidth: true
            title: root.wallet && root.wallet.isEncrypted ? qsTr("Update password") : qsTr("Set password")
            showsDisclosureIndicator: true
            onClicked: root.passwordRequested()
        }

        ListRow {
            Layout.fillWidth: true
            title: qsTr("Back up wallet")
            showsDisclosureIndicator: true
            onClicked: root.startBackup()
        }

        ListRow {
            Layout.fillWidth: true
            title: qsTr("Sign or verify message")
            showDivider: false
            showsDisclosureIndicator: true
            onClicked: root.signVerifyMessageRequested()
        }
    }

    FormRow {
        visible: root.errorText.length > 0
        Layout.fillWidth: true
        errorText: root.errorText
    }
}
