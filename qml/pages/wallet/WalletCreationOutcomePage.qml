// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

OnboardingView {
    id: root
    objectName: "walletCreationOutcomePage"
    required property WalletOperationModel operation
    showNavigationBar: false
    usesSharedNavigation: true
    navigationBackEnabled: false
    heading: operation.kind === WalletOperationModel.Create ? (operation.phase === WalletOperationModel.Uncertain ? qsTr("Check whether the wallet was created") : qsTr("Wallet creation needs attention")) : operation.errorTitle
    subheading: operation.error.message
    showBackButton: false
    showCloseButton: false
    primaryButtonText: operation.canReconcile ? qsTr("Check wallet status") : ""
    primaryButtonEnabled: operation.canReconcile
    secondaryButtonText: qsTr("Done")
    secondaryButtonObjectName: "walletCreationOutcomeDoneButton"
    signal done()
    signal openRequested(WalletQmlModel wallet)
    onPrimaryClicked: operation.reconcile()
    onSecondaryClicked: root.done()
    childView: ColumnLayout {
        TextButton {
            objectName: "walletOperationOpenOriginalResultButton"
            Layout.fillWidth: true
            visible: root.operation.canOpenResult
            text: qsTr("Open the wallet")
            onClicked: root.openRequested(root.operation.resultWallet)
        }
        // Keep repeated wallet buttons in a Column positioner, not ColumnLayout:
        // a Repeater inside a Quick Layout crashes on Qt 6.4 (QTBUG-111792).
        Column {
            id: resultWalletsColumn
            Layout.fillWidth: true
            visible: resultWalletsRepeater.count > 0
            spacing: 8
            Repeater {
                id: resultWalletsRepeater
                model: root.operation.resultWallets
                delegate: TextButton {
                    required property WalletQmlModel modelData
                    width: resultWalletsColumn.width
                    text: qsTr("Open %1").arg(modelData.displayName)
                    visible: modelData !== root.operation.resultWallet
                    enabled: modelData.available
                    onClicked: root.openRequested(modelData)
                }
            }
        }
        ListView {
            id: details
            Layout.fillWidth: true
            Layout.preferredHeight: count > 0 ? 120 : 0
            model: root.operation.errorDetails
            clip: true
            reuseItems: true
            cacheBuffer: 0
            delegate: CoreText { required property string content; width: details.width; text: content; textFormat: Text.PlainText; wrapMode: Text.WordWrap }
        }
        CoreText {
            objectName: "walletCreationRecoveryText"
            Layout.fillWidth: true
            text: root.operation.error.recovery
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
        CoreText {
            objectName: "walletCreationRecoveryPaths"
            Layout.fillWidth: true
            text: root.operation.result.recoveryPaths.join("\n")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
        WalletOperationWarnings { Layout.fillWidth: true; operation: root.operation }
    }
}
