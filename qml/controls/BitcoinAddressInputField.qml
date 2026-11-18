// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

ColumnLayout {
    id: root

    property BitcoinAddress address: null
    property string errorText: ""
    property bool showErrorMessage: true
    property string labelText: qsTr("Send to")
    property bool embedded: false
    property int surfaceLevel: 0
    property color surfaceColor: Theme.color.surface1
    property bool showLabel: true
    property var textStyle: Theme.text.monoBody
    property alias inputObjectName: addressInput.objectName
    property bool interceptPaste: false
    property alias inputText: addressInput.text
    property alias placeholderText: addressInput.placeholderText
    property bool readOnly: false
    readonly property alias field: addressInput
    readonly property alias contextMenu: editMenu.menu

    signal textChanged()
    signal editingFinished()
    // A pasted payment URI. With interceptPaste it is imported, not inserted.
    signal paymentUriPasted(string text)

    function paste(primary) {
        const text = primary ? Clipboard.selectionText() : Clipboard.text()
        if (!text.length || !root.enabled || root.readOnly || !root.address) return
        if (root.interceptPaste && BitcoinUri.isPaymentUri(text)) {
            root.paymentUriPasted(text)
            return
        }
        const cursor = root.address.pasteAddress(text, Math.min(addressInput.selectionStart, addressInput.selectionEnd),
            Math.max(addressInput.selectionStart, addressInput.selectionEnd))
        if (cursor >= 0) { addressInput.syncFromAddress(); addressInput.cursorPosition = cursor }
    }

    TextEditMenu {
        id: editMenu
        textEdit: addressInput
        pasteEnabled: root.enabled && !root.readOnly && !!root.address
        menuObjectName: root.inputObjectName + "ContextMenu"
        onPasteRequested: (primary) => root.paste(primary)
    }

    function syncFromAddress() {
        addressInput.syncFromAddress()
    }

    onAddressChanged: syncFromAddress()

    Layout.fillWidth: true
    spacing: Theme.space.xs

    Item {
        id: inputRow
        Layout.fillWidth: true
        implicitHeight: root.embedded ? Math.max(52, addressInput.height) : Math.max(56, addressInput.height + 16)

        CoreText {
            id: label
            visible: root.showLabel
            width: visible ? 128 : 0
            anchors.left: parent.left
            anchors.verticalCenter: addressInput.verticalCenter
            horizontalAlignment: Text.AlignLeft
            text: root.labelText
            textStyle: Theme.text.body
        }

        TextArea {
            id: addressInput
            property bool syncingFromAddress: false
            readOnly: root.readOnly

            function syncFromAddress() {
                const formattedAddress = root.address ? root.address.formattedAddress : ""
                if (text === formattedAddress) return
                syncingFromAddress = true
                text = formattedAddress
                syncingFromAddress = false
            }

            // A native paste (such as a platform menu) of a payment URI.
            function handlePaymentUriInput() {
                if (!root.interceptPaste) return false
                const uri = BitcoinUri.pastedPaymentUri(root.address ? root.address.formattedAddress : "", text, Clipboard.text())
                if (!uri.length) return false
                syncFromAddress()
                root.paymentUriPasted(uri)
                return true
            }
            anchors.left: label.right
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            enabled: root.enabled
            placeholderText: qsTr("Enter address…")
            text: ""
            Accessible.name: root.labelText
            Accessible.description: root.errorText
            Component.onCompleted: syncFromAddress()
            wrapMode: Text.WrapAnywhere
            verticalAlignment: TextEdit.AlignVCenter
            leftPadding: root.embedded ? 16 : 0
            topPadding: root.embedded ? 14 : 0
            rightPadding: root.embedded ? 16 : 0
            bottomPadding: root.embedded ? 14 : 0
            height: root.embedded ? Math.max(52, contentHeight + topPadding + bottomPadding) : Math.max(contentHeight, 32)
            font: root.textStyle.font
            color: Theme.color.neutral9
            placeholderTextColor: enabled ? Theme.color.placeholder : Theme.color.disabledContent
            background: Rectangle {
                color: root.surfaceLevel > 0 ? root.surfaceColor
                    : root.embedded ? Theme.color.surface2 : "transparent"
                radius: root.embedded ? 10 : 0

                FieldBorderRings {
                    anchors.fill: parent
                    visible: root.embedded && addressInput.activeFocus
                    cornerRadius: parent.radius
                }
            }
            selectByMouse: true

            Keys.onPressed: (event) => {
                if (root.interceptPaste && event.matches(StandardKey.Paste)) {
                    root.paste(false)
                    event.accepted = true
                }
            }
            Keys.onTabPressed: (event) => {
                const next = addressInput.nextItemInFocusChain(true)
                if (next && next !== addressInput) next.forceActiveFocus(Qt.TabFocusReason)
            }
            Keys.onBacktabPressed: (event) => {
                const previous = addressInput.nextItemInFocusChain(false)
                if (previous && previous !== addressInput) previous.forceActiveFocus(Qt.BacktabFocusReason)
            }

            onTextChanged: {
                if (syncingFromAddress) {
                    root.textChanged()
                    return
                }
                if (handlePaymentUriInput()) return
                if (root.address) {
                    const cursor = root.address.setAddress(text, cursorPosition)
                    if (cursor >= 0) cursorPosition = cursor
                }
                root.textChanged()
            }

            onEditingFinished: {
                if (root.address) {
                    root.address.setAddress(text, cursorPosition)
                    root.editingFinished()
                }
            }

            onActiveFocusChanged: {
                if (!activeFocus && root.address) {
                    root.address.setAddress(text)
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton | Qt.MiddleButton
                onPressed: (mouse) => editMenu.pointerPressed(mouse)
            }

            Connections {
                target: root.address ? root.address : null
                function onFormattedAddressChanged() {
                    addressInput.syncFromAddress()
                    if (root.address) addressInput.cursorPosition = root.address.normalizedCursor
                }
            }
        }
    }

    RowLayout {
        id: addressIssue
        Layout.fillWidth: true
        visible: root.showErrorMessage && root.errorText.length > 0
        spacing: Theme.space.m
        Layout.topMargin: Theme.space.xs

        Icon {
            source: "image://images/alert-filled"
            size: 22
            color: Theme.color.danger
        }

        CoreText {
            text: root.errorText
            textStyle: Theme.text.description
            color: Theme.color.dangerText
            horizontalAlignment: Text.AlignLeft
            Layout.fillWidth: true
        }
    }
}
