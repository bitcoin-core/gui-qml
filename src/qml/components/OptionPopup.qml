// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

Popup {
    id: root

    background: Item {
        anchors.fill: parent
        Rectangle {
            color: Theme.color.neutral0
            border.color: Theme.color.neutral4
            radius: 5
            border.width: 1
            anchors.fill: parent
        }
    }
}
