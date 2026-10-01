// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../../controls"
import "../../components"

OnboardingView {
    id: root
    objectName: "externalWalletCreatedPage"
    showNavigationBar: false
    usesSharedNavigation: true
    signal done()

    title: ""
    heading: qsTr("Your external wallet is ready")
    subheading: qsTr("This wallet uses the connected external signer for addresses and signing.")
    maximumContentWidth: 800
    showBackButton: false
    showCloseButton: false
    primaryButtonText: ""
    secondaryButtonText: qsTr("Go to wallet")
    secondaryButtonObjectName: "externalWalletCreatedDoneButton"
    onSecondaryClicked: root.done()

    imageView: WalletSuccessBadge {
        objectName: "externalWalletSuccessBadge"
    }
}
