// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../../controls"
import "../../components"

OnboardingView {
    id: root
    objectName: "onboardingStorageLocation"
    signal back()
    signal next()

    property int assumedChainstateSize: 0
    property var settingsModel: optionsModel
    readonly property int reducedStorageTargetGB: 2
    readonly property int freshMinimumStorageRequiredGB: root.assumedChainstateSize + root.reducedStorageTargetGB
    readonly property int minimumStorageRequiredGB: root.settingsModel.existingProfile
        ? root.settingsModel.storageMinimumRequiredGB : root.freshMinimumStorageRequiredGB

    isOnSurface: false
    backButtonInFooter: true
    backButtonObjectName: "onboardingWizardBackButton"
    autoNavigateBack: false
    maximumContentWidth: 640
    heading: qsTr("Storage location")
    subheading: qsTr("Where do you want to store the downloaded block data?\nYou need a minimum of %1GB of storage.").arg(root.minimumStorageRequiredGB)
    primaryButtonText: qsTr("Next")
    primaryButtonObjectName: "onboardingStorageLocationButton"
    primaryButtonEnabled: !loadedChildView || loadedChildView.validSelection
    onBackClicked: root.back()
    onPrimaryClicked: root.next()

    childView: StorageLocations {
        settingsModel: root.settingsModel
        minimumStorageRequiredGB: root.minimumStorageRequiredGB
    }
}
