// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

Item {
    id: root
    objectName: "addressRow"

    required property string address
    required property string ellipsesAddress
    required property string label
    required property string category
    required property string currentBalance
    required property string displayAmount
    required property bool hasAmount
    required property string scriptType
    required property bool isUsed
    required property bool canEditLabel
    required property bool canCreatePaymentRequest

    property alias menu: rowMenu
    // Resolved when the menu opens rather than bound per row: the lookup walks
    // the request model, which is work no list delegate should do while
    // scrolling, and a binding would not see requests saved in the meantime.
    property bool hasPaymentRequest: false
    // A single-use address offers the request action while it is unused, and
    // keeps it afterwards if a saved request is still there to edit.
    readonly property bool offersPaymentRequestAction:
        root.category === "single-use" && (!root.isUsed || root.hasPaymentRequest)

    signal menuAboutToOpen(string address)
    signal editLabelRequested(string address, string label)
    signal createPaymentRequestRequested(string address)
    signal detailsRequested(string address, string label, string amount, bool hasAmount, string category, string scriptType, bool used)

    height: 100

    Column {
        id: addressColumn
        anchors.left: parent.left
        anchors.right: noteButton.visible ? noteButton.left : menuButton.left
        anchors.rightMargin: noteButton.visible ? 16 : 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        CoreText {
            objectName: "addressRowAddressText"
            width: parent.width
            text: root.ellipsesAddress
            color: Theme.color.neutral9
            font: Theme.text.body.font
            horizontalAlignment: Text.AlignLeft
            elide: Text.ElideMiddle
            wrap: false
        }
        CoreText {
            objectName: "addressRowAmountText"
            width: parent.width
            text: root.displayAmount
            color: root.hasAmount ? Theme.color.green : Theme.color.neutral6
            font: Theme.text.body.font
            horizontalAlignment: Text.AlignLeft
            elide: Text.ElideRight
            wrap: false
        }
    }

    Button {
        id: noteButton
        objectName: "addressRowNoteButton"
        anchors.right: menuButton.left
        anchors.rightMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        visible: root.canEditLabel
        width: Math.min(noteLabel.implicitWidth + 28, 150)
        height: 36
        padding: 0
        text: root.label === "" ? qsTr("Add note to self") : root.label
        onClicked: root.editLabelRequested(root.address, root.label)

        contentItem: CoreText {
            id: noteLabel
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            text: noteButton.text
            font: Theme.text.description.font
            color: Theme.color.neutral6
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            wrap: false
        }

        background: Rectangle {
            color: noteButton.hovered ? Theme.color.neutral3 : Theme.color.neutral2
            radius: 18
        }
    }

    IconButton {
        id: menuButton
        objectName: "addressRowMenuButton"
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        iconSource: "image://images/ellipsis"
        iconColor: Theme.color.neutral9
        enabled: root.canCreatePaymentRequest || root.address !== ""
        onClicked: {
            const pos = menuButton.mapToItem(Overlay.overlay, menuButton.width, menuButton.height)
            rowMenu.x = Math.max(12, Math.min(pos.x - rowMenu.width, Overlay.overlay.width - rowMenu.width - 12))
            rowMenu.y = Math.max(12, Math.min(pos.y, Overlay.overlay.height - rowMenu.height - 12))
            root.menuAboutToOpen(root.address)
            rowMenu.open()
        }
    }

    ContextMenu {
        id: rowMenu
        parent: Overlay.overlay
        modal: true
        dim: false
        focus: true

        ContextMenuButton {
            objectName: "addressRowCreatePaymentRequestButton"
            text: root.hasPaymentRequest
                //: Opens the payment request already saved for this address in the editor
                ? qsTr("Edit payment request")
                //: Starts a new payment request for this address
                : qsTr("Create payment request")
            // Matches the address details button, which also keeps the action
            // for a used address that still has a saved request.
            visible: root.offersPaymentRequestAction
            enabled: root.address !== ""
            onTriggered: root.createPaymentRequestRequested(root.address)
        }
        ContextMenuButton {
            objectName: "addressRowCopyAddressButton"
            text: qsTr("Copy address")
            enabled: root.address !== ""
            onTriggered: Clipboard.setText(root.address)
        }
        ContextMenuButton {
            objectName: "addressRowDetailsButton"
            text: qsTr("Address details")
            enabled: root.address !== ""
            onTriggered: root.detailsRequested(root.address, root.label, root.displayAmount, root.hasAmount, root.category, root.scriptType, root.isUsed)
        }
    }

    Separator {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
    }
}
