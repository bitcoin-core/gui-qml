// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../../controls"

OnboardingView {
    id: root
    objectName: "onboardingStrengthen"
    signal back()
    signal next()

    isOnSurface: false
    backButtonInFooter: true
    backButtonObjectName: "onboardingWizardBackButton"
    autoNavigateBack: false
    maximumContentWidth: 640
    heading: qsTr("Strengthen bitcoin")
    subheading: qsTr("Bitcoin Core runs a full Bitcoin node which verifies " +
        "the rules of the network are being followed.\n\nUsers running nodes " +
        "is what makes bitcoin so resilient and trustworthy.")
    imageSource: Theme.image.network
    imageSize: 200
    primaryButtonText: qsTr("Next")
    primaryButtonObjectName: "onboardingStrengthenButton"
    onBackClicked: root.back()
    onPrimaryClicked: root.next()
}
