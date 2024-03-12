// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../controls"

ColumnLayout {
    id: root
    property bool customStorage: false
    property int customStorageAmount
    ButtonGroup {
        id: group
    }
    spacing: 15
    OptionButton {
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Reduce storage")
        description: qsTr("Uses about %1GB. For simple wallet use.").arg(onboardingModel.assumedChainstateSize + 2)
        recommended: true
        checked: !root.customStorage && onboardingModel.prune
        onClicked: {
            onboardingModel.prune = true
            onboardingModel.pruneSizeGB = 2
        }
        Component.onCompleted: {
            onboardingModel.prune = true
            onboardingModel.pruneSizeGB = 2
        }
    }
    OptionButton {
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Store all data")
        checked: !onboardingModel.prune
        description: qsTr("Uses about %1GB. Support the network.").arg(
            onboardingModel.assumedBlockchainSize + onboardingModel.assumedChainstateSize)
        onClicked: {
            onboardingModel.prune = false
        }
    }
    Loader {
        Layout.fillWidth: true
        active: root.customStorage
        visible: active
        sourceComponent: OptionButton {
            ButtonGroup.group: group
            checked: root.customStorage && onboardingModel.prune
            text: qsTr("Custom")
            description: qsTr("Storing about %1GB of data.").arg(root.customStorageAmount + onboardingModel.assumedChainstateSize)
            onClicked: {
                onboardingModel.prune = true
                onboardingModel.pruneSizeGB = root.customStorageAmount
            }
        }
    }
}
