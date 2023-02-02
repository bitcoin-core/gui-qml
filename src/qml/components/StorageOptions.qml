// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
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
        description: qsTr("Uses about 2GB. For simple wallet use.")
        recommended: true
        checked: true
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
        description: qsTr("Uses about 550GB. Support the network.")
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
            checked: root.customStorage
            text: qsTr("Custom")
            description: qsTr("Storing recent blocks up to %1GB").arg(root.customStorageAmount)
            onClicked: {
                optionsModel.prune = true
                optionsModel.pruneSizeGB = root.customStorageAmount
            }
        }
    }
}
