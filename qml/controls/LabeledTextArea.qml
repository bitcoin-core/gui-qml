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
    property alias readOnly: input.readOnly
    readonly property alias inputActiveFocus: input.activeFocus
    property var textStyle: Theme.text.description
    property bool interceptPaste: false
    property string paymentUriRestoreText: ""
    fieldFocused: input.activeFocus

    signal textEdited()
    signal editingFinished()
    signal pasteRequested()

    function paymentUri(text) {
        const trimmed = String(text).trim()
        return interceptPaste && trimmed.toLowerCase().startsWith("bitcoin:") ? trimmed : ""
    }

    function pastedPaymentUri(previousText, currentText) {
        const clipboardText = Clipboard.text()
        const uri = paymentUri(clipboardText)
        if (!uri.length || previousText === currentText) return ""
        const offset = String(currentText).indexOf(clipboardText)
        if (offset < 0) return ""
        const prefix = String(currentText).slice(0, offset)
        const suffix = String(currentText).slice(offset + clipboardText.length)
        return String(previousText).startsWith(prefix)
            && String(previousText).endsWith(suffix) ? uri : ""
    }

    function paste() { input.paste() }

    onPaymentUriRestoreTextChanged: if (interceptPaste) input.syncFromModel()

    TextArea {
        id: input
        property bool syncingFromModel: false

        function syncFromModel() {
            if (text === root.paymentUriRestoreText) return
            syncingFromModel = true
            text = root.paymentUriRestoreText
            syncingFromModel = false
        }

        function handlePaymentUriInput() {
            const uri = root.pastedPaymentUri(root.paymentUriRestoreText, text)
            if (!uri.length) return false
            syncFromModel()
            root.pasteRequested()
            syncFromModel()
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
        placeholderTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
        background: Item {}
        selectByMouse: true
        Accessible.name: root.label
        Accessible.description: root.errorText.length ? root.errorText : root.supportingText

        Keys.onPressed: (event) => {
            if (root.interceptPaste && event.matches(StandardKey.Paste)) {
                root.pasteRequested()
                event.accepted = true
            }
        }

        Component.onCompleted: if (root.interceptPaste) syncFromModel()
        onTextChanged: if (!syncingFromModel) handlePaymentUriInput()
        onTextEdited: if (!handlePaymentUriInput()) root.textEdited()
        onEditingFinished: root.editingFinished()
    }
}
