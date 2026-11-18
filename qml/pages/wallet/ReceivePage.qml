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
    // Without a loaded wallet there is nothing to receive into.
    readonly property bool walletLoaded: receive !== null
    readonly property bool noWalletsAvailable: ApplicationModels.walletController.noWalletsFound
    readonly property ReceiveAuthorizationPrompt prompt: receive ? receive.reservation.prompt : null
    signal paymentRequestCreated()
    signal addressHistoryRequested()
    signal walletSelectionRequested()
    objectName: "receivePage"
    padding: 0
    background: Rectangle { color: Theme.color.background }
    onVisibleChanged: if (visible && receive) {
        if (receive.reservation.selectedRequestId.length > 0) createdRequest.openRequest(receive.reservation.selectedRequestId)
        else receive.ensureAddress()
    }
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
            PageHeading {
                objectName: "receiveEmptyHeading"
                visible: !root.walletLoaded
                Layout.fillWidth: true
                title: root.noWalletsAvailable ? "" : qsTr("No wallet selected")
                description: root.noWalletsAvailable
                    ? qsTr("Add a wallet to receive bitcoin.")
                    : qsTr("Select a wallet to receive bitcoin.")
                titleAlignment: Text.AlignHCenter
            }
            NeutralButton {
                objectName: "receiveEmptyAction"
                buttonSize: NeutralButton.Large
                visible: !root.walletLoaded
                Layout.preferredWidth: 220
                Layout.alignment: Qt.AlignHCenter
                text: root.noWalletsAvailable ? qsTr("Add wallet…") : qsTr("Select wallet…")
                onClicked: root.walletSelectionRequested()
            }
            RowLayout {
                visible: root.walletLoaded
                NeutralButton { objectName: "receiveNewRequestButton"; text: qsTr("New request"); enabled: root.receive !== null && !root.receive.replacementPending; onClicked: root.receive.newRequest() }
                NeutralButton { objectName: "receiveNextAddressButton"; text: qsTr("Next address"); enabled: root.receive !== null && !root.receive.reservation.busy; onClicked: root.receive.nextAddress() }
                NeutralButton { objectName: "receiveViewAddressesButton"; text: qsTr("View addresses"); onClicked: root.addressHistoryRequested() }
            }
            ContextMenuPicker {
                objectName: "receiveAddressTypePicker"
                visible: root.walletLoaded
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
                visible: root.walletLoaded
                Layout.fillWidth: true
                viewActive: root.visible && root.walletViewActive
                closable: false
                receive: root.receive
                editor: root.receive ? root.receive.editor : null
                onRequestAgain: function(id) { if (root.receive) root.receive.useTemplate(id) }
            }
        }
    }
    // The reservation asks for the wallet password; its prompt decides
    // whether this shows.
    WalletPassphrasePopup {
        id: password
        parent: Overlay.overlay
        width: Math.min(420, parent ? parent.width - 32 : 420)
        shown: root.visible && root.prompt !== null && root.prompt.active
        popupObjectName: "receivePassphrasePopup"
        passphraseFieldObjectName: "receivePassphraseField"
        errorTextObjectName: "receivePassphraseError"
        cancelButtonObjectName: "receivePassphraseCancelButton"
        confirmButtonObjectName: "receivePassphraseConfirmButton"
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter your wallet password to create a receiving address.")
        confirmText: qsTr("Unlock and create address")
        busy: root.receive !== null && root.receive.reservation.busy
        errorText: root.prompt ? root.prompt.errorText : ""
        onSubmitted: function(text) { if (root.prompt && root.prompt.active) root.prompt.answer(text) }
        onCloseRequested: if (root.prompt && root.prompt.active) root.prompt.cancel()
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
}
