// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"
import "../settings"

Page {
    id: root
    objectName: "onboardingStorageAmount"
    signal back
    signal next
    property var settingsModel: optionsModel
    property int assumedBlockchainSize: 0
    property int assumedChainstateSize: 0
    property bool customStorage: false
    property int customStorageAmount
    readonly property bool storageCheckPending: root.settingsModel.storageCheckPending || false
    readonly property int storageAvailableGB: root.settingsModel.storageAvailableGB || 0
    readonly property string storageAvailableText: root.settingsModel.storageAvailableText || ""
    readonly property string storageWarningText: root.settingsModel.storageWarningText || ""
    readonly property string storageErrorText: root.settingsModel.storageErrorText || ""
    readonly property bool hasStorageResult: root.storageAvailableText.length > 0 && !root.storageCheckPending
    background: null
    clip: true
    PageStack {
        id: stack
        anchors.fill: parent
        vertical: true
        initialItem: onboardingStorageAmount
        Component {
            id: onboardingStorageAmount
            InformationPage {
                objectName: "onboardingStorageAmountPage"
                buttonObjectName: "onboardingStorageAmountButton"
                navLeftDetail: backButton
                bannerActive: false
                bold: true
                headerText: qsTr("Storage amount")
                headerMargin: 0
                description: root.hasStorageResult
                    ? qsTr("Data retrieved from the Bitcoin network is stored on your device.\nYou have %1GB of storage available.").arg(root.storageAvailableGB)
                    : qsTr("Data retrieved from the Bitcoin network is stored on your device.")
                descriptionMargin: 10
                subtext: root.storageErrorText.length > 0
                    ? root.storageErrorText
                    : root.storageWarningText
                subtextMargin: 10
                detailActive: true
                detailItem: ColumnLayout {
                    spacing: 0
                    StorageOptions {
                        settingsModel: root.settingsModel
                        assumedBlockchainSize: root.assumedBlockchainSize
                        assumedChainstateSize: root.assumedChainstateSize
                        customStorage: root.customStorage
                        customStorageAmount: root.customStorageAmount
                        Layout.maximumWidth: 450
                        Layout.alignment: Qt.AlignCenter
                        onStorageSelectionChanged: function(customStorage, customStorageAmount) {
                            root.customStorage = customStorage
                            root.customStorageAmount = customStorageAmount
                        }
                    }
                    TextButton {
                        Layout.topMargin: 10
                        Layout.alignment: Qt.AlignCenter
                        text: qsTr("Detailed settings")
                        onClicked: stack.push(storageAmountSettings)
                    }
                }
                buttonText: qsTr("Next")
                buttonMargin: 20
                buttonEnabled: !root.storageCheckPending && root.storageErrorText.length === 0 && root.settingsModel.storageEnoughForSelected
                onNext: root.next()
            }
        }

        Component {
            id: backButton
            NavButton {
                objectName: "onboardingStorageAmountBackButton"
                iconSource: "image://images/caret-left"
                text: qsTr("Back")
                onClicked: root.back()
            }
        }

        Component {
            id: storageAmountSettings
            SettingsStorage {
                id: advancedStorage
                settingsModel: root.settingsModel
                onboarding: true
                onBack: stack.pop()
                onCustomStorageChanged: {
                    root.customStorage = advancedStorage.customStorage
                }
                onCustomStorageAmountChanged: {
                    root.customStorageAmount = advancedStorage.customStorageAmount
                }
            }
        }
    }
}
