// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../controls"

ColumnLayout {
    ButtonGroup {
        id: group
    }
    spacing: 15
    OptionButton {
        ButtonGroup.group: group
        text: qsTr("SD Card")
        description: qsTr("The available space is large enough for full block storage. ")
        recommended: true
        checked: true
    }
    OptionButton {
        ButtonGroup.group: group
        text: qsTr("Hard drive")
        description: qsTr("Available space only allows for partial block storage.")
    }
}
