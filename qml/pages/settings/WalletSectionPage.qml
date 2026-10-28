// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../components"
import "../../controls"

SettingsPage {
    id: root
    objectName: "walletSettingsPage"
    title: qsTr("Wallet settings")
    showBackButton: false

    property var wallet: walletController.selectedWallet
    property QtObject backupWallet: null
    property string backupWalletName: ""
    property string backupPath: ""
    property string errorText: ""
    readonly property bool walletLoaded: walletController.isWalletLoaded
    readonly property bool noWalletsAvailable: walletController.noWalletsFound
    readonly property bool canManagePassphrase: root.wallet !== null && root.wallet.canManagePassphrase

    signal selectWalletRequested()
    signal passwordRequested()
    signal signVerifyMessageRequested()
    signal addressesRequested()
    signal coinsRequested()

    function backupFileName(walletName) {
        const fileName = walletName && walletName.length > 0
            ? walletName.replace(/[\\/]/g, "_")
            : "wallet"
        return fileName + ".bak"
    }

    function backupDefaultFileUrl(walletName) {
        return "file://" + walletController.homePath() + "/" + root.backupFileName(walletName)
    }

    function resolvedBackupPath(rawPath, walletName) {
        let normalized = walletController.normalizeWalletPath(rawPath)
        if (normalized.length === 0) return ""

        const hasKnownSuffix = /\.(bak|dat)$/i.test(normalized)
        if (walletController.walletPathExists(normalized) && !hasKnownSuffix) {
            normalized += "/" + root.backupFileName(walletName)
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
            const automatedPath = root.resolvedBackupPath(backupAutomationPath.text, root.wallet.name)
            backupAutomationPath.text = ""
            if (!root.wallet.backupWallet(automatedPath)) root.errorText = root.wallet.settingsError
            return
        }
        root.backupWallet = root.wallet
        root.backupWalletName = root.wallet.name
        backupDialog.open()
    }

    function confirmBackupPath(rawPath) {
        root.backupPath = rawPath.length > 0 ? root.resolvedBackupPath(rawPath, root.backupWalletName) : ""
        if (root.backupPath.length === 0) {
            root.clearBackupTarget()
            return
        }
        backupConfirmation.open()
    }

    function finishBackup() {
        const target = root.backupWallet
        const path = root.backupPath
        root.clearBackupTarget()
        if (!target) return
        if (!target.backupWallet(path)) root.errorText = target.settingsError
        else root.errorText = ""
    }

    function clearBackupTarget() {
        root.backupWallet = null
        root.backupWalletName = ""
        root.backupPath = ""
    }

    AppFileDialog {
        id: backupDialog
        objectName: "walletSettingsBackupDialog"
        title: qsTr("Back up wallet “%1”").arg(root.backupWalletName)
        fileMode: AppFileDialog.SaveFile
        currentFolder: "file://" + walletController.homePath()
        currentFile: root.backupDefaultFileUrl(root.backupWalletName)
        defaultSuffix: "bak"
        nameFilters: [qsTr("Wallet backup files (*.bak *.dat)"), qsTr("All files (*)")]
        onAccepted: root.confirmBackupPath(backupDialog.selectedFile.toString())
        onRejected: root.clearBackupTarget()
    }

    // The native save panel cannot show which wallet is being backed up on
    // every platform, so the pinned wallet and destination are confirmed here.
    AlertPopup {
        id: backupConfirmation
        objectName: "walletBackupConfirmationPopup"
        parent: Overlay.overlay
        implicitWidth: 520
        title: qsTr("Confirm wallet backup")
        details: [
            BackupDetail {
                label: qsTr("Wallet")
                value: root.backupWalletName
                valueObjectName: "walletBackupConfirmationWalletName"
            },
            BackupDetail {
                label: qsTr("Save to")
                value: root.backupPath
                valueObjectName: "walletBackupConfirmationPath"
            },
            CoreText {
                objectName: "walletBackupConfirmationError"
                visible: root.backupWallet === null
                Layout.fillWidth: true
                text: qsTr("This wallet was closed. Open it again and restart the backup.")
                color: Theme.color.red
                font: Theme.text.description.font
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }
        ]

        AlertAction {
            text: qsTr("Cancel")
            role: AlertAction.Cancel
            buttonObjectName: "walletBackupConfirmationCancelButton"
            onTriggered: root.clearBackupTarget()
        }

        AlertAction {
            text: qsTr("Back up")
            enabled: root.backupWallet !== null
            buttonObjectName: "walletBackupConfirmationBackupButton"
            onTriggered: root.finishBackup()
        }
    }

    component BackupDetail: ColumnLayout {
        id: detail
        property string label: ""
        property string value: ""
        property string valueObjectName: ""
        Layout.fillWidth: true
        spacing: 2

        CoreText {
            Layout.fillWidth: true
            text: detail.label
            color: Theme.color.neutral9
            font: Theme.text.captionStrong.font
            horizontalAlignment: Text.AlignLeft
        }

        CoreText {
            objectName: detail.valueObjectName
            Layout.fillWidth: true
            text: detail.value
            color: Theme.color.neutral8
            font: Theme.text.description.font
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
    }

    TextField {
        id: backupAutomationPath
        objectName: "walletSettingsBackupPathField"
        visible: false
    }

    Connections {
        target: root.wallet

        function onSettingsErrorChanged() {
            root.errorText = root.wallet ? root.wallet.settingsError : ""
        }
    }

    Connections {
        target: walletController

        function onSelectedWalletChanged() {
            root.errorText = ""
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

    OutlineButton {
        objectName: "walletSettingsEmptyAction"
        visible: !root.walletLoaded
        Layout.preferredWidth: 220
        Layout.alignment: Qt.AlignHCenter
        text: root.noWalletsAvailable ? qsTr("Add wallet") : qsTr("Select wallet")
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
