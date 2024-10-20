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
    signal back
    signal next
    property bool customStorage: false
    property int customStorageAmount
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
                navLeftDetail: NavButton {
                    iconSource: "image://images/caret-left"
                    text: qsTr("Back")
                    onClicked: root.back()
                }
                bannerActive: false
                bold: true
                headerText: qsTr("Storage")
                headerMargin: 0
                description: qsTr("Data retrieved from the Bitcoin network is stored on your device.\nYou have 500GB of storage available.")
                descriptionMargin: 10
                detailActive: true
                detailItem: ColumnLayout {
                    spacing: 0
                    StorageOptions {
                        customStorage: advancedStorage.loadedDetailItem.customStorage
                        customStorageAmount: advancedStorage.loadedDetailItem.customStorageAmount
                        Layout.maximumWidth: 450
                        Layout.alignment: Qt.AlignCenter
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
                onNext: root.next()
            }
        }
        Component {
            id: storageAmountSettings
            SettingsStorage {
                id: advancedStorage
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
