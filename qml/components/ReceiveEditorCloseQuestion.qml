// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

ColumnLayout {
    id: root
    property PaymentRequestEditorModel editor: null
    visible: editor !== null && editor.closePrompt !== null
    CoreText {
        Layout.fillWidth: true
        text: root.editor && root.editor.mutationKind === PaymentRequestEditorModel.Delete
            ? qsTr("Delete this saved request?") : qsTr("Save this request before closing?")
    }
    RowLayout {
        NeutralButton {
            objectName: "receiveKeepEditingButton"
            text: qsTr("Keep editing")
            property TextEditClosePrompt pressedPrompt: null
            onPressed: pressedPrompt = root.editor ? root.editor.closePrompt : null
            onCanceled: pressedPrompt = null
            onClicked: if (pressedPrompt) pressedPrompt.answer(TextEditClosePrompt.KeepEditing)
        }
        NeutralButton {
            objectName: "receiveDiscardCloseButton"
            text: qsTr("Discard")
            property TextEditClosePrompt pressedPrompt: null
            onPressed: pressedPrompt = root.editor ? root.editor.closePrompt : null
            onCanceled: pressedPrompt = null
            onClicked: if (pressedPrompt) pressedPrompt.answer(TextEditClosePrompt.Discard)
        }
        PrimaryButton {
            objectName: "receiveSaveCloseButton"
            text: root.editor && root.editor.mutationKind === PaymentRequestEditorModel.Delete ? qsTr("Delete") : qsTr("Save")
            enabled: root.editor !== null && root.editor.canSave
            property TextEditClosePrompt pressedPrompt: null
            onPressed: pressedPrompt = root.editor ? root.editor.closePrompt : null
            onCanceled: pressedPrompt = null
            onClicked: if (pressedPrompt) pressedPrompt.answer(TextEditClosePrompt.Save)
        }
    }
}
