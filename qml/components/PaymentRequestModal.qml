// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"

ContextPopup {
    id: root
    interactionContext: WalletContext {
        wallet: root.wallet
    }
    objectName: "paymentRequestModal"
    property var wallet: walletController.selectedWallet
    property var openedWallet: null
    property real originY: -1
    property bool reducedMotion: false
    property real verticalOffset: 0
    property string deletedRequestId: ""
    property string repeatRequestId: ""
    WalletContext {
        id: repeatContext
        wallet: root.wallet
        owner: root.interactionContext.owner
    }
    readonly property real centeredY: parent ? Math.max(20, (parent.height - height) / 2) : 0
    readonly property alias card: card

    parent: Overlay.overlay
    width: Math.min(560, parent ? parent.width - 32 : 560)
    height: Math.min(card.implicitHeight, parent ? parent.height - 40 : card.implicitHeight)
    x: parent ? (parent.width - width) / 2 : 0
    y: centeredY + verticalOffset
    padding: 0
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: null

    function openRequest(requestId, fromY) {
        repeatContext.invalidate()
        if (!wallet || !wallet.loadPaymentRequestDetail(requestId)) return false
        originY = fromY === undefined ? -1 : fromY
        card.resetFields()
        openedWallet = wallet
        open()
        return true
    }
    onAboutToShow: if (scroll.contentItem) scroll.contentItem.contentY = 0
    onClosed: {
        const originalWallet = openedWallet
        if (originalWallet && originalWallet === wallet && deletedRequestId) {
            for (const request of [originalWallet.currentPaymentRequest, originalWallet.detailPaymentRequest]) {
                if (request && request.id === deletedRequestId) request.clear()
            }
        }
        deletedRequestId = ""
        openedWallet = null
        card.resetFields()
        if (repeatRequestId) {
            const requestId = repeatRequestId
            const requestWallet = originalWallet
            const context = repeatContext.current
            repeatRequestId = ""
            // Let the Receive page finish clearing its previous creation draft.
            Qt.callLater(function() {
                if (!repeatContext.isValid(context)) return
                repeatContext.take()
                requestWallet.usePaymentRequestAsTemplate(requestId)
                walletController.requestOpenReceive()
            })
        }
    }
    onWalletChanged: {
        if (openedWallet && wallet !== openedWallet) {
            repeatRequestId = ""
            close()
        }
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.6)
        Behavior on opacity { NumberAnimation { duration: 180 } }
    }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180 }
            NumberAnimation {
                property: "verticalOffset"
                from: root.reducedMotion ? 0 : root.originY >= 0 ? root.originY - root.centeredY : 16
                to: 0
                duration: root.reducedMotion ? 0 : 280
                easing.type: Easing.OutCubic
            }
        }
    }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 150 } }
    contentItem: ScrollView {
        id: scroll
        clip: true
        contentWidth: availableWidth
        contentHeight: card.implicitHeight
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        PaymentRequestCard {
            id: card
            interactionOwner: root
            visible: root.visible
            width: scroll.availableWidth
            wallet: root.wallet
            request: root.wallet ? root.wallet.detailPaymentRequest : null
            modalView: true
            onCloseRequested: root.close()
            onRequestAgain: function(requestId) {
                if (!root.contextIsValid()) return
                repeatContext.begin()
                root.repeatRequestId = requestId
                root.close()
            }
            onDeleted: function(requestId) {
                root.deletedRequestId = requestId
                root.close()
            }
        }
    }
}
