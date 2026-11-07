// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"

DialogPopup {
    id: root
    objectName: "paymentRequestModal"
    property var wallet: null
    property var openedWallet: null
    property real originY: -1
    property string deletedRequestId: ""
    property var deletedRequestWallet: null
    property string repeatRequestId: ""
    readonly property alias card: card

    title: qsTr("Payment request")
    sheet: true
    implicitWidth: 560
    implicitHeight: card.implicitHeight
    padding: 0
    horizontalPadding: 0
    // The card draws the sheet surface.
    background: null
    // Opening from a row rises from that row; the exit only fades.
    enterOffset: originY >= 0 && parent ? originY - Math.round((parent.height - height) / 2) : 16
    exitOffset: 0

    function openRequest(requestId, fromY) {
        if (!wallet || !wallet.loadPaymentRequestDetail(requestId)) return false
        originY = fromY === undefined ? -1 : fromY
        card.resetFields()
        card.beginOpening()
        openedWallet = wallet
        open()
        return true
    }
    onAboutToShow: if (scroll.contentItem) scroll.contentItem.contentY = 0
    // A result arriving during the close animation belongs to an earlier opening.
    onAboutToHide: card.beginOpening()
    onClosed: {
        if (deletedRequestWallet && deletedRequestId) {
            for (const request of [deletedRequestWallet.currentPaymentRequest, deletedRequestWallet.detailPaymentRequest]) {
                if (request && request.id === deletedRequestId) request.clear()
            }
        }
        deletedRequestId = ""
        deletedRequestWallet = null
        openedWallet = null
        card.resetFields()
        if (repeatRequestId) {
            const requestId = repeatRequestId
            const requestWallet = wallet
            repeatRequestId = ""
            // Let the Receive page finish clearing its previous creation draft.
            Qt.callLater(function() {
                if (!requestWallet || requestWallet !== root.wallet) return
                requestWallet.usePaymentRequestAsTemplate(requestId)
                walletController.requestOpenReceive(requestWallet)
            })
        }
    }
    onWalletChanged: {
        if (openedWallet && wallet !== openedWallet) {
            repeatRequestId = ""
            close()
        }
    }

    contentItem: ScrollView {
        id: scroll
        clip: true
        contentWidth: availableWidth
        contentHeight: card.implicitHeight
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        PaymentRequestCard {
            id: card
            visible: root.visible
            width: scroll.availableWidth
            wallet: root.wallet
            request: root.wallet ? root.wallet.detailPaymentRequest : null
            modalView: true
            onCloseRequested: root.close()
            onRequestAgain: function(requestId) {
                root.repeatRequestId = requestId
                root.close()
            }
            onDeleted: function(requestId, requestWallet) {
                root.deletedRequestId = requestId
                root.deletedRequestWallet = requestWallet
                root.close()
            }
            onDeletedInEarlierOpening: function(requestId, requestWallet) {
                const request = requestWallet ? requestWallet.currentPaymentRequest : null
                if (request && request.id === requestId) request.clear()
                // The same request can be reopened while its deletion is pending.
                if (requestWallet === root.wallet && card.request && card.request.id === requestId) {
                    root.deletedRequestId = requestId
                    root.deletedRequestWallet = requestWallet
                    root.close()
                }
            }
        }
    }
}
