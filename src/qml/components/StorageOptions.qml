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
        description: qsTr("Uses about %1GB. For simple wallet use.").arg(optionsModel.assumedChainstateSize + 2)
        recommended: true
        checked: !root.customStorage && optionsModel.prune
        onClicked: {
            optionsModel.prune = true
            optionsModel.pruneSizeGB = 2
        }
        Component.onCompleted: {
            optionsModel.prune = true
            optionsModel.pruneSizeGB = 2
        }
    }
    OptionButton {
        Layout.fillWidth: true
        ButtonGroup.group: group
        text: qsTr("Store all data")
        checked: !optionsModel.prune
        description: qsTr("Uses about %1GB. Support the network.").arg(
            optionsModel.assumedBlockchainSize + optionsModel.assumedChainstateSize)
        onClicked: {
            optionsModel.prune = false
        }
    }
    Loader {
        Layout.fillWidth: true
        active: root.customStorage
        visible: active
        sourceComponent: OptionButton {
            ButtonGroup.group: group
            checked: root.customStorage && optionsModel.prune
            text: qsTr("Custom")
            description: qsTr("Storing about %1GB of data.").arg(root.customStorageAmount + chainModel.assumedChainstateSize)
            onClicked: {
                optionsModel.prune = true
                optionsModel.pruneSizeGB = root.customStorageAmount
            }
        }
    }
}
