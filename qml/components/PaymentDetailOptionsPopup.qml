// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

OptionPopup {
    id: root

    property bool hasPaymentInfo: false
    property alias showAddressType: addressTypeToggle.checked
    property alias showPaymentRequest: paymentRequestToggle.checked

    signal copyAddress()

    implicitWidth: 300
    implicitHeight: column.implicitHeight + 2 * column.anchors.topMargin

    padding: 0
    clip: true
    modal: true
    dim: false

    ColumnLayout {
        id: column
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: 5
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 0

        EllipsisMenuButtonItem {
            Layout.fillWidth: true
            text: qsTr("Copy address")
            onClicked: {
                root.copyAddress()
                root.close()
            }
        }

        EllipsisMenuToggleItem {
            id: addressTypeToggle
            Layout.fillWidth: true
            text: qsTr("Show Address Type")
        }

        EllipsisMenuToggleItem {
            id: paymentRequestToggle
            Layout.fillWidth: true
            text: qsTr("Show Payment Request")
            visible: root.hasPaymentInfo
        }
    }
}
