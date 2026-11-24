// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0
import "../controls"

Popup {
    id: root
    objectName: "paymentRequestModal"
    property WalletModel wallet: null
    property bool contextActive: true
    // Opens the saved request this modal shows. Another wallet or an inactive
    // context cancels an open in progress.
    PaymentRequestViewController {
        id: requestView
        objectName: "receiveRequestView"
        receive: root.wallet ? root.wallet.receive : null
        active: root.contextActive
    }
    // The opened editor stays bound through the exit transition. Hiding a
    // view never clears or writes a successor wallet's draft.
    readonly property PaymentRequestEditorModel openedEditor: requestView.editor
    readonly property bool requestReady: requestView.editor !== null
    onRequestReadyChanged: if (requestReady) open()
    readonly property alias card: card
    parent: Overlay.overlay
    width: Math.min(560, parent ? parent.width - 32 : 560)
    height: Math.min(card.implicitHeight, parent ? parent.height - 40 : card.implicitHeight)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    padding: 0
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.NoAutoClose
    background: null
    function openRequest(id, fromY) {
        if (!contextActive || !wallet || !wallet.receive) return false
        return requestView.open(id)
    }
    function cancelPending() { requestView.cancel() }
    onContextActiveChanged: if (!contextActive) close()
    onWalletChanged: if (requestView.editorReceive && (!wallet || requestView.editorReceive !== wallet.receive)) close()
    onClosed: requestView.release()
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.6); Behavior on opacity { NumberAnimation { duration: 180 } } }
    enter: Transition { enabled: Theme.motion.animated; NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180 } }
    exit: Transition { enabled: Theme.motion.animated; NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 150 } }
    AlertPopup {
        id: failedOpen
        parent: Overlay.overlay
        title: qsTr("Request unavailable")
        message: requestView.failure
        shown: requestView.failure.length > 0
        onCloseRequested: requestView.dismissFailure()
    }
    Shortcut { sequence: "Esc"; enabled: root.visible; onActivated: if (root.openedEditor) root.openedEditor.requestClose() }
    contentItem: ScrollView {
        id: scroll
        clip: true
        contentWidth: availableWidth
        contentHeight: card.implicitHeight
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ReceiveRequestEditorCard {
            id: card
            visible: root.visible
            viewActive: root.contextActive && root.opened
            width: scroll.availableWidth
            receive: requestView.editorReceive
            editor: root.openedEditor
            onCloseRequested: root.close()
            onDeleted: function(id) { if (root.openedEditor && root.openedEditor.requestId === id) root.close() }
            onRequestAgain: function(id) {
                const original = requestView.editorReceive
                if (original && root.wallet && root.wallet.receive === original && original.requestAgain(root.openedEditor)) {
                    ApplicationModels.walletController.requestOpenReceive(root.wallet)
                }
            }
        }
    }
}
