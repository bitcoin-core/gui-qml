// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"
import "../settings"

OnboardingView {
    id: root
    objectName: "onboardingStorageAmount"
    signal back()
    signal next()

    required property var settingsModel
    property int assumedBlockchainSize: 0
    property int assumedChainstateSize: 0
    readonly property bool customStorage: root.settingsModel.coreSettings.showCustomStorage
    readonly property int customStorageAmount: root.settingsModel.coreSettings.customStorageSizeGB
    readonly property bool storageCheckPending: root.settingsModel.storageCheckPending || false
    readonly property int storageAvailableGB: root.settingsModel.storageAvailableGB || 0
    readonly property string storageAvailableText: root.settingsModel.storageAvailableText || ""
    readonly property string storageWarningText: root.settingsModel.storageWarningText || ""
    readonly property string storageErrorText: root.settingsModel.storageErrorText || ""
    readonly property bool hasStorageResult: root.storageAvailableText.length > 0 && !root.storageCheckPending

    surfaceLevel: 0
    backButtonInFooter: true
    backButtonObjectName: "onboardingWizardBackButton"
    autoNavigateBack: false
    maximumContentWidth: 640
    heading: qsTr("Storage amount")
    subheading: root.hasStorageResult
        ? qsTr("Data retrieved from the Bitcoin network is stored on your device. You have %1GB of storage available.").arg(root.storageAvailableGB)
        : qsTr("Data retrieved from the Bitcoin network is stored on your device.")
    primaryButtonText: qsTr("Next")
    primaryButtonObjectName: "onboardingStorageAmountButton"
    primaryButtonEnabled: !root.storageCheckPending && root.storageErrorText.length === 0
        && root.settingsModel.storageEnoughForSelected && root.settingsModel.pruneTargetError.length === 0
    onBackClicked: root.back()
    onPrimaryClicked: root.next()

    childView: ColumnLayout {
        spacing: 16

        CoreText {
            Layout.fillWidth: true
            visible: text.length > 0
            text: root.storageErrorText.length > 0 ? root.storageErrorText : root.storageWarningText
            color: root.storageErrorText.length > 0 ? Theme.color.dangerText : Theme.color.neutral7
            textStyle: Theme.text.caption
            wrap: true
            horizontalAlignment: Text.AlignHCenter
        }

        StorageOptions {
            Layout.fillWidth: true
            settingsModel: root.settingsModel
            assumedBlockchainSize: root.assumedBlockchainSize
            assumedChainstateSize: root.assumedChainstateSize
        }

        LinkButton {
            objectName: "onboardingStorageSettingsButton"
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Storage settings")
            onClicked: storagePopup.open()
        }
    }

    OnboardingSettingsPopup {
        id: storagePopup
        objectName: "onboardingStorageSettingsPopup"
        closeButtonObjectName: "onboardingStorageSettingsCloseButton"
        initialPage: StorageSettingsPage {
            settingsModel: root.settingsModel
            onboarding: true
        }
    }
}
