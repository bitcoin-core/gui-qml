// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

Page {
    id: root
    objectName: "requestPaymentPage"
    property var wallet: walletController.selectedWallet
    property var request: wallet ? wallet.currentPaymentRequest : null
    property var clipboard: Clipboard
    property var presentedRequest: null
    property bool pendingNextAddress: false
    property string pendingAddressType: ""
    readonly property real pageSidePadding: width < 640 ? 16 : 40
    readonly property real headerContentWidth: Math.max(0, Math.min(660, width - pageSidePadding * 2))
    readonly property alias requestModal: requestModal
    readonly property alias draftCard: card
    signal addressHistoryRequested()
    signal paymentRequestCreated()
    background: Rectangle { color: Theme.color.neutral0 }
    padding: 0

    function ensureReceivingAddress(next, type) {
        if (!wallet) return
        pendingNextAddress = !!next
        pendingAddressType = type || ""
        if (wallet.ensureReceivingAddress(pendingNextAddress, pendingAddressType)) card.errorText = ""
        else if (wallet.receivingAddress.needsUnlock) passphrasePopup.open()
        //: Error shown in Receive when preparing an address for a payment request fails.
        else card.errorText = qsTr("A receiving address could not be generated. Please try again.")
    }

    function prepareReceivingAddress() {
        const type = root.request && !root.request.id && root.wallet && root.wallet.receivingAddress.address === ""
            ? root.request.addressType.toLowerCase() : ""
        root.ensureReceivingAddress(false, type)
    }

    // The Addresses page may bring an existing request into Receive.
    // Saved requests always use the same modal, including this entry point.
    onVisibleChanged: {
        if (visible) Qt.callLater(function() {
            if (root.visible) root.prepareReceivingAddress()
            if (root.visible && root.request && root.request.id && !requestModal.visible) {
                root.presentedRequest = root.request
                requestModal.openRequest(root.request.id)
            }
        })
    }
    Component.onCompleted: Qt.callLater(function() {
        if (root.visible) root.prepareReceivingAddress()
    })

    Item {
        objectName: "requestHistoryCount"
        visible: false
        property int count: root.wallet ? root.wallet.receiveRequests.count : 0
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        contentHeight: content.implicitHeight + 48
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: content
            width: scroll.availableWidth
            spacing: 24
            RowLayout {
                objectName: "receivePageHeader"
                Layout.fillWidth: true
                Layout.preferredWidth: root.headerContentWidth
                Layout.maximumWidth: root.headerContentWidth
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 28
                CoreText {
                    Layout.fillWidth: true
                    text: qsTr("Receive bitcoin")
                    font: Theme.text.headline.font
                    lineHeight: Theme.text.headline.lineHeight
                    lineHeightMode: Text.FixedHeight
                    horizontalAlignment: Text.AlignLeft
                }
                OverflowMenuButton {
                    id: receiveAddressSettingsButton
                    objectName: "receiveAddressSettingsButton"
                    iconSource: "qrc:/icons/address-settings.svg"
                    iconSize: 28
                    Accessible.name: qsTr("Address settings")
                    checked: receiveAddressTypeMenu.opened
                    onClicked: receiveAddressTypeMenu.opened ? receiveAddressTypeMenu.close() : receiveAddressTypeMenu.open()
                    ContextMenu {
                        id: receiveAddressTypeMenu
                        objectName: "receiveAddressTypeMenu"
                        x: receiveAddressSettingsButton.width - width
                        y: receiveAddressSettingsButton.height + 8
                        minMenuWidth: 320
                        modal: true
                        dim: false
                        ContextMenuPicker {
                            objectName: "receiveAddressTypePicker"
                            title: qsTr("Address Type")
                            textRole: "label"
                            valueRole: "id"
                            subtitleRole: "description"
                            objectNameRole: "objectName"
                            subtitleObjectNameRole: "subtitleObjectName"
                            model: root.wallet ? root.wallet.availableReceiveAddressTypes().map(function(type) {
                                return {
                                    id: type.id,
                                    label: type.label,
                                    description: type.description,
                                    objectName: "receiveAddressType_" + type.id,
                                    subtitleObjectName: "receiveAddressTypeDescription_" + type.id
                                }
                            }) : []
                            currentValue: root.wallet && root.wallet.receivingAddress
                                ? root.wallet.receivingAddress.addressType.toLowerCase() : ""
                            enabled: walletController.initialized
                            onActivated: function(value) {
                                receiveAddressTypeMenu.close()
                                root.ensureReceivingAddress(false, value)
                            }
                        }
                    }
                }
                OverflowMenuButton {
                    id: receiveMoreButton
                    objectName: "receiveMoreButton"
                    Layout.leftMargin: 6
                    checked: receiveMoreMenu.opened
                    onClicked: receiveMoreMenu.opened ? receiveMoreMenu.close() : receiveMoreMenu.open()
                    ContextMenu {
                        id: receiveMoreMenu
                        objectName: "receiveMoreMenu"
                        x: receiveMoreButton.width - width
                        y: receiveMoreButton.height + 8
                        modal: true
                        dim: false
                        ContextMenuButton {
                            objectName: "requestPaymentHistoryButton"
                            text: qsTr("View addresses")
                            onTriggered: root.addressHistoryRequested()
                        }
                        ContextMenuDivider {}
                        ContextMenuButton {
                            objectName: "receiveClearFormButton"
                            text: qsTr("Clear form")
                            iconSource: "qrc:/icons/cross"
                            enabled: !!root.request && !root.request.id
                            onTriggered: card.clearForm()
                        }
                    }
                }
            }
            ColumnLayout {
                objectName: "receiveWorkspace"
                Layout.preferredWidth: root.headerContentWidth
                Layout.maximumWidth: root.headerContentWidth
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                spacing: 24
                PaymentRequestCard {
                    id: card
                    Layout.preferredWidth: 532
                    Layout.minimumWidth: 300
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    wallet: root.wallet
                    request: root.request
                    clipboard: root.clipboard
                    enabled: walletController.initialized
                    onRetryAddressRequested: root.ensureReceivingAddress(root.pendingNextAddress, root.pendingAddressType)
                    onCreated: function(requestId) {
                        root.presentedRequest = root.request
                        root.paymentRequestCreated()
                        requestModal.openRequest(requestId)
                    }
                }
            }
        }
    }
    PaymentRequestModal {
        id: requestModal
        wallet: root.wallet
        onClosed: {
            if (root.presentedRequest && root.request === root.presentedRequest) root.request.clear()
            root.presentedRequest = null
            card.resetFields()
            if (root.visible) root.prepareReceivingAddress()
        }
    }
    Connections {
        target: root.wallet ? root.wallet.receivingAddress : null
        function onPaymentReceivedChanged() {
            if (root.wallet.receivingAddress.paymentReceived) {
                Qt.callLater(function() { if (root.visible) root.ensureReceivingAddress(false, "") })
            }
        }
    }
    WalletPassphrasePopup {
        id: passphrasePopup
        parent: Overlay.overlay
        width: Math.min(420, parent ? parent.width - 32 : 420)
        //: Title of the password dialog shown when Receive needs to unlock the wallet to generate an address.
        titleText: qsTr("Enter wallet password")
        //: Explains why the wallet password is needed to prepare a payment request.
        descriptionText: qsTr("Enter your wallet password to create a receiving address.")
        //: Button that unlocks the wallet and generates the address for a payment request.
        confirmText: qsTr("Unlock and create address")
        onSubmitted: function(passphrase) {
            if (root.wallet.ensureReceivingAddressWithPassphrase(passphrase, root.pendingNextAddress, root.pendingAddressType)) {
                card.errorText = ""
                close()
            //: Error shown when an address cannot be generated after unlocking the wallet.
            } else errorText = root.wallet.receivingAddress.unlockError || qsTr("The receiving address could not be created.")
        }
    }
    Connections {
        target: walletController
        function onOpenReceiveRequested() {
            Qt.callLater(function() {
                card.resetFields()
                if (root.visible) root.prepareReceivingAddress()
            })
        }
        function onSelectedWalletChanged() {
            passphrasePopup.close()
            requestModal.close()
            if (root.request) root.request.clear()
            card.resetFields()
            Qt.callLater(function() { if (root.visible) root.ensureReceivingAddress(false, "") })
        }
    }
}
