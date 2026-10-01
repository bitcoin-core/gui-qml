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
        isOnSurface: true
        showGradientBorder: false
        rowSpacing: 0

        KeyValueRow {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 16
            Layout.bottomMargin: 12
            key: CoreText {
                text: qsTr("Wallet name")
                color: Theme.color.neutral7
                font: Theme.text.body.font
            }
            value: CoreText {
                objectName: "importWalletSuccessWalletName"
                text: walletController.lastImportedWalletName.length > 0
                    ? walletController.lastImportedWalletName : qsTr("Unknown")
                color: Theme.color.neutral9
                font: Theme.text.subheading.font
                wrap: true
                horizontalAlignment: Text.AlignRight
            }
        }
        Separator {
            objectName: "importWalletInfoSeparator"
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            color: Theme.color.neutral3
        }
        KeyValueRow {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.topMargin: 12
            Layout.bottomMargin: 16
            key: CoreText {
                text: qsTr("Key scheme")
                color: Theme.color.neutral7
                font: Theme.text.body.font
            }
            value: CoreText {
                objectName: "importWalletSuccessKeyScheme"
                text: walletController.lastImportedWalletKeyScheme.length > 0
                    ? walletController.lastImportedWalletKeyScheme : qsTr("Unknown")
                color: Theme.color.neutral9
                font: Theme.text.subheading.font
                wrap: true
                horizontalAlignment: Text.AlignRight
            }
        }
    }
}
