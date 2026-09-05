// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Page {
    id: root
    signal back

    objectName: "settingsDisplayUnitPage"
    background: null
    leftPadding: 20
    rightPadding: 20
    topPadding: 30

    header: SettingsHeader {
        title: qsTr("Display unit")
        backButtonObjectName: "settingsDisplayUnitBack"
        onBack: root.back()
    }

    ColumnLayout {
        spacing: 15
        width: Math.min(parent.width, 450)
        anchors.horizontalCenter: parent.horizontalCenter

        OptionButton {
            objectName: "displayUnitBTC"
            Layout.fillWidth: true
            text: qsTr("BTC")
            description: qsTr("8 decimal places (0.00000001 BTC = 1 sat)")
            checked: optionsModel.displayUnit === 0
            onClicked: optionsModel.displayUnit = 0
        }

        OptionButton {
            objectName: "displayUnitMBTC"
            Layout.fillWidth: true
            text: qsTr("mBTC")
            description: qsTr("5 decimal places (0.00001 mBTC = 1 sat)")
            checked: optionsModel.displayUnit === 1
            onClicked: optionsModel.displayUnit = 1
        }

        OptionButton {
            objectName: "displayUnitUBTC"
            Layout.fillWidth: true
            text: qsTr("bits")
            description: qsTr("2 decimal places (0.01 bits = 1 sat)")
            checked: optionsModel.displayUnit === 2
            onClicked: optionsModel.displayUnit = 2
        }

        OptionButton {
            objectName: "displayUnitSAT"
            Layout.fillWidth: true
            text: qsTr("sat")
            description: qsTr("Satoshi, the smallest unit (1 sat = 0.00000001 BTC)")
            checked: optionsModel.displayUnit === 3
            onClicked: optionsModel.displayUnit = 3
        }
    }
}
