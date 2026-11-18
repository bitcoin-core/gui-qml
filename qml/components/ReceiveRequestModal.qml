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
    property WalletQmlModel wallet: null
    property bool contextActive: true
    property WalletQmlModel openedWallet: null
    property WalletReceiveModel pendingReceive: null
    property string pendingId: ""
    property string pendingToken: ""
    property PaymentRequestEditorModel openedEditor: null
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
        const token = wallet.receive.beginOpenRequest(id)
        if (!token.length) { failedOpen.message = wallet.receive.error.message; failedOpen.open(); return false }
        pendingReceive = wallet.receive
        pendingId = id
        pendingToken = token
        return true
    }
    function cancelPending() {
        if (pendingReceive && pendingToken.length) pendingReceive.cancelOpenRequest(pendingToken)
        pendingReceive = null
        pendingId = ""
        pendingToken = ""
    }
    onContextActiveChanged: if (!contextActive) { cancelPending(); close(); failedOpen.close() }
    Component.onDestruction: cancelPending()
    onWalletChanged: {
        cancelPending()
        failedOpen.close()
        if (openedWallet && openedWallet !== wallet) close()
    }
    // The captured editor remains bound throughout the exit transition.
    // Hiding a view never clears or writes a successor wallet's draft.
    onClosed: { openedEditor = null; openedWallet = null }
    Overlay.modal: Rectangle { color: Qt.rgba(0, 0, 0, 0.6); Behavior on opacity { NumberAnimation { duration: 180 } } }
    enter: Transition { enabled: Theme.motion.animated; NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180 } }
    exit: Transition { enabled: Theme.motion.animated; NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 150 } }
    Connections {
        target: root.pendingReceive
        function onRequestViewReady(token, editor) {
            if (!root.contextActive || token !== root.pendingToken || !root.wallet || root.wallet.receive !== root.pendingReceive || editor.requestId !== root.pendingId) return
            root.openedWallet = root.wallet
            root.openedEditor = editor
            root.pendingReceive = null
            root.pendingId = ""
            root.pendingToken = ""
            root.open()
        }
        function onRequestViewFailed(token, error) {
            if (token !== root.pendingToken) return
            root.pendingReceive = null; root.pendingId = ""; root.pendingToken = ""
            failedOpen.message = error.message; failedOpen.open()
        }
    }
    AlertPopup { id: failedOpen; parent: Overlay.overlay; title: qsTr("Request unavailable") }
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
            receive: root.openedWallet ? root.openedWallet.receive : null
            editor: root.openedEditor
            onCloseRequested: root.close()
            onDeleted: function(id) { if (root.openedEditor && root.openedEditor.requestId === id) root.close() }
            onRequestAgain: function(id) {
                const original = root.openedWallet
                if (original && original.receive && original.receive.requestAgain(root.openedEditor)) {
                    ApplicationModels.walletController.requestOpenReceive(original)
                }
            }
        }
    }
}
