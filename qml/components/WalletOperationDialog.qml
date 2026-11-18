// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

Item {
    id: root
    required property WalletOperationModel operation
    required property WalletOperationPresentationModel presenter
    required property bool active
    property bool showResult: true
    property real maximumWidth: 420

    // Each delegate captures an immutable prompt. A delayed Popup close or
    // input callback answers that prompt, never its successor.
    Repeater {
        model: root.operation.prompts
        delegate: Item {
            id: session
            required property WalletOperationPrompt modelData
            readonly property WalletOperationPrompt prompt: modelData
            WalletMigrationPopup {
                parent: Overlay.overlay
                width: Math.min(root.maximumWidth, parent ? parent.width - 40 : root.maximumWidth)
                popupObjectName: root.active && root.operation.confirmationPrompt === session.prompt ? "walletMigrationPopup" : ""
                errorTextObjectName: root.active && root.operation.confirmationPrompt === session.prompt ? "walletMigrationErrorText" : ""
                cancelButtonObjectName: root.active && root.operation.confirmationPrompt === session.prompt ? "walletMigrationCancelButton" : ""
                confirmButtonObjectName: root.active && root.operation.confirmationPrompt === session.prompt ? "walletMigrationConfirmButton" : ""
                titleText: session.prompt.title
                descriptionText: session.prompt.message
                errorText: session.prompt.error.message
                errorDetailsModel: root.operation.errorDetails
                busy: session.prompt.busy
                visible: root.active && root.operation.prompt === session.prompt && session.prompt.kind === WalletOperationPrompt.Confirmation
                onConfirmed: session.prompt.confirm()
                onClosed: root.presenter.reportClosed(root.operation, session.prompt)
            }
            WalletPassphrasePopup {
                parent: Overlay.overlay
                width: Math.min(root.maximumWidth, parent ? parent.width - 40 : root.maximumWidth)
                popupObjectName: root.active && root.operation.passphrasePrompt === session.prompt ? "walletMigrationPassphrasePopup" : ""
                passphraseFieldObjectName: root.active && root.operation.passphrasePrompt === session.prompt ? "walletMigrationPassphraseField" : ""
                errorTextObjectName: root.active && root.operation.passphrasePrompt === session.prompt ? "walletMigrationPassphraseErrorText" : ""
                cancelButtonObjectName: root.active && root.operation.passphrasePrompt === session.prompt ? "walletMigrationPassphraseCancelButton" : ""
                confirmButtonObjectName: root.active && root.operation.passphrasePrompt === session.prompt ? "walletMigrationPassphraseConfirmButton" : ""
                titleText: session.prompt.title
                descriptionText: session.prompt.message
                errorText: session.prompt.error.message
                errorDetailsModel: root.operation.errorDetails
                busy: session.prompt.busy
                visible: root.active && root.operation.prompt === session.prompt && session.prompt.kind === WalletOperationPrompt.Passphrase
                onSubmitted: function(secret) { session.prompt.submit(secret) }
                onClosed: root.presenter.reportClosed(root.operation, session.prompt)
            }
        }
    }
    DialogPopup {
        id: resultPopup
        objectName: root.active ? "walletMigrationResultPopup" : ""
        parent: Overlay.overlay
        implicitWidth: root.maximumWidth
        // Never taller than the space the dialog gets, so the details list,
        // not the actions below it, gives way in a short window.
        implicitHeight: parent ? Math.min(620, parent.height - 2 * edgeMargin) : 620
        visible: root.showResult && root.active && root.operation.terminal
        title: root.operation.phase === WalletOperationModel.Succeeded ? (root.operation.kind === WalletOperationModel.Open ? qsTr("Wallet opened with warnings") : qsTr("Wallet update complete")) : root.operation.errorTitle
        onClosed: { if (root.active && root.operation.terminal) root.presenter.dismiss(root.operation) }
        contentItem: ColumnLayout {
            spacing: Theme.space.ml
            DialogHeader {
                Layout.fillWidth: true
                title: resultPopup.title
                onCloseRequested: resultPopup.close()
            }
            CoreText { Layout.fillWidth: true; visible: text.length > 0; text: root.operation.errorDescription; textFormat: Text.PlainText; wrapMode: Text.WordWrap }
            CoreText { Layout.fillWidth: true; visible: text.length > 0; text: root.operation.error.recovery; textFormat: Text.PlainText; wrapMode: Text.WordWrap }
            CoreText { Layout.fillWidth: true; visible: text.length > 0; text: root.operation.result.recoveryPaths.join("\n"); textFormat: Text.PlainText; wrapMode: Text.WordWrap }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                reuseItems: true
                cacheBuffer: 0
                model: root.operation.errorDetails
                delegate: CoreText { required property string content; width: ListView.view.width; text: content; textFormat: Text.PlainText; wrapMode: Text.WordWrap }
            }
            WalletOperationWarnings { Layout.fillWidth: true; operation: root.operation }
            Column {
                Layout.fillWidth: true
                Repeater {
                    model: root.operation.resultWallets
                    delegate: LinkButton {
                        required property WalletQmlModel modelData
                        text: qsTr("Open %1").arg(modelData.displayName)
                        visible: modelData !== root.operation.resultWallet
                        enabled: modelData.available
                        onClicked: { if (ApplicationModels.walletController.selectWallet(modelData)) root.presenter.dismiss(root.operation) }
                    }
                }
            }
            PrimaryButton {
                objectName: root.active ? "walletMigrationOpenResultButton" : ""
                Layout.fillWidth: true
                text: root.operation.canOpenResult ? qsTr("Open wallet") : qsTr("Done")
                onClicked: {
                    if (!root.operation.canOpenResult || ApplicationModels.walletController.selectWallet(root.operation.resultWallet)) root.presenter.dismiss(root.operation)
                }
            }
        }
    }
}
