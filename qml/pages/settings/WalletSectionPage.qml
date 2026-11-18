// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQml.Models 2.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

SettingsPage {
    id: root
    objectName: "walletSettingsPage"
    title: qsTr("Wallet settings")
    showBackButton: false

    property WalletQmlModel wallet: null
    readonly property string errorText: root.wallet
        ? (root.wallet.settingsError.length > 0 ? root.wallet.settingsError
            : root.wallet.backupOperation ? root.wallet.backupOperation.error : "")
        : ""
    readonly property bool walletLoaded: root.wallet !== null
    readonly property bool noWalletsAvailable: ApplicationModels.walletController.noWalletsFound
    readonly property bool canManagePassphrase: root.wallet !== null && root.wallet.canManagePassphrase

    signal selectWalletRequested()
    signal passwordRequested()
    signal signVerifyMessageRequested()
    signal addressesRequested()
    signal coinsRequested()

    function startBackup() {
        if (!root.wallet) return
        root.wallet.clearSettingsError()
        if (backupAutomationPath.text.length > 0) {
            const automatedPath = backupAutomationPath.text
            backupAutomationPath.text = ""
            root.wallet.backupWallet(automatedPath)
            return
        }
        backupDialog.open()
    }

    AppFileDialog {
        id: backupDialog
        objectName: "walletSettingsBackupDialog"
        fileMode: AppFileDialog.SaveFile
        currentFolder: ApplicationModels.walletController.homeUrl
        suggestedName: root.wallet ? root.wallet.backupFileName : "wallet.bak"
        defaultSuffix: "bak"
        confirmOverwrite: false // The operation confirms the resolved final path.
        nameFilters: [qsTr("Wallet backup files (*.bak *.dat)"), qsTr("All files (*)")]
        contextObject: root.wallet
        enabled: root.visible && root.wallet !== null && root.wallet.available
        onChosen: (context, file) => { if (context === root.wallet) root.wallet.backupWallet(file.toString()) }
    }

    TextField {
        id: backupAutomationPath
        objectName: "walletSettingsBackupPathField"
        visible: false
    }

    Instantiator {
        model: root.wallet && root.wallet.backupOperation ? [root.wallet.backupOperation] : []
        delegate: SaveConfirmationPopup {
            required property BackupFileOperation modelData
            objectName: "walletBackupOverwritePopup"
            parent: root.Overlay.overlay
            visible: root.visible && modelData.confirmationRequired
            replacing: modelData.replacing
            finalPath: modelData.finalPath
            saveTitle: qsTr("Save backup?")
            replaceTitle: qsTr("Replace backup?")
            pathObjectName: "walletBackupFinalPathText"
            approveObjectName: "walletBackupOverwriteApproveButton"
            refuseObjectName: "walletBackupOverwriteRejectButton"
            onApproved: modelData.approve()
            onRefused: modelData.refuse()
        }
    }

    PageHeading {
        objectName: "walletSettingsEmptyHeading"
        visible: !root.walletLoaded
        Layout.fillWidth: true
        title: root.noWalletsAvailable ? "" : qsTr("No wallet selected")
        description: root.noWalletsAvailable
            ? qsTr("Add a wallet to manage its settings.")
            : qsTr("Select a wallet to manage wallet-specific settings.")
        titleAlignment: Text.AlignHCenter
    }

    NeutralButton {
        objectName: "walletSettingsEmptyAction"
        buttonSize: NeutralButton.Large
        visible: !root.walletLoaded
        Layout.preferredWidth: 220
        Layout.alignment: Qt.AlignHCenter
        text: root.noWalletsAvailable ? qsTr("Add wallet…") : qsTr("Select wallet…")
        onClicked: root.selectWalletRequested()
    }

    FormSection {
        objectName: "walletInfoSection"
        visible: root.walletLoaded
        Layout.fillWidth: true
        title: qsTr("Wallet info")

        ValueRow {
            objectName: "walletNameRow"
            Layout.fillWidth: true
            title: qsTr("Name")
            value: root.wallet ? root.wallet.displayName : ""
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
        objectName: "walletActionsSection"
        visible: root.walletLoaded
        Layout.fillWidth: true
        title: qsTr("Wallet actions")

        ListRow {
            objectName: "walletAddressesRow"
            Layout.fillWidth: true
            title: qsTr("Addresses")
            showsDisclosureIndicator: true
            onClicked: root.addressesRequested()
        }

        ListRow {
            objectName: "walletCoinsRow"
            Layout.fillWidth: true
            //: Opens the wallet's unspent coin browser.
            title: qsTr("View coins")
            showsDisclosureIndicator: true
            onClicked: root.coinsRequested()
        }

        ListRow {
            objectName: "walletPasswordRow"
            visible: root.canManagePassphrase
            Layout.fillWidth: true
            title: root.wallet && root.wallet.isEncrypted ? qsTr("Update password") : qsTr("Set password")
            showsDisclosureIndicator: true
            onClicked: root.passwordRequested()
        }

        ListRow {
            objectName: "walletBackupRow"
            Layout.fillWidth: true
            title: qsTr("Back up wallet")
            enabled: root.wallet && (!root.wallet.backupOperation || !root.wallet.backupOperation.pending)
            supportingText: root.wallet && root.wallet.backupOperation
                ? (root.wallet.backupOperation.pending ? qsTr("Preparing backup...")
                    : root.wallet.backupOperation.phase === BackupFileOperation.Succeeded ? qsTr("Backup saved.") : "")
                : ""
            description: root.wallet && root.wallet.isEncrypted
                ? qsTr("After setting a password, earlier backups may not cover new receiving addresses or change. Make a fresh backup. Keep your older backups in a safe place.")
                : ""
            showsDisclosureIndicator: true
            onClicked: root.startBackup()
        }

        ListRow {
            objectName: "walletSignVerifyMessageRow"
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
