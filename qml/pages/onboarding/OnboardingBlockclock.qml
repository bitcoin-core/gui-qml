// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../../controls"

OnboardingView {
    id: root
    objectName: "onboardingBlockclock"
    signal back()
    signal next()

    isOnSurface: false
    backButtonInFooter: true
    backButtonObjectName: "onboardingWizardBackButton"
    autoNavigateBack: false
    maximumContentWidth: 640
    heading: qsTr("The block clock")
    subheading: qsTr("The Bitcoin network targets a new block every 10 minutes. " +
        "Sometimes it's faster and sometimes slower.\n\nThe block clock indicates each " +
        "block on a dial that represents the current day.")
    imageSource: Theme.image.blocktime
    imageSize: 200
    primaryButtonText: qsTr("Next")
    primaryButtonObjectName: "onboardingBlockclockButton"
    onBackClicked: root.back()
    onPrimaryClicked: root.next()
}
