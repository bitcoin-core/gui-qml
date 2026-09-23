// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

ColumnLayout {
    id: root
    property var wallet
    property var recipient
    property bool clipboardRequest: false
    property bool canRemove: false
    property string removeButtonObjectName: "sendRemoveRecipientButton"
    property alias addressField: addressField
    property alias noteField: noteField
    property alias amountField: amountField
    signal pasteRequested(string field)
    signal edited()
    signal removeRequested()
    spacing: 18

    LabeledField {
        objectName: "sendAddressGroup"
        Layout.fillWidth: true
        label: qsTr("Bitcoin address")
        labelSpacing: 6
        messageSpacing: 8
        fieldSurface: true
        fieldFocused: addressField.field.activeFocus
        errorText: root.recipient ? root.recipient.addressError : ""
        actionText: root.clipboardRequest ? qsTr("Use payment request") : ""
        actionIconSource: "qrc:/icons/paste-payment-request.svg"
        actionObjectName: "clipboardUriPasteButton"
        onActionRequested: root.pasteRequested("address")
        BitcoinAddressInputField {
            id: addressField
            objectName: "sendAddressField"
            inputObjectName: "sendAddressInput"
            Layout.fillWidth: true
            embedded: false
            showLabel: false
            showErrorMessage: false
            interceptPaste: true
            textStyle: Theme.text.monoDescription
            address: root.recipient ? root.recipient.address : null
            errorText: root.recipient ? root.recipient.addressError : ""
            placeholderText: qsTr("Enter a Bitcoin address or payment request")
            onPasteRequested: root.pasteRequested("address")
            onTextChanged: root.edited()
        }
    }

    ColumnLayout {
        visible: root.recipient && root.recipient.hasPaymentRequest
        Layout.fillWidth: true
        spacing: 8
        CoreText {
            Layout.fillWidth: true
            text: qsTr("Payment request")
            font: Theme.text.description.font
            color: Theme.color.neutral7
            horizontalAlignment: Text.AlignLeft
        }
        FormSection {
            Layout.fillWidth: true
            isOnSurface: true
            showGradientBorder: false
            ValueRow {
                visible: root.recipient && root.recipient.paymentRequestLabel.length > 0
                objectName: "sendPaymentRequestPayTo"
                Layout.fillWidth: true
                title: qsTr("Pay to")
                value: root.recipient ? root.recipient.paymentRequestLabel : ""
                showDivider: root.recipient && root.recipient.message.length > 0
                dividerColor: Theme.color.neutral3
            }
            ValueRow {
                visible: root.recipient && root.recipient.message.length > 0
                objectName: "sendPaymentRequestMessageText"
                Layout.fillWidth: true
                title: qsTr("For")
                value: root.recipient ? root.recipient.message : ""
                showDivider: false
            }
        }
    }

    LabeledField {
        objectName: "sendAmountGroup"
        Layout.fillWidth: true
        label: qsTr("Amount")
        labelSpacing: 6
        messageSpacing: 8
        messageObjectName: "sendAmountErrorText"
        fieldSurface: true
        fieldFocused: amountField.field.activeFocus
        errorText: root.recipient ? root.recipient.amountError : ""
        actionText: qsTr("Use maximum")
        actionObjectName: "sendUseMaximumButton"
        onActionRequested: { root.wallet.useMaximum(); amountField.syncFromAmount(true) }
        BitcoinAmountInputField {
            id: amountField
            Layout.fillWidth: true
            showLabel: false
            showErrorMessage: false
            inputObjectName: "sendAmountInput"
            unitToggleObjectName: "sendAmountUnitToggle"
            unitLabelObjectName: "sendAmountUnitLabel"
            amount: root.recipient ? root.recipient.amount : null
            errorText: root.recipient ? root.recipient.amountError : ""
            interceptPaste: true
            onInputTextChanged: root.edited()
            onEditingFinished: root.edited()
            onPasteRequested: root.pasteRequested("amount")
        }
    }

    CheckBox {
        objectName: "sendDeductFeeCheckbox"
        Layout.fillWidth: true
        text: qsTr("Deduct fee from this recipient")
        checked: root.recipient ? root.recipient.subtractFeeFromAmount : false
        onToggled: { root.recipient.subtractFeeFromAmount = checked; root.edited() }
    }

    Separator { Layout.fillWidth: true }

    LabeledTextArea {
        id: noteField
        objectName: "sendNoteField"
        Layout.fillWidth: true
        label: qsTr("Note to self")
        supportingText: qsTr("Recommended")
        labelSpacing: 6
        messageSpacing: 8
        fieldSurface: true
        inputObjectName: "sendNoteInput"
        interceptPaste: true
        placeholderText: qsTr("Add a note for yourself")
        paymentUriRestoreText: root.recipient ? root.recipient.label : ""
        onTextEdited: if (root.recipient) root.recipient.label = text
        onPasteRequested: root.pasteRequested("label")
    }

    Separator { visible: root.canRemove; Layout.fillWidth: true }

    NeutralButton {
        visible: root.canRemove
        objectName: root.removeButtonObjectName
        Layout.preferredWidth: 96
        Layout.alignment: Qt.AlignLeft
        text: qsTr("Remove")
        onClicked: root.removeRequested()
    }
}
