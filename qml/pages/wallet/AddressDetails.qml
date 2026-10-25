// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../controls"
import "../../components"

ColumnLayout {
    id: root
    objectName: "addressDetails"

    property string address
    property string label
    property string amount
    property bool hasAmount
    property string category
    property string scriptType
    property bool used
    property bool hasPaymentRequest: false
    property bool canEditLabel: category !== "change"
    property string noteErrorText: ""

    signal closeRequested
    signal createPaymentRequestRequested
    signal editLabelRequested(string address, string label)

    function resetNoteEditor() {
        noteRow.text = root.label
        if (noteRow.field) noteRow.field.text = root.label
        root.noteErrorText = ""
    }

    onLabelChanged: {
        if (noteRow.field && !noteRow.field.activeFocus) {
            noteRow.text = root.label
            noteRow.field.text = root.label
        }
    }

    spacing: 0

    DialogHeader {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.space.xl
        title: qsTr("Address details")
        closeButtonObjectName: "addressDetailsCloseButton"
        onCloseRequested: root.closeRequested()
    }

    FormSection {
        objectName: "addressDetailsSection"
        Layout.fillWidth: true
        backgroundColor: Theme.color.surface2

        ValueRow {
            objectName: "addressDetailsAddressRow"
            Layout.fillWidth: true
            title: qsTr("Address")
            minimumRowHeight: 76
            dividerColor: Theme.color.surfaceDivider

            trailingItem: AddressLabel {
                objectName: "addressDetailsAddressLabel"
                width: Math.min(380, Math.max(200, root.width * 0.68))
                address: root.address
                surfaceLevel: 2
                textStyle: Theme.text.monoCaption
                textAlignment: Text.AlignRight
                leftPadding: 0
                rightPadding: 0
                topPadding: 2
                bottomPadding: 2
            }
        }

        ValueRow {
            objectName: "addressDetailsAmountRow"
            amountValue: true
            Layout.fillWidth: true
            title: qsTr("Amount")
            value: root.amount
            valueColor: root.hasAmount ? Theme.color.txIncoming : Theme.color.neutral6
            dividerColor: Theme.color.surfaceDivider
        }

        TextFieldRow {
            id: noteRow
            objectName: "addressDetailsNoteRow"
            visible: root.canEditLabel
            Layout.fillWidth: true
            //: Private address note, also used as the payment request's note to self.
            title: qsTr("Note to self")
            fieldObjectName: "addressDetailsNoteField"
            fieldWidth: Math.min(320, Math.max(180, root.width * 0.55))
            text: root.label
            //: Placeholder shown when an address has no private note yet.
            placeholderText: qsTr("Add note…")
            enabled: root.canEditLabel
            readOnly: !root.canEditLabel
            errorText: root.noteErrorText
            dividerColor: Theme.color.surfaceDivider
            onEditingFinished: {
                if (text !== root.label) {
                    root.editLabelRequested(root.address, text)
                }
            }
        }

        ValueRow {
            objectName: "addressDetailsTypeRow"
            Layout.fillWidth: true
            title: qsTr("Type")
            value: root.category === "change" ? qsTr("Change") : qsTr("Receive")
            dividerColor: Theme.color.surfaceDivider
        }

        ValueRow {
            objectName: "addressDetailsAddressTypeRow"
            Layout.fillWidth: true
            title: qsTr("Address type")
            value: root.scriptType === "" ? qsTr("Unknown") : root.scriptType
            showDivider: false
        }
    }

    ContinueButton {
        objectName: "addressDetailsCreatePaymentRequestButton"
        Layout.fillWidth: true
        Layout.topMargin: 24
        text: root.hasPaymentRequest
            //: Opens the payment request already saved for this address
            ? qsTr("View payment request")
            //: Starts a new payment request for this address
            : qsTr("Create payment request")
        visible: root.category === "single-use" && !root.used
        onClicked: root.createPaymentRequestRequested()
    }
}
