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
        Layout.fillWidth: true
        text: qsTr("Reduce storage")
        description: qsTr("Uses about 75GB.")
        recommended: true
        checked: true
        detail: ProgressIndicator {
            implicitWidth: 75
            progress: 0.25
        }
    }
    OptionButton {
        ButtonGroup.group: group
        Layout.fillWidth: true
        text: qsTr("Default")
        description: qsTr("Uses about 423GB.")
        detail: ProgressIndicator {
            implicitWidth: 75
            progress: 0.8
        }
    }
}
