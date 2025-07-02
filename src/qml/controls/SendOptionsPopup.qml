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
    property alias multipleRecipientsEnabled: multipleRecipientsToggle.checked

    implicitWidth: 300
    implicitHeight: 100

    clip: true
    modal: true
    dim: false

    ColumnLayout {
        id: columnLayout
        anchors.centerIn: parent
        anchors.margins: 10
        spacing: 0

        EllipsisMenuToggleItem {
            id: coinControlToggle
            Layout.fillWidth: true
            text: qsTr("Enable Coin control")
        }

        Separator {
            id: separator
            Layout.fillWidth: true
        }

        EllipsisMenuToggleItem {
            id: multipleRecipientsToggle
            Layout.fillWidth: true
            text: qsTr("Multiple Recipients")
        }
    }
}
