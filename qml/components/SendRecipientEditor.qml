// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

ColumnLayout {
    id: root
    required property FeeSelectionModel fees
    required property bool importPending
    required property bool selectedCoins
    signal useMaximumRequested()
    property SendRecipient recipient
    property bool clipboardRequest: false
    property string clipboardText: ""
    property bool canRemove: false
    property string removeButtonObjectName: "sendRemoveRecipientButton"
    property alias addressField: addressField
    property alias noteField: noteField
    property alias amountField: amountField
    readonly property bool sendingMaximum: root.recipient
        && root.recipient.amountMode === SendRecipient.Maximum
    // A payment URI pasted into one of the fields, or the clipboard's request.
    signal paymentUriPasted(int field, string text)
    signal edited()
    signal removeRequested()
    spacing: Theme.space.l

    LabeledField {
        id: addressGroup
        objectName: "sendAddressGroup"
        Layout.fillWidth: true
        label: qsTr("Bitcoin address")
        labelSpacing: Theme.space.s
        messageSpacing: Theme.space.m
        fieldSurface: true
        fieldFocused: addressField.field.activeFocus
        errorText: root.recipient ? root.recipient.addressError : ""
        actionText: root.clipboardRequest ? qsTr("Use payment request") : ""
        actionIconSource: "qrc:/icons/paste-payment-request.svg"
        actionObjectName: "clipboardUriPasteButton"
        onActionRequested: root.paymentUriPasted(BitcoinUri.AddressField, root.clipboardText)
        BitcoinAddressInputField {
            id: addressField
            objectName: "sendAddressField"
            inputObjectName: "sendAddressInput"
            Layout.fillWidth: true
            embedded: false
            labelText: addressGroup.label
            showLabel: false
            showErrorMessage: false
            interceptPaste: true
            // An imported payment request replaces the address; typing would be lost.
            readOnly: root.importPending
            textStyle: Theme.text.monoDescription
            address: root.recipient ? root.recipient.address : null
            errorText: root.recipient ? root.recipient.addressError : ""
            placeholderText: qsTr("Enter a Bitcoin address or payment request")
            onPaymentUriPasted: (text) => root.paymentUriPasted(BitcoinUri.AddressField, text)
            onTextChanged: root.edited()
        }
    }

    FormSection {
        objectName: "sendPaymentRequestSection"
        visible: root.recipient && root.recipient.hasPaymentRequest
            && (root.recipient.paymentRequestLabel.length > 0 || root.recipient.message.length > 0)
        Layout.fillWidth: true
        // Titled like the field labels around it.
        title: qsTr("Payment request")
        titleTextStyle: Theme.text.description
        sectionSpacing: Theme.space.s
        surfaceLevel: 1
        ValueRow {
            visible: root.recipient && root.recipient.paymentRequestLabel.length > 0
            objectName: "sendPaymentRequestPayTo"
            Layout.fillWidth: true
            title: qsTr("Pay to")
            value: root.recipient ? root.recipient.paymentRequestLabel : ""
            showDivider: root.recipient && root.recipient.message.length > 0
            dividerColor: Theme.color.surfaceDivider
        }
        ValueRow {
            visible: root.recipient && root.recipient.message.length > 0
            objectName: "sendPaymentRequestMessageText"
            Layout.fillWidth: true
            title: qsTr("Message")
            value: root.recipient ? root.recipient.message : ""
            showDivider: false
        }
    }

    LabeledField {
        objectName: "sendAmountGroup"
        Layout.fillWidth: true
        label: qsTr("Amount")
        labelSpacing: Theme.space.s
        messageSpacing: Theme.space.m
        messageObjectName: "sendAmountStatusText"
        fieldSurface: true
        fieldFocused: amountField.field.activeFocus
        errorText: root.recipient ? root.recipient.inputBudgetError || root.recipient.amountError : ""
        //: Send-form action that fills the amount with the selected coins' total minus fees.
        //: Without a selection, it selects all available coins first.
        actionText: root.selectedCoins
            ? qsTr("Use maximum from selected coins") : qsTr("Use maximum")
        actionEnabled: !root.sendingMaximum
        actionObjectName: "sendUseMaximumButton"
        supportingText: !root.sendingMaximum ? ""
            : root.fees && !root.fees.estimatePending && root.fees.errorText.length > 0
            ? qsTr("Previous amount; maximum could not be calculated.") : root.selectedCoins
            ? qsTr("Sending maximum from selected coins") : qsTr("Sending maximum available")
        onActionRequested: root.useMaximumRequested()
        BitcoinAmountInputField {
            id: amountField
            Layout.fillWidth: true
            showLabel: false
            showErrorMessage: false
            inputObjectName: "sendAmountInput"
            unitToggleObjectName: "sendAmountUnitToggle"
            unitLabelObjectName: "sendAmountUnitLabel"
            amount: root.recipient ? root.recipient.amount : null
            errorText: root.recipient ? root.recipient.inputBudgetError || root.recipient.amountError : ""
            // An imported payment request replaces the amount; typing would be lost.
            readOnly: root.importPending
            interceptPaste: true
            onInputTextChanged: root.edited()
            // Text edits already update the draft and fee estimate. Formatting
            // on focus loss must not disable Review during its mouse press.
            onPaymentUriPasted: (text) => root.paymentUriPasted(BitcoinUri.AmountField, text)
        }
    }

    Separator { Layout.fillWidth: true }

    LabeledTextArea {
        id: noteField
        objectName: "sendNoteField"
        Layout.fillWidth: true
        label: qsTr("Note to self")
        supportingText: qsTr("Recommended")
        labelSpacing: Theme.space.s
        messageSpacing: Theme.space.m
        fieldSurface: true
        inputObjectName: "sendNoteInput"
        interceptPaste: true
        placeholderText: qsTr("Add a note for yourself")
        paymentUriRestoreText: root.recipient ? root.recipient.label : ""
        errorText: root.recipient ? root.recipient.inputBudgetError : ""
        onTextEdited: { if (root.recipient && !root.recipient.editPrivateNote(text)) noteField.field.syncFromModel(); root.edited() }
        onPaymentUriPasted: (text) => root.paymentUriPasted(BitcoinUri.PrivateNoteField, text)
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
