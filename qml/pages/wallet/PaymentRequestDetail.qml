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
        root.StackView.view.pop()
        walletController.requestOpenReceive()
    }

    function useAsTemplate() {
        if (!root.wallet || !root.request) return
        root.wallet.usePaymentRequestAsTemplate(root.request.id)
        root.StackView.view.pop()
        walletController.requestOpenReceive()
    }

    function formatAmount(satoshi) {
        if (satoshi <= 0) return ""
        var btc = (satoshi / 100000000).toFixed(8)
        var parts = btc.split(".")
        var intPart = parts[0]
        var decPart = parts[1]
        var spaced = decPart.substring(0, 2)
        if (decPart.length > 2) spaced += " " + decPart.substring(2, 5)
        if (decPart.length > 5) spaced += " " + decPart.substring(5, 8)
        return "₿ " + intPart + "." + spaced
    }

    function formatAddressRichText(addr) {
        if (!addr) return ""
        var c1 = Theme.color.neutral9
        var c2 = Theme.color.neutral7
        var html = ""
        for (var i = 0; i < addr.length; i += 4) {
            var chunk = addr.substring(i, Math.min(i + 4, addr.length))
            var color = (Math.floor(i / 4) % 2 === 0) ? c1 : c2
            if (i > 0) html += ' '
            html += '<nobr><font color="' + color + '">' + chunk + '</font></nobr>'
        }
        return html
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
                text: root.request ? formatAmount(root.request.amount.satoshi) : ""
                font.pixelSize: 24
                bold: true
                color: Theme.color.neutral9
                horizontalAlignment: Text.AlignHCenter
            }

            Item {
                Layout.fillWidth: true
                Layout.topMargin: 15
                visible: root.request !== null && root.request.amount.satoshi <= 0
                implicitHeight: addAmountText.implicitHeight

                CoreText {
                    id: addAmountText
                    anchors.centerIn: parent
                    text: qsTr("Add amount")
                    font.pixelSize: 24
                    bold: true
                    color: Theme.color.neutral7
                    horizontalAlignment: Text.AlignHCenter
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.editRequest()
                }
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
                    label: qsTr("Your name")
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

                Item {
                    Layout.fillWidth: true
                    visible: root.request !== null && root.request.address !== ""
                    implicitHeight: addressCol.implicitHeight + 20

                    ColumnLayout {
                        id: addressCol
                        anchors.left: parent.left
                        anchors.right: copyAddressIcon.left
                        anchors.rightMargin: 10
                        anchors.top: parent.top
                        anchors.topMargin: 10
                        spacing: 4

                        CoreText {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            text: qsTr("Address")
                            font.pixelSize: 13
                            color: Theme.color.neutral7
                        }
                        CoreText {
                            id: addressValue
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            font.pixelSize: 18
                            textFormat: Text.RichText
                            wrapMode: Text.WordWrap
                            text: root.request ? formatAddressRichText(root.request.address) : ""
                        }
                    }

                    Icon {
                        id: copyAddressIcon
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        source: "qrc:/icons/copy"
                        color: Theme.color.neutral9
                        size: 24
                    }

                    MouseArea {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 44
                        height: 44
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            if (root.request) {
                                Clipboard.setText(root.request.address)
                                copiedToast.show()
                            }
                        }
                    }
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
                    id: shareButton
                    objectName: "paymentRequestDetailShare"
                    Layout.fillWidth: true
                    enabled: false
                    hoverEnabled: AppMode.isDesktop
                    implicitHeight: 46
                    Accessible.name: qsTr("Share payment request")

                    contentItem: RowLayout {
                        spacing: 6
                        Item { Layout.fillWidth: true }
                        Icon {
                            source: "qrc:/icons/share"
                            color: shareButton.enabled ? Theme.color.neutral9 : Theme.color.neutral5
                            size: 24
                        }
                        CoreText {
                            text: qsTr("Share")
                            bold: true
                            font.pixelSize: 18
                            color: shareButton.enabled ? Theme.color.neutral9 : Theme.color.neutral5
                        }
                        Item { Layout.fillWidth: true }
                    }

                    background: Rectangle {
                        implicitHeight: 46
                        color: Theme.color.background
                        radius: 5
                        border.width: 1
                        border.color: shareButton.hovered && shareButton.enabled ? Theme.color.neutral9 : Theme.color.neutral6
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                    }
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
                            copiedToast.show()
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

            CopiedToast {
                id: copiedToast
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 8
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 20
            }
        }
    }

    QRCodePopup {
        id: qrPopup
        code: root.request ? root.request.qrPayload : ""
        label: root.request ? root.request.label : ""
        onCopyRequested: {
            if (root.request) {
                Clipboard.setText(root.request.qrPayload)
                copiedToast.show()
            }
            qrPopup.close()
        }
    }
}
