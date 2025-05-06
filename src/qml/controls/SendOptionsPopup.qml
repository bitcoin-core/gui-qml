// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../components"
import "../controls"

OptionPopup {
    id: root

    property alias coinControlEnabled: coinControlToggle.checked

    clip: true
    modal: true
    dim: false

    EllipsisMenuToggleItem {
        id: coinControlToggle
        anchors.centerIn: parent
        text: qsTr("Enable Coin control")
    }
}