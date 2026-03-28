// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../components"
import "../controls"

OptionPopup {
    id: root
    objectName: "sendOptionsPopup"

    property alias coinControlEnabled: coinControlToggle.checked
    property alias multipleRecipientsEnabled: multipleRecipientsToggle.checked

    signal openPaymentRequest()

    implicitWidth: 300
    implicitHeight: columnLayout.implicitHeight + 20

    clip: true
    modal: true
    dim: false

    ColumnLayout {
        id: columnLayout
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 5
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 0

        EllipsisMenuButtonItem {
            objectName: "sendOptionsOpenPaymentRequestButton"
            Layout.fillWidth: true
            text: qsTr("Open payment request")
            onClicked: {
                root.close()
                root.openPaymentRequest()
            }
        }

        Separator {
            Layout.fillWidth: true
        }

        EllipsisMenuToggleItem {
            id: coinControlToggle
            objectName: "sendOptionsCoinControlToggle"
            Layout.fillWidth: true
            text: qsTr("Enable Coin control")
        }

        EllipsisMenuToggleItem {
            id: multipleRecipientsToggle
            objectName: "sendOptionsMultipleRecipientsToggle"
            Layout.fillWidth: true
            text: qsTr("Multiple Recipients")
        }
    }
}
