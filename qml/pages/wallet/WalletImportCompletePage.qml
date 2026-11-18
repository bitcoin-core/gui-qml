// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

OnboardingView {
    id: root
    objectName: "importWalletSuccessPage"
    showNavigationBar: false
    usesSharedNavigation: true
    required property WalletOperationModel operation
    signal done()

    title: ""
    heading: qsTr("Import complete")
    subheading: qsTr("Your wallet was added successfully. We recommend running a health check to make sure that everything works as expected.")
    maximumContentWidth: 800
    showBackButton: false
    showCloseButton: false
    primaryButtonText: ""
    secondaryButtonText: root.operation.canOpenResult ? qsTr("Go to wallet") : qsTr("Done")
    secondaryButtonObjectName: "importWalletSuccessOverviewButton"
    onSecondaryClicked: root.done()

    imageView: WalletSuccessBadge {
        objectName: "importWalletSuccessBadge"
    }

    childView: FormSection {
        objectName: "importWalletInfoSection"
        surfaceLevel: 1
        rowSpacing: 0

        ValueRow {
            Layout.fillWidth: true
            title: qsTr("Wallet name")
            valueObjectName: "importWalletSuccessWalletName"
            // The operation's own result, not the controller's latest import.
            value: root.operation.resultWallet ? root.operation.resultWallet.displayName
                : root.operation.targetWalletId.length > 0 ? root.operation.targetWalletId : qsTr("Default wallet")
            valueWrap: true
        }
        ValueRow {
            Layout.fillWidth: true
            title: qsTr("Key scheme")
            valueObjectName: "importWalletSuccessKeyScheme"
            value: root.operation.keyScheme.length > 0 ? root.operation.keyScheme : qsTr("Unknown")
            valueWrap: true
            showDivider: false
        }
        WalletOperationWarnings { Layout.fillWidth: true; operation: root.operation }
        CoreText { Layout.fillWidth: true; text: root.operation.result.recoveryPaths.join("\n"); textFormat: Text.PlainText; wrapMode: Text.WordWrap }
    }
}
