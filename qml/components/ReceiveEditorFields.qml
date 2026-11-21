// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQml 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

ColumnLayout {
    id: root
    required property PaymentRequestEditorModel editor
    spacing: 16
    Binding {
        target: root.editor ? root.editor.amount : null
        property: "unit"
        value: ApplicationModels.optionsModel.displayUnit
        when: root.editor !== null
    }
    CoreText { Layout.fillWidth: true; text: root.editor ? root.editor.address : ""; wrapMode: Text.WrapAnywhere }
    CoreText {
        Layout.fillWidth: true
        visible: root.editor !== null && root.editor.historicallyUsed
        text: qsTr("This address has been used. Public payment details are read only; the private note can still be edited.")
    }
    BitcoinAmountInputField {
        amount: root.editor ? root.editor.amount : null
        readOnly: root.editor === null || !root.editor.publicFieldsEditable || root.editor.busy
        inputObjectName: "receiveAmountInput"
        unitToggleObjectName: "receiveAmountUnitButton"
    }
    TextFieldRow {
        Layout.fillWidth: true
        title: qsTr("Recipient name")
        fieldObjectName: "receivePublicLabelInput"
        textInput: root.editor ? root.editor.publicLabel : null
        maximumLength: 16384
        readOnly: root.editor === null || !root.editor.publicFieldsEditable || root.editor.busy
    }
    TextFieldRow {
        Layout.fillWidth: true
        title: qsTr("Public message")
        fieldObjectName: "receiveMessageInput"
        textInput: root.editor ? root.editor.message : null
        maximumLength: 16384
        readOnly: root.editor === null || !root.editor.publicFieldsEditable || root.editor.busy
    }
    TextFieldRow {
        Layout.fillWidth: true
        title: qsTr("Note to self")
        fieldObjectName: "receivePrivateNoteInput"
        textInput: root.editor ? root.editor.privateNote : null
        maximumLength: 16384
        readOnly: root.editor === null || !root.editor.available || root.editor.busy
    }
    Repeater {
        model: root.editor ? root.editor.conflicts : []
        delegate: RowLayout {
            id: conflictRow
            required property var modelData
            CoreText { Layout.fillWidth: true; text: conflictRow.modelData.fieldId + ": " + conflictRow.modelData.committedText }
            NeutralButton {
                text: qsTr("Use current")
                onClicked: root.editor.resolveConflict(conflictRow.modelData.fieldId, TextEditSessionModel.UseCurrent)
            }
            NeutralButton {
                text: qsTr("Keep edit")
                enabled: root.editor !== null && (root.editor.publicFieldsEditable || conflictRow.modelData.fieldId === "note")
                onClicked: root.editor.resolveConflict(conflictRow.modelData.fieldId, TextEditSessionModel.KeepEdit)
            }
        }
    }
    CoreText {
        objectName: "receiveEditorError"
        Layout.fillWidth: true
        visible: text.length > 0
        text: root.editor ? root.editor.error.message : ""
        color: Theme.color.dangerText
    }
    RowLayout {
        PrimaryButton {
            objectName: "receiveSaveRequestButton"
            text: root.editor && root.editor.mutationKind === PaymentRequestEditorModel.Create ? qsTr("Create request") : qsTr("Update request")
            enabled: root.editor !== null && root.editor.canSave
            busy: root.editor !== null && root.editor.busy
            onClicked: root.editor.save()
        }
        NeutralButton {
            objectName: "receiveDiscardEditsButton"
            text: qsTr("Discard edits")
            enabled: root.editor !== null && root.editor.canDiscard
            onClicked: root.editor.discard()
        }
    }
    ReceiveEditorCloseQuestion { Layout.fillWidth: true; editor: root.editor }
}
