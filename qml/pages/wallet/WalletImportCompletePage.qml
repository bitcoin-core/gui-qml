// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

OnboardingView {
    id: root
    objectName: "importWalletSuccessPage"
    showNavigationBar: false
    usesSharedNavigation: true
    signal done()

    title: ""
    heading: qsTr("Import complete")
    subheading: qsTr("Your wallet was added successfully. We recommend running a health check to make sure that everything works as expected.")
    maximumContentWidth: 800
    showBackButton: false
    showCloseButton: false
    primaryButtonText: ""
    secondaryButtonText: qsTr("Go to wallet")
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
            value: walletController.lastImportedWalletName.length > 0
                ? walletController.lastImportedWalletName : qsTr("Unknown")
            valueWrap: true
        }
        ValueRow {
            Layout.fillWidth: true
            title: qsTr("Key scheme")
            valueObjectName: "importWalletSuccessKeyScheme"
            value: walletController.lastImportedWalletKeyScheme.length > 0
                ? walletController.lastImportedWalletKeyScheme : qsTr("Unknown")
            valueWrap: true
            showDivider: false
        }
    }
}
