// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

LabeledField {
    id: root

    property alias text: input.text
    property alias placeholderText: input.placeholderText
    property alias inputObjectName: input.objectName
    property bool readOnly: false
    readonly property alias field: input
    readonly property alias inputActiveFocus: input.activeFocus
    property var textStyle: Theme.text.description
    property bool interceptPaste: false
    property string paymentUriRestoreText: ""
    fieldFocused: input.activeFocus

    signal textEdited()
    signal editingFinished()
    // A pasted payment URI. With interceptPaste it is imported, not inserted.
    signal paymentUriPasted(string text)

    function paste(primary) {
        const text = primary ? Clipboard.selectionText() : Clipboard.text()
        if (!text.length || !root.enabled || root.readOnly) return
        if (root.interceptPaste && BitcoinUri.isPaymentUri(text)) {
            root.paymentUriPasted(text)
            return
        }
        const start = Math.min(input.selectionStart, input.selectionEnd)
        input.syncingFromModel = true
        input.remove(start, Math.max(input.selectionStart, input.selectionEnd))
        input.insert(start, text)
        input.cursorPosition = start + text.length
        input.syncingFromModel = false
        root.textEdited()
    }
    onPaymentUriRestoreTextChanged: if (interceptPaste) input.syncFromModel()
    TextEditMenu {
        id: editMenu
        textEdit: input
        pasteEnabled: root.enabled && !root.readOnly
        menuObjectName: root.inputObjectName + "ContextMenu"
        onPasteRequested: (primary) => root.paste(primary)
    }

    TextArea {
        id: input
        property bool syncingFromModel: false

        function syncFromModel() {
            if (text === root.paymentUriRestoreText) return
            syncingFromModel = true
            text = root.paymentUriRestoreText
            syncingFromModel = false
        }

        // A native paste (such as a platform menu) of a payment URI.
        function handlePaymentUriInput() {
            if (!root.interceptPaste) return false
            const uri = BitcoinUri.pastedPaymentUri(root.paymentUriRestoreText, text, Clipboard.text())
            if (!uri.length) return false
            syncFromModel()
            root.paymentUriPasted(uri)
            return true
        }
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(56, contentHeight + topPadding + bottomPadding)
        topPadding: 14
        bottomPadding: 14
        leftPadding: 0
        rightPadding: 0
        wrapMode: TextEdit.WrapAnywhere
        verticalAlignment: TextEdit.AlignVCenter
        textFormat: TextEdit.PlainText
        font: root.textStyle.font
        color: Theme.color.neutral9
        placeholderTextColor: enabled ? Theme.color.placeholder : Theme.color.disabledContent
        background: Item {}
        selectByMouse: true
        readOnly: root.readOnly
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton | Qt.MiddleButton
            onPressed: (mouse) => editMenu.pointerPressed(mouse)
        }
        Accessible.name: root.label
        Accessible.description: root.errorText.length ? root.errorText : root.supportingText

        Keys.onPressed: (event) => {
            if (root.interceptPaste && event.matches(StandardKey.Paste)) {
                root.paste(false)
                event.accepted = true
            }
        }
        Keys.onTabPressed: (event) => {
            const next = input.nextItemInFocusChain(true)
            if (next && next !== input) next.forceActiveFocus(Qt.TabFocusReason)
        }
        Keys.onBacktabPressed: (event) => {
            const previous = input.nextItemInFocusChain(false)
            if (previous && previous !== input) previous.forceActiveFocus(Qt.BacktabFocusReason)
        }

        Component.onCompleted: if (root.interceptPaste) syncFromModel()
        // TextEdit.textEdited was added in Qt 6.9. Treat changes made while
        // focused as edits, excluding updates restored from the model.
        onTextChanged: {
            if (syncingFromModel || !activeFocus) return
            if (!handlePaymentUriInput()) root.textEdited()
        }
        onEditingFinished: root.editingFinished()
    }
}
