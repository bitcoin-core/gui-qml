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

    property var wallet: null
    property string errorText: ""
    property string resultText: ""
    property bool resultSuccess: false
    readonly property bool walletLoaded: root.wallet !== null
    readonly property bool noWalletsAvailable: walletController.noWalletsFound
    readonly property bool canManagePassphrase: root.wallet !== null && root.wallet.canManagePassphrase
    readonly property bool canShowDescriptors: root.walletLoaded && !!root.wallet && root.wallet.canShowDescriptors
    readonly property bool watchOnlyExportPending: !!root.wallet && root.wallet.watchOnlyWalletExportPending

    signal selectWalletRequested()
    signal passwordRequested()
    signal signVerifyMessageRequested()
    signal addressesRequested()
    signal coinsRequested()
    signal descriptorsRequested()

    function folderOf(path) {
        return path.substring(0, Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\")))
    }

    function showResult(success, text) {
        root.resultSuccess = success
        root.resultText = text
    }

    function startBackup(watchOnly) {
        if (!root.wallet) return
        root.errorText = ""
        root.resultText = ""
        root.wallet.clearSettingsError()
        backupPopup.targetWallet = root.wallet
        backupPopup.canChooseType = root.wallet.canExportWatchOnlyWallet
        backupPopup.watchOnly = watchOnly === true && backupPopup.canChooseType
        backupPopup.errorText = ""
        if (backupAutomationPath.text.length > 0) {
            backupPopup.destinationPath = walletController.normalizeWalletPath(backupAutomationPath.text)
            backupAutomationPath.text = ""
        } else {
            backupPopup.destinationPath = walletController.defaultWalletFilePath(root.wallet, backupPopup.watchOnly)
        }
        backupPopup.open()
    }

    // A default name follows the chosen type; a name the person picked is kept.
    function followDefaultFileName() {
        const path = backupPopup.destinationPath
        if (!root.wallet || path.length === 0) return
        const folder = root.folderOf(path)
        if (path === walletController.defaultWalletFilePath(root.wallet, !backupPopup.watchOnly, folder)) {
            backupPopup.destinationPath = walletController.defaultWalletFilePath(root.wallet, backupPopup.watchOnly, folder)
        }
    }

    function chooseBackupLocation() {
        const path = backupPopup.destinationPath
        backupLocationDialog.defaultSuffix = backupPopup.watchOnly ? "dat" : "bak"
        backupLocationDialog.currentFolder = walletController.homeFileUrl(root.folderOf(path))
        backupLocationDialog.currentFile = walletController.homeFileUrl(path)
        backupLocationDialog.open()
    }

    function saveBackup() {
        const wallet = backupPopup.targetWallet
        if (!wallet || wallet !== root.wallet) return
        backupPopup.errorText = ""
        const destination = walletController.walletFileDestination(wallet, backupPopup.destinationPath, backupPopup.watchOnly)
        if (destination.status === "blocked") {
            backupPopup.errorText = destination.error
            return
        }
        backupPopup.destinationPath = destination.path
        if (destination.status === "available") {
            root.writeWalletFile(destination.path, false)
            return
        }
        replaceAlert.destination = destination
        replaceAlert.open()
    }

    function writeWalletFile(path, replace) {
        const wallet = backupPopup.targetWallet
        if (!wallet || wallet !== root.wallet) return
        backupPopup.destinationPath = path
        if (backupPopup.watchOnly) {
            if (!wallet.exportWatchOnlyWallet(path, replace)) {
                backupPopup.errorText = qsTr("The watch-only copy could not be saved.")
            }
            return
        }
        if (wallet.backupWallet(path, replace)) {
            backupPopup.close()
            //: %1 is the path of the saved wallet backup.
            root.showResult(true, qsTr("Backup saved to %1").arg(path))
        } else {
            backupPopup.errorText = wallet.settingsError
            wallet.clearSettingsError()
        }
    }

    BackupWalletPopup {
        id: backupPopup
        property var targetWallet: null
        encrypted: !!root.wallet && root.wallet.isEncrypted
        pending: root.watchOnlyExportPending
        onWatchOnlyChanged: root.followDefaultFileName()
        onLocationChangeRequested: root.chooseBackupLocation()
        onSaveRequested: root.saveBackup()
    }

    AppFileDialog {
        id: backupLocationDialog
        objectName: "walletSettingsBackupDialog"
        fileMode: AppFileDialog.SaveFile
        // Saving asks before replacing, with what would be lost.
        confirmOverwrite: false
        nameFilters: [qsTr("Wallet files (*.bak *.dat)"), qsTr("All files (*)")]
        onAccepted: {
            if (!backupPopup.visible || selectedFile.toString().length === 0) return
            backupPopup.destinationPath = walletController.normalizeWalletPath(selectedFile.toString())
            backupPopup.errorText = ""
        }
    }

    AlertPopup {
        id: replaceAlert
        objectName: "backupReplaceAlert"
        parent: Overlay.overlay
        implicitWidth: 440
        property var destination: ({})
        readonly property bool walletFile: destination.status === "walletFile"
        title: walletFile ? qsTr("Replace wallet file?") : qsTr("Replace file?")
        messageObjectName: "backupReplaceMessage"
        //: %1 is the name of a file that already exists.
        message: !walletFile ? qsTr("A file named %1 already exists. Replacing deletes it permanently.").arg(destination.fileName || "")
            : backupPopup.watchOnly
            //: %1 is the name of a wallet file that already exists.
            ? qsTr("A wallet file named %1 already exists. Replacing deletes it permanently. If it holds private keys from another wallet or an older backup, funds they control could be lost. This copy has no private keys to replace them.").arg(destination.fileName || "")
            //: %1 is the name of a wallet file that already exists.
            : qsTr("A wallet file named %1 already exists. Replacing deletes it permanently. If it holds private keys from another wallet or an older backup, funds they control could be lost.").arg(destination.fileName || "")
        detail: Component {
            Rectangle {
                implicitHeight: replaceRows.implicitHeight + 24
                radius: 10
                color: Theme.color.neutral2

                GridLayout {
                    id: replaceRows
                    anchors.fill: parent
                    anchors.margins: 12
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 6

                    CoreText {
                        Layout.alignment: Qt.AlignTop
                        text: qsTr("Folder")
                        font: Theme.text.caption.font
                        color: Theme.color.neutral7
                        horizontalAlignment: Text.AlignLeft
                        wrap: false
                    }
                    CoreText {
                        objectName: "backupReplaceFolder"
                        Layout.fillWidth: true
                        text: replaceAlert.destination.folder || ""
                        font: Theme.text.monoCaption.font
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.WrapAnywhere
                    }
                    CoreText {
                        Layout.alignment: Qt.AlignTop
                        text: qsTr("Existing")
                        font: Theme.text.caption.font
                        color: Theme.color.neutral7
                        horizontalAlignment: Text.AlignLeft
                        wrap: false
                    }
                    CoreText {
                        objectName: "backupReplaceExistingName"
                        Layout.fillWidth: true
                        text: replaceAlert.destination.fileName || ""
                        font: Theme.text.monoCaption.font
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.WrapAnywhere
                    }
                    CoreText {
                        Layout.alignment: Qt.AlignTop
                        text: qsTr("Keep both")
                        font: Theme.text.caption.font
                        color: Theme.color.neutral7
                        horizontalAlignment: Text.AlignLeft
                        wrap: false
                    }
                    CoreText {
                        objectName: "backupReplaceKeepBothName"
                        Layout.fillWidth: true
                        text: replaceAlert.destination.keepBothFileName || ""
                        font: Theme.text.monoCaption.font
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }
        }

        AlertAction {
            text: qsTr("Cancel")
            role: AlertAction.Cancel
            buttonObjectName: "backupReplaceCancel"
        }
        AlertAction {
            //: Saves the new file under another name and keeps the existing one.
            text: qsTr("Keep both")
            buttonObjectName: "backupReplaceKeepBoth"
            onTriggered: root.writeWalletFile(replaceAlert.destination.keepBothPath, false)
        }
        AlertAction {
            text: qsTr("Replace")
            role: AlertAction.Destructive
            buttonObjectName: "backupReplaceConfirm"
            onTriggered: root.writeWalletFile(replaceAlert.destination.path, true)
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

        function onWatchOnlyWalletExported(path) {
            backupPopup.close()
            //: %1 is the path of the saved watch-only wallet file.
            root.showResult(true, qsTr("Watch-only copy saved to %1").arg(path))
        }

        function onWatchOnlyWalletExportFailed(error) {
            if (backupPopup.visible) backupPopup.errorText = error
            else root.showResult(false, error)
        }
    }

    onWalletChanged: {
        root.errorText = ""
        root.resultText = ""
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
            objectName: "walletDescriptorsRow"
            visible: root.canShowDescriptors
            Layout.fillWidth: true
            //: Shows the wallet's public output descriptors and extended public keys.
            title: qsTr("Descriptors")
            //: Under the Descriptors row, so people looking for their xpub find it here.
            description: qsTr("Public descriptors and extended keys (xpub) for watch-only wallets.")
            showsDisclosureIndicator: true
            onClicked: root.descriptorsRequested()
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
            enabled: !root.watchOnlyExportPending
            Layout.fillWidth: true
            title: qsTr("Back up or export wallet")
            description: root.watchOnlyExportPending ? qsTr("Saving a watch-only copy...") : ""
            showsDisclosureIndicator: true
            onClicked: root.startBackup(false)
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

    ToastBanner {
        objectName: "walletSettingsResultBanner"
        visible: root.resultText.length > 0
        Layout.fillWidth: true
        readonly property color resultColor: root.resultSuccess ? Theme.color.green : Theme.color.red
        backgroundColor: Qt.rgba(resultColor.r, resultColor.g, resultColor.b, 0.25)
        iconSource: root.resultSuccess ? "image://images/check" : "image://images/info-filled"
        iconColor: resultColor
        text: root.resultText
        textObjectName: "walletSettingsResultText"
        textColor: resultColor
    }
}
