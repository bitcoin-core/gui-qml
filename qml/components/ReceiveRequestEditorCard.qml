// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

Pane {
    id: root
    property WalletReceiveModel receive: null
    property PaymentRequestEditorModel editor: null
    property PaymentRequestEditorModel deleteEditor: null
    property bool viewActive: true
    readonly property RequestSharingModel sharing: receive && editor && editor.requestId.length > 0 ? receive.sharing(editor, root) : null
    signal closeRequested()
    signal requestAgain(string id)
    signal deleted(string id)
    BitcoinAmount {
        id: receivedAmount
        satoshi: root.editor ? root.editor.receipts.receivedAmountSatoshi : 0
        unit: ApplicationModels.optionsModel.displayUnit
    }
    padding: 16
    implicitWidth: 560
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    contentItem: ColumnLayout {
        spacing: 20
        CoreText {
            objectName: "savedRequestReceiptStatus"
            Layout.fillWidth: true
            visible: root.editor !== null && root.editor.requestId.length > 0
            text: !root.editor ? "" : !root.editor.receipts.fresh
                ? root.editor.receipts.phase === ReceiptReconciliationModel.Closed ? qsTr("Wallet closed")
                    : root.editor.receipts.errorText.length > 0 ? root.editor.receipts.errorText : qsTr("Checking payments…")
                : root.editor.receipts.receiptState === RequestReceiptModel.Received ? qsTr("Payment received") : qsTr("Awaiting payment")
        }
        CoreText {
            objectName: "savedRequestReceivedAmount"
            Layout.fillWidth: true
            visible: root.editor !== null && root.editor.receipts.fresh && root.editor.receipts.receiptState === RequestReceiptModel.Received
            text: qsTr("%1 received").arg(receivedAmount.displayWithUnit)
        }
        CoreText {
            objectName: "savedRequestHistoricalUseNotice"
            Layout.fillWidth: true
            visible: root.editor !== null && root.editor.receipts.fresh && root.editor.receipts.historicallyUsed && root.editor.receipts.receiptState === RequestReceiptModel.Awaiting
            text: qsTr("No payment is currently credited. This address remains used; request again to share a fresh address.")
        }
        NeutralButton {
            objectName: "savedRequestRetryPaymentsButton"
            text: qsTr("Check payments again")
            visible: root.editor !== null && root.editor.receipts.errorText.length > 0
            onClicked: root.editor.receipts.retry()
        }
        // A different editor identity destroys the old native text/undo
        // context. Amount canonical state remains in that editor's model.
        Repeater {
            model: root.editor ? [root.editor] : []
            delegate: ReceiveEditorFields {
                required property PaymentRequestEditorModel modelData
                Layout.fillWidth: true
                editor: modelData
            }
        }
        Repeater {
            model: root.sharing ? [root.sharing] : []
            delegate: SavedRequestSharing {
                required property RequestSharingModel modelData
                Layout.fillWidth: true
                sharing: modelData
                interested: root.viewActive && root.visible
            }
        }
        RowLayout {
            visible: root.editor !== null && root.editor.requestId.length > 0
            NeutralButton { objectName: "savedRequestAgainButton"; text: qsTr("Request again"); onClicked: root.requestAgain(root.editor.requestId) }
            NeutralButton {
                objectName: "savedRequestDeleteButton"
                text: qsTr("Delete request")
                enabled: root.receive !== null && root.editor !== null && !root.editor.busy && root.deleteEditor === null
                onClicked: root.deleteEditor = root.receive.deleteRequest(root.editor.requestId)
            }
        }
        ReceiveEditorCloseQuestion { Layout.fillWidth: true; editor: root.deleteEditor }
        CoreText { Layout.fillWidth: true; text: root.deleteEditor ? root.deleteEditor.error.message : ""; visible: text.length > 0; color: Theme.color.dangerText }
        NeutralButton { objectName: "savedRequestCloseButton"; text: qsTr("Close"); enabled: root.editor !== null; onClicked: root.editor.requestClose() }
    }
    Connections {
        target: root.editor
        function onClosed(editorId) { if (root.editor && editorId === root.editor.editorId) root.closeRequested() }
    }
    Connections {
        target: root.deleteEditor
        function onPersisted(result) {
            if (result.erased && root.editor && result.requestId === root.editor.requestId) root.deleted(result.requestId)
        }
        function onKeptEditing(editorId, token) { root.deleteEditor = null }
        function onClosed(editorId) { root.deleteEditor = null }
    }
}
