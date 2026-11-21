// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

ColumnLayout {
    id: root

    property BitcoinAmount amount
    property string errorText: ""
    property bool showErrorMessage: true
    readonly property string effectiveErrorText: errorText || (amount ? amount.inputError : "")
    property bool readOnly: false
    property string labelText: qsTr("Amount")
    property string accessibleName: labelText
    property alias inputObjectName: amountInput.objectName
    property alias unitToggleObjectName: unitToggle.objectName
    property alias unitLabelObjectName: unitToggle.labelObjectName
    property alias errorTextObjectName: errorTextLabel.objectName
    readonly property alias field: amountInput
    property string placeholderText: root.amount ? root.amountInputPlaceholder(root.amount.unit) : "0.00000000"
    property bool interceptPaste: false
    property bool showLabel: true

    signal inputTextChanged
    signal textEdited
    signal editingFinished(string value)
    // A pasted payment URI. With interceptPaste it is imported, not inserted.
    signal paymentUriPasted(string text)

    function syncFromAmount(force) {
        if (root.amount && (force || !amountInput.activeFocus)) root.amount.resetInput()
    }

    function paste(primary) {
        const text = primary ? Clipboard.selectionText() : Clipboard.text()
        if (!text.length || !root.enabled || root.readOnly || !root.amount) return
        if (root.interceptPaste && BitcoinUri.isPaymentUri(text)) {
            root.paymentUriPasted(text)
            return
        }
        const cursor = root.amount.paste(text, Math.min(amountInput.selectionStart, amountInput.selectionEnd),
            Math.max(amountInput.selectionStart, amountInput.selectionEnd))
        amountInput.restoreFromModel()
        if (cursor >= 0) amountInput.cursorPosition = Math.min(cursor, amountInput.length)
        root.inputTextChanged()
        root.textEdited()
    }

    TextEditMenu {
        id: editMenu
        textInput: amountInput
        pasteEnabled: root.enabled && !root.readOnly && !!root.amount
        menuObjectName: root.inputObjectName + "ContextMenu"
        onPasteRequested: (primary) => root.paste(primary)
    }

    function amountInputPlaceholder(unit) {
        if (unit === BitcoinAmount.SAT) return "0"
        if (unit === BitcoinAmount.MicroBTC) return "0.00"
        if (unit === BitcoinAmount.MilliBTC) return "0.00000"
        return "0.00000000"
    }

    function flippedDisplayUnit(unit) {
        return unit === BitcoinAmount.SAT ? BitcoinAmount.BTC : BitcoinAmount.SAT
    }

    Layout.fillWidth: true
    spacing: Theme.space.xs

    Item {
        id: inputRow
        Layout.fillWidth: true
        Layout.preferredHeight: 56

        CoreText {
            id: lbl
            visible: root.showLabel
            width: visible ? 128 : 0
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignLeft
            text: root.labelText
            textStyle: Theme.text.body
        }

        TextField {
            id: amountInput
            function restoreFromModel() {
                text = Qt.binding(function() { return root.amount ? root.amount.inputText : "" })
            }
            function finishAmountEditing() {
                if (root.amount) root.amount.format()
                root.editingFinished(text)
            }

            // A native paste of a payment URI is imported; other URI text
            // is not an amount, so the field keeps its previous text.
            function handlePaymentUriInput() {
                if (!root.interceptPaste) return false
                const uri = BitcoinUri.pastedPaymentUri(root.amount ? root.amount.inputText : "", text, Clipboard.text())
                if (!uri.length && text.toLowerCase().indexOf("bitcoin:") < 0) return false
                restoreFromModel()
                if (uri.length) root.paymentUriPasted(uri)
                return true
            }
            Accessible.name: root.accessibleName
            anchors.left: lbl.right
            anchors.right: unitToggle.left
            anchors.verticalCenter: parent.verticalCenter
            leftPadding: 0
            enabled: root.enabled
            font: Theme.text.body.font
            color: Theme.color.neutral9
            placeholderTextColor: enabled ? Theme.color.placeholder : Theme.color.disabledContent
            verticalAlignment: TextInput.AlignVCenter
            background: Item {}
            placeholderText: root.placeholderText
            selectByMouse: true

            Keys.onPressed: (event) => {
                if (root.interceptPaste && event.matches(StandardKey.Paste)) {
                    root.paste(false)
                    event.accepted = true
                }
            }

            text: root.amount ? root.amount.inputText : ""
            onTextEdited: {
                if (handlePaymentUriInput()) return
                if (root.amount) root.amount.editInput(text)
                root.inputTextChanged()
                root.textEdited()
            }
            onEditingFinished: finishAmountEditing()
            onActiveFocusChanged: if (!activeFocus) finishAmountEditing()
            // Intercept long native commits before business validation; the model
            // refuses the exact original input instead of parsing a cut prefix.
            maximumLength: root.interceptPaste ? 2147483647 : 129
            readOnly: root.readOnly
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton | Qt.MiddleButton
                onPressed: (mouse) => editMenu.pointerPressed(mouse)
            }

        }

        AmountUnitButton {
            id: unitToggle
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            opacity: root.enabled ? 1.0 : 0.5
            enabled: root.enabled && !!root.amount && root.amount.inputAcceptable
            unit: root.amount ? root.amount.unit : BitcoinAmount.BTC
            onClicked: {
                root.amount.format()
                ApplicationModels.optionsModel.displayUnit = root.flippedDisplayUnit(root.amount.unit)
            }
        }
    }

    RowLayout {
        id: errorRow
        Layout.fillWidth: true
        visible: root.showErrorMessage && root.effectiveErrorText.length > 0

        Icon {
            source: "image://images/alert-filled"
            size: 22
            color: Theme.color.danger
        }

        CoreText {
            id: errorTextLabel
            text: root.effectiveErrorText
            textStyle: Theme.text.description
            color: Theme.color.dangerText
            horizontalAlignment: Text.AlignLeft
            Layout.fillWidth: true
        }
    }
}
