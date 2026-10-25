// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

import "../controls"

// The actions on one payment request, shared by its activity row and its
// card. The host shows the actions it can perform there.
ContextMenu {
    id: root

    property bool showsCopyRequest: false
    property bool copyRequestEnabled: true
    property bool showsQrActions: false
    property bool qrActionsEnabled: true
    property bool showsRequestAgain: false
    property string copyRequestObjectName: "requestPaymentCopyMenuButton"
    property string deleteObjectName: "requestPaymentDeleteMenuButton"

    signal copyRequestRequested()
    signal copyQrRequested()
    signal saveQrRequested()
    signal requestAgainRequested()
    signal deleteRequested()

    ContextMenuButton {
        objectName: root.copyRequestObjectName
        visible: root.showsCopyRequest
        enabled: root.copyRequestEnabled
        text: qsTr("Copy payment request")
        iconSource: "image://images/copy"
        onTriggered: root.copyRequestRequested()
    }
    ContextMenuButton {
        objectName: "requestPaymentCopyQRMenuButton"
        visible: root.showsQrActions
        enabled: root.qrActionsEnabled
        text: qsTr("Copy QR code")
        iconSource: "image://images/copy"
        onTriggered: root.copyQrRequested()
    }
    ContextMenuButton {
        objectName: "requestPaymentSaveQRMenuButton"
        visible: root.showsQrActions
        enabled: root.qrActionsEnabled
        text: qsTr("Save QR code…")
        onTriggered: root.saveQrRequested()
    }
    ContextMenuDivider {
        visible: root.showsRequestAgain && (root.showsCopyRequest || root.showsQrActions)
    }
    ContextMenuButton {
        objectName: "requestPaymentAgainMenuButton"
        visible: root.showsRequestAgain
        text: qsTr("Request again")
        onTriggered: root.requestAgainRequested()
    }
    ContextMenuDivider {
        objectName: root.deleteObjectName + "Divider"
    }
    ContextMenuButton {
        objectName: root.deleteObjectName
        text: qsTr("Delete payment request")
        role: ContextMenuButton.Destructive
        onTriggered: root.deleteRequested()
    }
}
