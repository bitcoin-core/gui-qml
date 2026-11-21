// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../controls"

// The actions on one payment request, shared by its activity row and its
// card. The host shows the actions it can perform there.
ContextMenu {
    id: root

    property bool showsCopyRequest: false
    property bool copyRequestEnabled: true
    // An asynchronous copy keeps the menu open until the host closes it.
    property bool copyRequestClosesMenu: true
    property string copyRequestErrorText: ""
    property bool showsQrActions: false
    property bool qrActionsEnabled: true
    property bool showsRequestAgain: false
    // Request again and delete wait while the host is busy with the request.
    property bool requestActionsEnabled: true
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
        autoClose: root.copyRequestClosesMenu
        text: qsTr("Copy payment request")
        onTriggered: root.copyRequestRequested()
    }
    CoreText {
        objectName: root.copyRequestObjectName + "Error"
        Layout.fillWidth: true
        Layout.maximumWidth: 240
        Layout.margins: Theme.space.ml
        visible: root.showsCopyRequest && text.length > 0
        text: root.copyRequestErrorText
        color: Theme.color.dangerText
        textStyle: Theme.text.caption
        wrapMode: Text.WordWrap
    }
    ContextMenuButton {
        objectName: "requestPaymentCopyQRMenuButton"
        visible: root.showsQrActions
        enabled: root.qrActionsEnabled
        text: qsTr("Copy QR code")
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
        enabled: root.requestActionsEnabled
        text: qsTr("Request again")
        onTriggered: root.requestAgainRequested()
    }
    ContextMenuDivider {
        objectName: root.deleteObjectName + "Divider"
    }
    ContextMenuButton {
        objectName: root.deleteObjectName
        enabled: root.requestActionsEnabled
        text: qsTr("Delete payment request")
        role: ContextMenuButton.Destructive
        onTriggered: root.deleteRequested()
    }
}
