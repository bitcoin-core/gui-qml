// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../components"
import "../../controls"

Page {
    id: root
    property WalletQmlModel wallet: null
    property bool walletViewActive: true
    readonly property WalletReceiveModel receive: wallet ? wallet.receive : null
    signal paymentRequestCreated()
    signal addressHistoryRequested()
    objectName: "receiveWorkspace"
    padding: 0
    background: Rectangle { color: Theme.color.background }
    onVisibleChanged: if (visible && receive) {
        if (receive.reservation.selectedRequestId.length > 0) createdRequest.openRequest(receive.reservation.selectedRequestId)
        else receive.ensureAddress()
    } else password.close()
    Component.onCompleted: if (visible && receive) receive.ensureAddress()
    ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        contentHeight: column.height + 48
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: column
            width: scroll.availableWidth
            spacing: Theme.space.xl
            CoreText { Layout.fillWidth: true; Layout.topMargin: Theme.space.xxl; text: qsTr("Receive bitcoin"); textStyle: Theme.text.headline }
            RowLayout {
                NeutralButton { objectName: "receiveNewRequestButton"; text: qsTr("New request"); enabled: root.receive !== null && !root.receive.replacementPending; onClicked: root.receive.newRequest() }
                NeutralButton { objectName: "receiveNextAddressButton"; text: qsTr("Next address"); enabled: root.receive !== null && !root.receive.reservation.busy; onClicked: root.receive.nextAddress() }
                NeutralButton { text: qsTr("View addresses"); onClicked: root.addressHistoryRequested() }
            }
            ContextMenuPicker {
                objectName: "receiveAddressTypePicker"
                Layout.fillWidth: true
                title: qsTr("Address type")
                textRole: "label"
                valueRole: "id"
                subtitleRole: "description"
                model: root.wallet ? root.wallet.receiveAddressTypes : []
                currentValue: root.receive ? root.receive.reservation.addressType : ""
                enabled: root.receive !== null && !root.receive.reservation.busy && !root.receive.replacementPending
                onActivated: function(value) { root.receive.chooseAddressType(value) }
            }
            CoreText { Layout.fillWidth: true; text: root.receive ? root.receive.reservation.error.message || root.receive.error.message : ""; visible: text.length > 0; color: Theme.color.dangerText }
            NeutralButton {
                objectName: "receiveRetryAddressButton"
                text: qsTr("Retry address")
                visible: root.receive !== null && (root.receive.reservation.phase === ReceiveReservationModel.Failed || root.receive.reservation.phase === ReceiveReservationModel.Cancelled)
                onClicked: root.receive.reservation.retry()
            }
            ReceiveRequestEditorCard {
                Layout.fillWidth: true
                viewActive: root.visible && root.walletViewActive
                receive: root.receive
                editor: root.receive ? root.receive.editor : null
                onRequestAgain: function(id) { if (root.receive) root.receive.useTemplate(id) }
            }
        }
    }
    WalletPassphrasePopup {
        id: password
        property ReceiveAuthorizationPrompt originalPrompt: null
        parent: Overlay.overlay
        width: Math.min(420, parent ? parent.width - 32 : 420)
        popupObjectName: "receivePassphrasePopup"
        passphraseFieldObjectName: "receivePassphraseField"
        errorTextObjectName: "receivePassphraseError"
        cancelButtonObjectName: "receivePassphraseCancelButton"
        confirmButtonObjectName: "receivePassphraseConfirmButton"
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter your wallet password to create a receiving address.")
        confirmText: qsTr("Unlock and create address")
        busy: root.receive !== null && root.receive.reservation.busy
        errorText: originalPrompt ? originalPrompt.errorText : ""
        onSubmitted: function(text) { if (originalPrompt && originalPrompt.active) originalPrompt.answer(text) }
        onClosed: if (originalPrompt && originalPrompt.active) originalPrompt.cancel()
    }
    Connections {
        target: root.receive ? root.receive.reservation : null
        function onChanged() {
            const prompt = root.receive ? root.receive.reservation.prompt : null
            if (root.visible && prompt && prompt.active) {
                if (password.originalPrompt !== prompt) password.originalPrompt = prompt
                if (!password.opened) password.open()
            } else password.close()
        }
    }
    Connections {
        target: root.receive
        function onExistingRequestRequested(id) { if (root.visible && root.walletViewActive) createdRequest.openRequest(id) }
        function onRequestPersisted(result) {
            if (!root.visible || !root.walletViewActive || !result.requestSaved || !root.receive || !root.receive.editor || root.receive.editor.editorId !== result.editorId) return
            const original = root.wallet
            const owner = root.receive
            root.paymentRequestCreated()
            if (root && root.wallet === original && root.receive === owner) createdRequest.openRequest(result.requestId)
        }
    }
    ReceiveRequestModal { id: createdRequest; wallet: root.wallet; contextActive: root.walletViewActive }
    onWalletChanged: password.close()
}
