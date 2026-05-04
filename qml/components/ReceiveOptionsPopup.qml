// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

OptionPopup {
    id: root

    property alias showName: nameToggle.checked
    property alias showMessage: messageToggle.checked
    property alias showNoteSelf: noteSelfToggle.checked
    property alias showAddressType: addressTypeToggle.checked

    implicitWidth: 300
    implicitHeight: columnLayout.implicitHeight + 20

    clip: true
    modal: true
    dim: false

    ColumnLayout {
        id: columnLayout
        anchors.centerIn: parent
        anchors.margins: 10
        spacing: 5

        EllipsisMenuToggleItem {
            id: nameToggle
            Layout.fillWidth: true
            text: qsTr("Name")
            checked: true
        }

        EllipsisMenuToggleItem {
            id: messageToggle
            Layout.fillWidth: true
            text: qsTr("Message")
            checked: true
        }

        EllipsisMenuToggleItem {
            id: noteSelfToggle
            Layout.fillWidth: true
            text: qsTr("Note to self")
            checked: true
        }

        EllipsisMenuToggleItem {
            id: addressTypeToggle
            Layout.fillWidth: true
            text: qsTr("Address type")
        }

        EllipsisMenuButtonItem {
            Layout.fillWidth: true
            text: qsTr("View address history")
            enabled: false
        }
    }
}
