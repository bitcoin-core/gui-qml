// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
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

    RowLayout {
        Layout.fillWidth: true
        Layout.bottomMargin: 22

        Header {
            Layout.fillWidth: true
            header: qsTr("Address details")
            headerBold: true
            center: false
        }

        Icon {
            objectName: "addressDetailsCloseButton"
            source: "image://images/cross"
            color: Theme.color.neutral8
            size: 10
            enabled: true
            padding: 0
            onClicked: root.closeRequested()
            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }
        }
    }

    FormSection {
        objectName: "addressDetailsSection"
        Layout.fillWidth: true
        backgroundColor: Theme.color.neutral2

        ValueRow {
            objectName: "addressDetailsAddressRow"
            Layout.fillWidth: true
            title: qsTr("Address")
            minimumRowHeight: 76
            dividerColor: Theme.color.neutral3

            trailingItem: AddressLabel {
                objectName: "addressDetailsAddressLabel"
                width: Math.min(380, Math.max(200, root.width * 0.68))
                address: root.address
                embedded: true
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
            Layout.fillWidth: true
            title: qsTr("Amount")
            value: root.amount
            valueColor: root.hasAmount ? Theme.color.green : Theme.color.neutral6
            dividerColor: Theme.color.neutral3
        }

        TextFieldRow {
            id: noteRow
            objectName: "addressDetailsNoteRow"
            Layout.fillWidth: true
            title: qsTr("Note")
            fieldObjectName: "addressDetailsNoteField"
            fieldWidth: Math.min(320, Math.max(180, root.width * 0.55))
            text: root.label
            placeholderText: qsTr("Add a note to self")
            enabled: root.canEditLabel
            readOnly: !root.canEditLabel
            errorText: root.noteErrorText
            dividerColor: Theme.color.neutral3
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
            value: root.category === "change" ? qsTr("Change") : qsTr("Single-use (Receive)")
            dividerColor: Theme.color.neutral3
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
        text: qsTr("Create payment request")
        visible: root.category === "single-use" && !root.used
        backgroundColor: Theme.color.orange
        backgroundHoverColor: Theme.color.orangeLight1
        backgroundPressedColor: Theme.color.orangeLight2
        textColor: Theme.color.white
        onClicked: root.createPaymentRequestRequested()
    }
}
