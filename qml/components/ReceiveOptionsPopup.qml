// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../controls"

ContextMenu {
    id: root
    objectName: "receiveOptionsPopup"

    property alias showName: nameToggle.checked
    property alias showMessage: messageToggle.checked
    property alias showNoteSelf: noteSelfToggle.checked
    property alias showAddressType: addressTypeToggle.checked
    property bool showRequestActions: false
    property bool showEditAction: false

    signal editRequest()
    signal useAsTemplate()
    signal deleteFromHistory()
    signal viewAddressHistory()

    modal: true
    dim: false

    ContextMenuToggle {
        id: nameToggle
        objectName: "receiveOptionsNameToggle"
        //: Toggle for the request label field: shared with the payer and kept as the address book label.
        text: qsTr("Label")
        checked: true
    }

    ContextMenuToggle {
        id: messageToggle
        objectName: "receiveOptionsMessageToggle"
        text: qsTr("Message")
        checked: true
    }

    ContextMenuToggle {
        id: noteSelfToggle
        objectName: "receiveOptionsNoteSelfToggle"
        text: qsTr("Note to self")
        checked: true
    }

    ContextMenuToggle {
        id: addressTypeToggle
        objectName: "receiveOptionsAddressTypeToggle"
        text: qsTr("Address type")
    }

    ContextMenuDivider {}

    ContextMenuButton {
        objectName: "receiveOptionsEditButton"
        visible: root.showEditAction
        text: qsTr("Edit payment request")
        onTriggered: root.editRequest()
    }

    ContextMenuButton {
        objectName: "receiveOptionsViewAddressHistoryButton"
        text: qsTr("View address history")
        onTriggered: root.viewAddressHistory()
    }

    ContextMenuButton {
        objectName: "receiveOptionsUseAsTemplateButton"
        visible: root.showRequestActions
        text: qsTr("Use as template")
        onTriggered: root.useAsTemplate()
    }

    ContextMenuButton {
        objectName: "receiveOptionsDeleteFromHistoryButton"
        visible: root.showRequestActions
        text: qsTr("Delete from history")
        role: ContextMenuButton.Destructive
        onTriggered: root.deleteFromHistory()
    }
}
