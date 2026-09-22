// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

ColumnLayout {
    id: root

    property var amount
    property string errorText: ""
    property bool showErrorMessage: true
    property string labelText: qsTr("Amount")
    property string accessibleName: labelText
    property alias inputObjectName: amountInput.objectName
    property alias unitToggleObjectName: unitToggle.objectName
    property alias unitLabelObjectName: unitToggle.labelObjectName
    property alias errorTextObjectName: errorTextLabel.objectName
    readonly property alias field: amountInput
    property bool enabled: true
    property string placeholderText: root.amount ? root.amountInputPlaceholder(root.amount.unit) : "0.00000000"
    property bool interceptPaste: false
    property bool showLabel: true

    signal inputTextChanged
    signal textEdited
    signal editingFinished(string value)
    signal pasteRequested()

    function syncFromAmount(force) {
        amountInput.syncFromAmount(force)
    }

    function paymentUri(text) {
        const trimmedText = String(text).trim()
        return root.interceptPaste && trimmedText.toLowerCase().startsWith("bitcoin:")
            ? trimmedText
            : ""
    }

    function pastedPaymentUri(previousText, currentText) {
        const clipboardText = Clipboard.text()
        const uri = paymentUri(clipboardText)
        if (uri.length === 0 || previousText === currentText) return ""

        let offset = String(currentText).indexOf(clipboardText)
        while (offset !== -1) {
            const prefix = String(currentText).slice(0, offset)
            const suffix = String(currentText).slice(offset + clipboardText.length)
            if (String(previousText).startsWith(prefix)
                    && String(previousText).endsWith(suffix)
                    && prefix.length + suffix.length <= String(previousText).length) {
                return uri
            }
            offset = String(currentText).indexOf(clipboardText, offset + 1)
        }
        return ""
    }

    function paste() {
        amountInput.paste()
    }

    function amountInputPlaceholder(unit) {
        if (unit === BitcoinAmount.SAT) return "0"
        if (unit === BitcoinAmount.uBTC) return "0.00"
        if (unit === BitcoinAmount.mBTC) return "0.00000"
        return "0.00000000"
    }

    function amountInputPattern(unit) {
        if (unit === BitcoinAmount.SAT) return /^0*\d{0,16}$/
        if (unit === BitcoinAmount.uBTC) return /^0*\d{0,13}(\.\d{0,2})?$/
        if (unit === BitcoinAmount.mBTC) return /^0*\d{0,10}(\.\d{0,5})?$/
        return /^0*\d{0,7}(\.\d{0,8})?$/
    }

    function amountInputMaximumLength(unit) {
        if (unit === BitcoinAmount.SAT) return 16
        if (unit === BitcoinAmount.uBTC) return 17
        if (unit === BitcoinAmount.mBTC) return 17
        return 17
    }

    function flippedDisplayUnit(unit) {
        return unit === BitcoinAmount.SAT ? BitcoinAmount.BTC : BitcoinAmount.SAT
    }

    Layout.fillWidth: true
    spacing: 4

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
            font: Theme.text.body.font
            lineHeight: Theme.text.body.lineHeight
            lineHeightMode: Text.FixedHeight
        }

        TextField {
            id: amountInput
            property bool syncingFromAmount: false
            property string lastAcceptedText: ""

            function amountDisplay() {
                return root.amount ? root.amount.display : ""
            }

            // Keep user draft text while focused, then pull from BitcoinAmount
            // after format() so the field shows the clean canonical display.
            function syncFromAmount(force) {
                if (!force && activeFocus) return
                const display = amountDisplay()
                if (text === display) {
                    lastAcceptedText = display
                    return
                }
                syncingFromAmount = true
                text = display
                lastAcceptedText = display
                syncingFromAmount = false
            }

            function restoreLastAcceptedText() {
                if (text === lastAcceptedText) return
                syncingFromAmount = true
                text = lastAcceptedText
                syncingFromAmount = false
            }

            function commitAmountText() {
                if (root.amount && root.amount.display !== text) {
                    root.amount.display = text
                }
            }

            function finishAmountEditing() {
                commitAmountText()
                if (root.amount) {
                    root.amount.format()
                    root.syncFromAmount(true)
                }
                root.editingFinished(text)
            }

            function handlePaymentUriInput() {
                const uri = root.pastedPaymentUri(lastAcceptedText, text)
                if (uri.length === 0) {
                    // Paste interception disables the validator so native
                    // context-menu paste can be observed. Reject URI-like text
                    // unless it is the exact text currently on the clipboard.
                    if (!root.interceptPaste || String(text).toLowerCase().indexOf("bitcoin:") === -1) return false
                    restoreLastAcceptedText()
                    return true
                }
                const previousDisplay = amountDisplay()
                restoreLastAcceptedText()
                root.pasteRequested()
                if (amountDisplay() !== previousDisplay) root.syncFromAmount(true)
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
            placeholderTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
            background: Item {}
            placeholderText: root.placeholderText
            selectByMouse: true

            Keys.onPressed: (event) => {
                if (root.interceptPaste && event.matches(StandardKey.Paste)) {
                    root.pasteRequested()
                    event.accepted = true
                }
            }

            text: ""
            Component.onCompleted: root.syncFromAmount(true)

            onTextChanged: {
                if (handlePaymentUriInput()) return
                if (!syncingFromAmount) {
                    if (root.amount && root.amountInputPattern(root.amount.unit).test(String(text))) {
                        lastAcceptedText = text
                    }
                    root.inputTextChanged()
                }
            }

            onTextEdited: {
                if (handlePaymentUriInput()) return
                // The validator is disabled while paste interception is active
                // so an URI inserted beside the current amount reaches this
                // handler. Preserve the same numeric validation by restoring
                // the last model value before an invalid edit is committed.
                if (root.interceptPaste
                        && root.amount
                        && !root.amountInputPattern(root.amount.unit).test(String(text))) {
                    restoreLastAcceptedText()
                    return
                }
                lastAcceptedText = text
                commitAmountText()
                root.textEdited()
            }

            onEditingFinished: finishAmountEditing()

            onActiveFocusChanged: {
                if (!activeFocus) {
                    finishAmountEditing()
                }
            }

            validator: root.interceptPaste ? null : amountValidator

            RegularExpressionValidator {
                id: amountValidator
                regularExpression: root.amount
                    ? root.amountInputPattern(root.amount.unit)
                    : /^(0|[1-9]\d{0,7})(\.\d{0,8})?$/
            }
            maximumLength: root.interceptPaste
                ? 2048 + (root.amount ? root.amountInputMaximumLength(root.amount.unit) : 17)
                : (root.amount ? root.amountInputMaximumLength(root.amount.unit) : 17)

            Connections {
                target: root
                function onAmountChanged() {
                    root.syncFromAmount(true)
                }
            }

            Connections {
                target: root.amount ? root.amount : null
                function onDisplayChanged() {
                    amountInput.syncFromAmount(false)
                }
                function onUnitChanged() {
                    root.syncFromAmount(true)
                }
            }
        }

        AmountUnitButton {
            id: unitToggle
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            opacity: root.enabled ? 1.0 : 0.5
            enabled: root.enabled && !!root.amount
            unit: root.amount ? root.amount.unit : BitcoinAmount.BTC
            onClicked: {
                amountInput.commitAmountText()
                optionsModel.displayUnit = root.flippedDisplayUnit(root.amount.unit)
            }
        }
    }

    RowLayout {
        id: errorRow
        Layout.fillWidth: true
        visible: root.showErrorMessage && root.errorText.length > 0

        Icon {
            source: "image://images/alert-filled"
            size: 22
            color: Theme.color.red
        }

        CoreText {
            id: errorTextLabel
            text: root.errorText
            font: Theme.text.description.font
            lineHeight: Theme.text.description.lineHeight
            lineHeightMode: Text.FixedHeight
            color: Theme.color.red
            horizontalAlignment: Text.AlignLeft
            Layout.fillWidth: true
        }
    }
}
