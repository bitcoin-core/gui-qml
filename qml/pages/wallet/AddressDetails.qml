// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

import "../../controls"

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
    property TextEditSessionModel editor: null
    property string noteErrorText: ""

    signal closeRequested
    signal createPaymentRequestRequested
    signal editLabelRequested(string address, string label)

    function resetNoteEditor() {
        if (!root.editor) {
            noteRow.text = root.label
        }
        root.noteErrorText = ""
    }

    onLabelChanged: {
        if (!root.editor && noteRow.field && !noteRow.field.activeFocus) {
            noteRow.text = root.label
        }
    }

    spacing: 0

    DialogHeader {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.space.xl
        title: qsTr("Address details")
        closeButtonObjectName: "addressDetailsCloseButton"
        onCloseRequested: root.editor ? root.editor.requestClose() : root.closeRequested()
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
                Layout.preferredWidth: Math.min(380, Math.max(200, root.width * 0.68))
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
            hidesAmount: true
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
            textInput: root.editor ? root.editor.text : null
            //: Placeholder shown when an address has no private note yet.
            placeholderText: qsTr("Add note…")
            enabled: root.canEditLabel
            readOnly: !root.canEditLabel || (root.editor !== null && !root.editor.available)
            errorText: root.editor ? root.editor.error.message : root.noteErrorText
            dividerColor: Theme.color.surfaceDivider
            onEditingFinished: {
                if (!root.editor && text !== root.label) {
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

    GridLayout {
        columns: 2
        visible: root.editor !== null && (root.editor.dirty || root.editor.conflict)
        Layout.fillWidth: true
        NeutralButton {
            objectName: "addressDetailsDiscardNoteButton"
            text: qsTr("Discard edits")
            enabled: root.editor !== null && root.editor.canDiscard
            onClicked: root.editor.discard()
        }
        NeutralButton {
            objectName: "addressDetailsUseCurrentNoteButton"
            text: qsTr("Use current note")
            visible: root.editor !== null && root.editor.conflict
            onClicked: root.editor.resolveConflict("text", TextEditSessionModel.UseCurrent)
        }
        NeutralButton {
            objectName: "addressDetailsKeepEditedNoteButton"
            text: qsTr("Keep my edit")
            visible: root.editor !== null && root.editor.conflict
            onClicked: root.editor.resolveConflict("text", TextEditSessionModel.KeepEdit)
        }
        PrimaryButton {
            objectName: "addressDetailsSaveNoteButton"
            text: qsTr("Save note")
            enabled: root.editor !== null && root.editor.canSave
            onClicked: root.editor.save()
        }
    }

    QtObject {
        readonly property Dialog dialog: Dialog {
            id: noteCloseDialog
            objectName: "addressNoteClosePrompt"
            // Keeps the overlay when the page leaves the window. A popup whose parent
            // goes null there loses its window while its item is still in the overlay,
            // and a hover event then crashes Qt 6.8.4 and newer.
            Binding on parent {
                when: root.Overlay.overlay !== null
                value: root.Overlay.overlay
                restoreMode: Binding.RestoreNone
            }
            modal: true
            width: Math.min(620, parent ? parent.width - 40 : 620)
            x: parent ? (parent.width - width) / 2 : 0
            y: parent ? (parent.height - height) / 2 : 0
            closePolicy: Popup.NoAutoClose
            visible: root.visible && root.editor !== null && root.editor.available && root.editor.closePrompt !== null
            title: qsTr("Save note edits?")
            contentItem: GridLayout {
                columns: noteCloseDialog.width < 480 ? 1 : 3
                NeutralButton {
                    id: keepEditingButton
                    objectName: "addressNoteKeepEditingButton"
                    Layout.fillWidth: true
                    enabled: root.editor !== null && !root.editor.busy
                    property TextEditClosePrompt pressedPrompt: null
                    onPressed: keepEditingButton.pressedPrompt = root.editor ? root.editor.closePrompt : null
                    text: qsTr("Keep editing")
                    onClicked: if (keepEditingButton.pressedPrompt) keepEditingButton.pressedPrompt.answer(TextEditClosePrompt.KeepEditing)
                }
                NeutralButton {
                    id: discardAndCloseButton
                    objectName: "addressNoteDiscardAndCloseButton"
                    Layout.fillWidth: true
                    enabled: root.editor !== null && !root.editor.busy
                    property TextEditClosePrompt pressedPrompt: null
                    onPressed: discardAndCloseButton.pressedPrompt = root.editor ? root.editor.closePrompt : null
                    text: qsTr("Discard")
                    onClicked: if (discardAndCloseButton.pressedPrompt) discardAndCloseButton.pressedPrompt.answer(TextEditClosePrompt.Discard)
                }
                PrimaryButton {
                    id: saveAndCloseButton
                    objectName: "addressNoteSaveAndCloseButton"
                    Layout.fillWidth: true
                    property TextEditClosePrompt pressedPrompt: null
                    onPressed: saveAndCloseButton.pressedPrompt = root.editor ? root.editor.closePrompt : null
                    text: qsTr("Save")
                    enabled: root.editor !== null && root.editor.valid && !root.editor.conflict && !root.editor.busy
                    onClicked: if (saveAndCloseButton.pressedPrompt) saveAndCloseButton.pressedPrompt.answer(TextEditClosePrompt.Save)
                }
            }
        }
    }
    Keys.onEscapePressed: function(event) {
        if (root.editor) root.editor.requestClose()
        else root.closeRequested()
        event.accepted = true
    }

    PrimaryButton {
        objectName: "addressDetailsCreatePaymentRequestButton"
        Layout.fillWidth: true
        Layout.topMargin: 24
        text: root.hasPaymentRequest
            //: Opens the payment request already saved for this address
            ? qsTr("View payment request")
            //: Starts a new payment request for this address
            : qsTr("Create payment request")
        visible: root.category === "single-use" && !root.used
        enabled: !root.editor || (!root.editor.dirty && !root.editor.busy)
        onClicked: root.createPaymentRequestRequested()
    }
}
