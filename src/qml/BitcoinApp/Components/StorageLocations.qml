// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import BitcoinApp.Controls

ColumnLayout {
    ButtonGroup {
        id: group
    }
    spacing: 15
    OptionButton {
        ButtonGroup.group: group
        text: qsTr("Default directory")
        description: qsTr("The downloaded block data will be saved to the default data directory for your OS.")
        recommended: true
        checked: true
    }
    OptionButton {
        ButtonGroup.group: group
        text: qsTr("Custom directory")
        description: qsTr("The downloaded block data will be saved to the chosen directory.")
    }
}
