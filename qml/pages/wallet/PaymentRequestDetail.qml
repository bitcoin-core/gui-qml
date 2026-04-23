// Copyright (c) 2026 The Bitcoin Core developers
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
    objectName: "paymentRequestDetailPage"
    background: null

    signal done()

    property WalletQmlModel wallet: walletController.selectedWallet
    property PaymentRequest request: wallet ? wallet.currentPaymentRequest : null
    property bool detailsExpanded: false

    function formatRelativeTime(isoString) {
        if (!isoString) return ""
        var then = new Date(isoString)
        var now = new Date()
        var diffSec = Math.floor((now - then) / 1000)
        if (diffSec < 60) return qsTr("just now")
        var m = Math.floor(diffSec / 60)
        if (diffSec < 3600) return m === 1 ? qsTr("1 minute ago") : qsTr("%1 minutes ago").arg(m)
        var h = Math.floor(diffSec / 3600)
        if (diffSec < 86400) return h === 1 ? qsTr("1 hour ago") : qsTr("%1 hours ago").arg(h)
        var d = Math.floor(diffSec / 86400)
        return d === 1 ? qsTr("1 day ago") : qsTr("%1 days ago").arg(d)
    }

    header: NavigationBar2 {
        centerItem: CoreText {
            text: qsTr("Payment request")
            font.pixelSize: 18
            bold: true
            color: Theme.color.neutral9
        }
        rightItem: NavButton {
            objectName: "paymentRequestDetailDone"
            text: qsTr("Done")
            onClicked: root.done()
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

            // Summary text
            ColumnLayout {
                Layout.topMargin: 30
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: 400
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                spacing: 4

                CoreText {
                    Layout.fillWidth: true
                    visible: root.request !== null && root.request.amount.satoshi > 0
                    text: {
                        if (!root.request) return ""
                        var label = root.request.amount.unitLabel
                        if (root.request.amount.unit === BitcoinAmount.SAT && root.request.amount.satoshi !== 1)
                            label = "sats"
                        return qsTr("Requesting ") + root.request.amount.display + " " + label
                    }
                    font.pixelSize: 16
                    color: Theme.color.neutral9
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                CoreText {
                    Layout.fillWidth: true
                    visible: root.request !== null && root.request.label !== ""
                    text: root.request ? qsTr("Labelled \"%1\"").arg(root.request.label) : ""
                    font.pixelSize: 16
                    color: Theme.color.neutral7
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }

                CoreText {
                    Layout.fillWidth: true
                    visible: root.request !== null && root.request.message !== ""
                    text: root.request ? "\"%1\"".arg(root.request.message) : ""
                    font.pixelSize: 16
                    color: Theme.color.neutral7
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }

            // QR code
            Pane {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 30
                Layout.preferredWidth: 250
                Layout.preferredHeight: 250
                padding: 0
                background: Rectangle {
                    color: Theme.color.neutral2
                    visible: qrImage.code === ""
                }
                contentItem: QRImage {
                    id: qrImage
                    objectName: "paymentRequestDetailQRCode"
                    backgroundColor: "transparent"
                    foregroundColor: Theme.color.neutral9
                    code: root.request ? root.request.qrPayload : ""
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 20
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.maximumWidth: 470
                Layout.alignment: Qt.AlignHCenter
                spacing: 15

                OutlineButton {
                    objectName: "paymentRequestDetailShare"
                    Layout.fillWidth: true
                    enabled: false
                    text: qsTr("Share")
                    iconSource: "image://images/share"
                    onClicked: {
                        if (root.request) {
                            Clipboard.setText(root.request.address)
                            copiedToast.show()
                        }
                    }
                }

                OutlineButton {
                    objectName: "paymentRequestDetailCopy"
                    Layout.fillWidth: true
                    text: qsTr("Copy")
                    iconSource: "image://images/copy"
                    onClicked: {
                        if (root.request) {
                            Clipboard.setText(root.request.qrPayload)
                            copiedToast.show()
                        }
                    }
                }

                OutlineButton {
                    id: menuButton
                    objectName: "paymentRequestDetailMenu"
                    iconSource: "image://images/ellipsis"
                    Layout.preferredWidth: 46
                    Layout.preferredHeight: 46
                    leftPadding: 0
                    rightPadding: 0
                    onClicked: optionsPopup.open()
                }

                PaymentDetailOptionsPopup {
                    id: optionsPopup
                    x: menuButton.x - width + menuButton.width
                    y: menuButton.y + menuButton.height

                    hasPaymentInfo: root.request ? root.request.hasPaymentInfo : false

                    onCopyAddress: {
                        if (root.request) {
                            Clipboard.setText(root.request.address)
                            copiedToast.show()
                        }
                    }
                }
            }

            CopiedToast {
                id: copiedToast
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 8
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 20
                spacing: 5

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.detailsExpanded = !root.detailsExpanded
                }

                Icon {
                    source: "image://images/caret-down-medium-filled"
                    color: Theme.color.neutral8
                    size: 20
                    rotation: root.detailsExpanded ? 180 : 0
                    Behavior on rotation {
                        NumberAnimation { duration: 150 }
                    }
                }

                CoreText {
                    text: qsTr("Details")
                    font.pixelSize: 15
                    color: Theme.color.neutral9
                }
            }

            // Details content
            ColumnLayout {
                id: detailsContent
                visible: root.detailsExpanded
                Layout.fillWidth: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                Layout.maximumWidth: 470
                Layout.alignment: Qt.AlignHCenter
                spacing: 10

                // URI (Payment Request)
                Item {
                    Layout.fillWidth: true
                    visible: optionsPopup.showPaymentRequest && root.request !== null && root.request.qrPayload !== ""
                    implicitHeight: uriValueText.implicitHeight + 16

                    CoreText {
                        id: uriKeyText
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.topMargin: 8
                        width: 110
                        horizontalAlignment: Text.AlignLeft
                        text: qsTr("URI")
                        font.pixelSize: 18
                    }
                    CoreText {
                        id: uriValueText
                        anchors.left: uriKeyText.right
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 8
                        horizontalAlignment: Text.AlignLeft
                        text: root.request ? root.request.qrPayload : ""
                        font.pixelSize: 18
                        color: Theme.color.neutral9
                        wrapMode: Text.WrapAnywhere
                    }
                }

                Separator {
                    Layout.fillWidth: true
                    visible: optionsPopup.showPaymentRequest && root.request !== null && root.request.qrPayload !== ""
                    color: Theme.color.neutral5
                }

                // Address Type
                Item {
                    Layout.fillWidth: true
                    visible: optionsPopup.showAddressType && root.request !== null && root.request.addressType !== ""
                    implicitHeight: addressTypeLabel.implicitHeight + 16

                    CoreText {
                        id: addressTypeLabel
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 110
                        horizontalAlignment: Text.AlignLeft
                        text: qsTr("Address type")
                        font.pixelSize: 18
                    }
                    CoreText {
                        anchors.left: addressTypeLabel.right
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignLeft
                        text: root.request ? root.request.addressType : ""
                        font.pixelSize: 18
                        color: Theme.color.neutral9
                    }
                }

                Separator {
                    Layout.fillWidth: true
                    visible: optionsPopup.showAddressType && root.request !== null && root.request.addressType !== ""
                    color: Theme.color.neutral5
                }

                // Address
                Item {
                    Layout.fillWidth: true
                    visible: root.request !== null && root.request.address !== ""
                    implicitHeight: addressValueText.implicitHeight + 16

                    CoreText {
                        id: addressKeyText
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.topMargin: 8
                        width: 110
                        horizontalAlignment: Text.AlignLeft
                        text: qsTr("Address")
                        font.pixelSize: 18
                    }
                    CoreText {
                        id: addressValueText
                        anchors.left: addressKeyText.right
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 8
                        horizontalAlignment: Text.AlignLeft
                        text: root.request ? root.request.address : ""
                        font.pixelSize: 18
                        color: Theme.color.neutral9
                        wrapMode: Text.WrapAnywhere
                    }
                }

                Separator {
                    Layout.fillWidth: true
                    visible: root.request !== null && root.request.address !== ""
                    color: Theme.color.neutral5
                }

                // Amount
                Item {
                    Layout.fillWidth: true
                    visible: root.request !== null && root.request.amount.satoshi > 0
                    implicitHeight: amountKeyText.implicitHeight + 16

                    CoreText {
                        id: amountKeyText
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 110
                        horizontalAlignment: Text.AlignLeft
                        text: qsTr("Amount")
                        font.pixelSize: 18
                    }
                    CoreText {
                        anchors.left: amountKeyText.right
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignLeft
                        text: root.request ? root.request.amount.display + " " + root.request.amount.unitLabel : ""
                        font.pixelSize: 18
                        color: Theme.color.neutral9
                    }
                }

                Separator {
                    Layout.fillWidth: true
                    visible: root.request !== null && root.request.amount.satoshi > 0
                    color: Theme.color.neutral5
                }

                // Label
                Item {
                    Layout.fillWidth: true
                    visible: root.request !== null && root.request.label !== ""
                    implicitHeight: labelKeyText.implicitHeight + 16

                    CoreText {
                        id: labelKeyText
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 110
                        horizontalAlignment: Text.AlignLeft
                        text: qsTr("Label")
                        font.pixelSize: 18
                    }
                    CoreText {
                        anchors.left: labelKeyText.right
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignLeft
                        text: root.request ? root.request.label : ""
                        font.pixelSize: 18
                        color: Theme.color.neutral9
                        wrapMode: Text.WordWrap
                    }
                }

                Separator {
                    Layout.fillWidth: true
                    visible: root.request !== null && root.request.label !== ""
                    color: Theme.color.neutral5
                }

                // Message
                Item {
                    Layout.fillWidth: true
                    visible: root.request !== null && root.request.message !== ""
                    implicitHeight: messageKeyText.implicitHeight + 16

                    CoreText {
                        id: messageKeyText
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 110
                        horizontalAlignment: Text.AlignLeft
                        text: qsTr("Message")
                        font.pixelSize: 18
                    }
                    CoreText {
                        anchors.left: messageKeyText.right
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignLeft
                        text: root.request ? root.request.message : ""
                        font.pixelSize: 18
                        color: Theme.color.neutral9
                        wrapMode: Text.WordWrap
                    }
                }

                Separator {
                    Layout.fillWidth: true
                    visible: root.request !== null && root.request.message !== ""
                    color: Theme.color.neutral5
                }

                // Created
                Item {
                    Layout.fillWidth: true
                    visible: root.request !== null && root.request.createdIso !== ""
                    implicitHeight: createdKeyText.implicitHeight + 16

                    CoreText {
                        id: createdKeyText
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 110
                        horizontalAlignment: Text.AlignLeft
                        text: qsTr("Created")
                        font.pixelSize: 18
                    }
                    CoreText {
                        anchors.left: createdKeyText.right
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignLeft
                        text: root.request ? formatRelativeTime(root.request.createdIso) : ""
                        font.pixelSize: 18
                        color: Theme.color.neutral9
                    }
                }

                Separator {
                    Layout.fillWidth: true
                    visible: root.request !== null && root.request.createdIso !== ""
                    color: Theme.color.neutral5
                }

                // Bottom padding
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 20
                }
            }
        }
    }
}
