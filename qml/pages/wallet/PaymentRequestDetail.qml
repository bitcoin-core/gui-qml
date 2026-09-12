// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../controls/utils.js" as Utils
import "../../components"

Page {
    id: root
    objectName: "paymentRequestDetailPage"
    background: null

    property WalletQmlModel wallet: walletController.selectedWallet
    property PaymentRequest request: wallet ? wallet.detailPaymentRequest : null

    function editRequest() {
        if (!root.wallet || !root.request) return
        root.wallet.loadPaymentRequest(root.request.id)
        root.wallet.currentPaymentRequest.edit()
        walletController.requestOpenReceive()
    }

    function useAsTemplate() {
        if (!root.wallet || !root.request) return
        root.wallet.usePaymentRequestAsTemplate(root.request.id)
        root.StackView.view.pop()
        walletController.requestOpenReceive()
    }

    header: NavigationBar2 {
        leftItem: NavButton {
            objectName: "paymentRequestDetailBack"
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: root.StackView.view.pop()
        }
        centerItem: CoreText {
            text: qsTr("Payment request")
            font.pixelSize: 18
            bold: true
            color: Theme.color.neutral9
        }
        rightItem: IconButton {
            id: menuButton
            objectName: "paymentRequestDetailMenu"
            checked: optionsPopup.opened
            iconSource: "image://images/ellipsis"
            Accessible.name: qsTr("Options menu")
            onClicked: optionsPopup.open()
        }
    }

    PaymentDetailOptionsPopup {
        id: optionsPopup
        x: root.width - width - 20
        y: 0

        hasLabel: root.request ? root.request.label !== "" : false
        hasMessage: root.request ? root.request.message !== "" : false
        hasNoteSelf: root.request ? root.request.noteSelf !== "" : false

        onAddName: root.editRequest()
        onAddMessage: root.editRequest()
        onAddNoteSelf: root.editRequest()
        onUseAsTemplate: root.useAsTemplate()
        onDeleteFromHistory: {
            if (root.wallet && root.request) {
                root.wallet.removeReceiveRequest(root.request.id)
                root.StackView.view.pop()
            }
        }
    }

    ScrollView {
        id: scrollView
        anchors.fill: parent
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: scrollView.availableWidth
            spacing: 0

            Icon {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 20
                source: "qrc:/icons/triangle-down"
                color: Theme.color.purple
                size: 40
            }

            CoreText {
                Layout.fillWidth: true
                Layout.topMargin: 15
                visible: root.request !== null && root.request.amount.satoshi > 0
                text: root.request ? root.request.amount.displayWithUnit : ""
                font.pixelSize: 24
                bold: true
                color: Theme.color.neutral9
                horizontalAlignment: Text.AlignHCenter
            }

            CoreText {
                objectName: "paymentRequestDetailAnyAmount"
                Layout.fillWidth: true
                Layout.topMargin: 15
                visible: root.request !== null && root.request.amount.satoshi <= 0
                //: Shown in place of the amount on a payment request created without one: the payer chooses how much to send. The amount of a saved request is fixed; a different amount needs a new request.
                text: qsTr("Any amount")
                font.pixelSize: 24
                bold: true
                color: Theme.color.neutral7
                horizontalAlignment: Text.AlignHCenter
            }

            CoreText {
                Layout.fillWidth: true
                Layout.topMargin: 4
                visible: root.request !== null && root.request.createdIso !== ""
                text: root.request ? qsTr("Created %1").arg(Utils.formatRelativeTime(root.request.createdIso)) : ""
                font.pixelSize: 15
                color: Theme.color.neutral7
                horizontalAlignment: Text.AlignHCenter
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.topMargin: 25
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.maximumWidth: 470
                Layout.alignment: Qt.AlignHCenter
                spacing: 0

                DetailEditRow {
                    visible: root.request !== null && root.request.label !== ""
                    //: The request's label: shared with the payer and kept as the address book label.
                    label: qsTr("Label")
                    value: root.request ? root.request.label : ""
                    onEditClicked: root.editRequest()
                }

                DetailEditRow {
                    visible: root.request !== null && root.request.message !== ""
                    label: qsTr("Message")
                    value: root.request ? root.request.message : ""
                    onEditClicked: root.editRequest()
                }

                DetailEditRow {
                    visible: root.request !== null && root.request.noteSelf !== ""
                    label: qsTr("Note to self")
                    value: root.request ? root.request.noteSelf : ""
                    onEditClicked: root.editRequest()
                }

                AddressDetailRow {
                    Layout.fillWidth: true
                    visible: root.request !== null && root.request.address !== ""
                    address: root.request ? root.request.address : ""
                    onCopied: copiedToast.show(copyButton)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 25
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.maximumWidth: 470
                Layout.alignment: Qt.AlignHCenter
                spacing: 10

                Button {
                    id: editButton
                    objectName: "paymentRequestDetailEdit"
                    Layout.fillWidth: true
                    hoverEnabled: AppMode.isDesktop
                    implicitHeight: 46
                    Accessible.name: qsTr("Edit payment request")

                    contentItem: RowLayout {
                        spacing: 6
                        Item { Layout.fillWidth: true }
                        Icon {
                            source: "qrc:/icons/edit"
                            color: Theme.color.neutral9
                            size: 24
                        }
                        CoreText {
                            text: qsTr("Edit")
                            bold: true
                            font.pixelSize: 18
                            color: Theme.color.neutral9
                        }
                        Item { Layout.fillWidth: true }
                    }

                    background: Rectangle {
                        implicitHeight: 46
                        color: Theme.color.background
                        radius: 5
                        border.width: 1
                        border.color: editButton.pressed ? Theme.color.orangeLight2 : editButton.hovered ? Theme.color.neutral9 : Theme.color.neutral6
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                    }

                    onClicked: root.editRequest()
                }

                Button {
                    id: copyButton
                    objectName: "paymentRequestDetailCopy"
                    Layout.fillWidth: true
                    hoverEnabled: AppMode.isDesktop
                    implicitHeight: 46
                    Accessible.name: qsTr("Copy payment request")

                    contentItem: RowLayout {
                        spacing: 6
                        Item { Layout.fillWidth: true }
                        Icon {
                            source: "qrc:/icons/copy"
                            color: Theme.color.neutral9
                            size: 24
                        }
                        CoreText {
                            text: qsTr("Copy")
                            bold: true
                            font.pixelSize: 18
                            color: Theme.color.neutral9
                        }
                        Item { Layout.fillWidth: true }
                    }

                    background: Rectangle {
                        implicitHeight: 46
                        color: Theme.color.background
                        radius: 5
                        border.width: 1
                        border.color: copyButton.pressed ? Theme.color.orangeLight2 : copyButton.hovered ? Theme.color.neutral9 : Theme.color.neutral6
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                    }

                    onClicked: {
                        if (root.request) {
                            Clipboard.setText(root.request.qrPayload)
                            copiedToast.show(copyButton)
                        }
                    }
                }

                Button {
                    id: qrButton
                    objectName: "paymentRequestDetailQRButton"
                    Layout.fillWidth: true
                    hoverEnabled: AppMode.isDesktop
                    implicitHeight: 46
                    Accessible.name: qsTr("Show QR code")

                    contentItem: RowLayout {
                        spacing: 6
                        Item { Layout.fillWidth: true }
                        Icon {
                            source: "qrc:/icons/qr-code"
                            color: Theme.color.neutral9
                            size: 24
                        }
                        CoreText {
                            text: qsTr("QR Code")
                            bold: true
                            font.pixelSize: 18
                            color: Theme.color.neutral9
                        }
                        Item { Layout.fillWidth: true }
                    }

                    background: Rectangle {
                        implicitHeight: 46
                        color: Theme.color.background
                        radius: 5
                        border.width: 1
                        border.color: qrButton.pressed ? Theme.color.orangeLight2 : qrButton.hovered ? Theme.color.neutral9 : Theme.color.neutral6
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                    }

                    onClicked: qrPopup.open()
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 20
            }
        }
    }

    ToastPopup {
        id: copiedToast
        popupAnchor: copyButton
        popupOffset: 4
        text: qsTr("Copied")
        backgroundColor: Theme.color.green
        borderColor: Theme.color.green
        textColor: Theme.color.white
        iconSource: "image://images/check"
        iconColor: Theme.color.white
    }

    QRCodePopup {
        id: qrPopup
        code: root.request ? root.request.qrPayload : ""
        label: root.request ? root.request.label : ""
        onCopyRequested: {
            if (root.request) {
                Clipboard.setText(root.request.qrPayload)
                copiedToast.show(copyButton)
            }
            qrPopup.close()
        }
    }
}
