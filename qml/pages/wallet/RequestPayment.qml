// Copyright (c) 2024-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"
import "../settings"

Page {
    id: root
    objectName: "requestPaymentPage"
    background: null

    property WalletQmlModel wallet: walletController.selectedWallet
    property PaymentRequest request: wallet ? wallet.currentPaymentRequest : null
    property bool hasAddress: root.request !== null && root.request.address !== ""
    property bool hasAddressType: receiveOptionsPopup.showAddressType && hasAddress && root.request !== null && root.request.addressType !== ""

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

    Item {
        id: requestHistoryCount
        objectName: "requestHistoryCount"
        visible: false
        property int count: root.wallet ? root.wallet.receiveRequests.count : 0
    }

    ScrollView {
        clip: true
        width: parent.width
        height: parent.height
        contentWidth: width

        ColumnLayout {
            width: 450
            anchors.horizontalCenter: parent.horizontalCenter

            spacing: 5

            Item {
                id: titleRow
                Layout.fillWidth: true
                Layout.topMargin: 30
                Layout.bottomMargin: 20
                implicitHeight: Math.max(titleText.implicitHeight, receiveOptionsButton.implicitHeight)

                CoreText {
                    id: titleText
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Receive")
                    font.pixelSize: 21
                    bold: true
                }

                IconButton {
                    id: receiveOptionsButton
                    objectName: "receiveOptionsButton"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    checked: receiveOptionsPopup.opened
                    iconSource: "image://images/ellipsis"
                    Accessible.name: qsTr("Receive options")
                    onClicked: receiveOptionsPopup.open()
                }

                ReceiveOptionsPopup {
                    id: receiveOptionsPopup
                    x: receiveOptionsButton.x - width + receiveOptionsButton.width
                    y: receiveOptionsButton.y + receiveOptionsButton.height
                }
            }

            Item {
                Layout.fillWidth: true
                height: amountInput.height
                CoreText {
                    id: amountLabel
                    width: 110
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignLeft
                    text: qsTr("Amount")
                    font.pixelSize: 18
                }

                TextField {
                    id: amountInput
                    objectName: "requestPaymentAmountInput"
                    Accessible.name: qsTr("Payment amount")
                    anchors.left: amountLabel.right
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: 0
                    font.family: "Inter"
                    font.styleName: "Regular"
                    font.pixelSize: 18
                    color: Theme.color.neutral9
                    placeholderTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
                    background: Item {}
                    placeholderText: "0.00000000"
                    selectByMouse: true
                    enabled: root.request ? root.request.isEditing : true
                    text: root.request ? root.request.amount.display : ""
                    onTextEdited: {
                        if (root.request) {
                            root.request.amount.display = text
                        }
                    }
                    onEditingFinished: {
                        if (root.request) {
                            root.request.amount.format()
                        }
                    }
                    onActiveFocusChanged: {
                        if (!activeFocus && root.request) {
                            root.request.amount.format()
                        }
                    }
                    validator: RegularExpressionValidator {
                        regularExpression: !root.request || root.request.amount.unit === BitcoinAmount.BTC
                            ? /^(0|[1-9]\d{0,7})(\.\d{0,8})?$/
                            : /^(0|[1-9]\d{0,15})$/
                    }
                    maximumLength: !root.request || root.request.amount.unit === BitcoinAmount.BTC ? 17 : 16
                }
                Item {
                    width: unitLabel.width + flipIcon.width
                    height: Math.max(unitLabel.height, flipIcon.height)
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (root.request) {
                                root.request.amount.flipUnit()
                            }
                        }
                    }
                    CoreText {
                        id: unitLabel
                        anchors.right: flipIcon.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.request ? root.request.amount.unitLabel : ""
                        font.pixelSize: 18
                        color: enabled ? Theme.color.neutral7 : Theme.color.neutral4
                    }
                    Icon {
                        id: flipIcon
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        source: "image://images/flip-vertical"
                        color: unitLabel.enabled ? Theme.color.neutral8 : Theme.color.neutral4
                        size: 30
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.request !== null && root.request.amountError.length > 0

                Icon {
                    source: "image://images/alert-filled"
                    size: 22
                    color: Theme.color.red
                }

                CoreText {
                    text: root.request ? root.request.amountError : ""
                    font.pixelSize: 15
                    color: Theme.color.red
                    horizontalAlignment: Text.AlignLeft
                    Layout.fillWidth: true
                }
            }

            Separator {
                Layout.fillWidth: true
            }

            LabeledTextInput {
                id: nameInput
                objectName: "requestPaymentYourNameInput"
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showName
                labelText: qsTr("Name")
                placeholderText: qsTr("Enter name...")
                enabled: root.request ? root.request.isEditing : true
                text: root.request ? root.request.label : ""
                onTextEdited: {
                    if (root.request) {
                        root.request.label = nameInput.text
                    }
                }
            }

            Separator {
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showName && (receiveOptionsPopup.showMessage || receiveOptionsPopup.showNoteSelf || hasAddressType || hasAddress)
            }

            LabeledTextInput {
                id: messageInput
                objectName: "requestPaymentMessageInput"
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showMessage
                labelText: qsTr("Message")
                placeholderText: qsTr("Enter message...")
                enabled: root.request ? root.request.isEditing : true
                text: root.request ? root.request.message : ""
                onTextEdited: {
                    if (root.request) {
                        root.request.message = messageInput.text
                    }
                }
            }

            Separator {
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showMessage && (receiveOptionsPopup.showNoteSelf || hasAddressType || hasAddress)
            }

            LabeledTextInput {
                id: noteSelfInput
                objectName: "requestPaymentNoteSelfInput"
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showNoteSelf
                labelText: qsTr("Note to self")
                placeholderText: qsTr("Enter private note...")
                enabled: root.request ? root.request.isEditing : true
                text: root.request ? root.request.noteSelf : ""
                onTextEdited: {
                    if (root.request) {
                        root.request.noteSelf = noteSelfInput.text
                    }
                }
            }

            Separator {
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showNoteSelf && (hasAddressType || hasAddress)
            }

            Item {
                Layout.fillWidth: true
                visible: hasAddressType
                implicitHeight: addressTypeLabel.implicitHeight + 16
                height: implicitHeight

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
                visible: hasAddressType && hasAddress
            }

            Item {
                Layout.fillWidth: true
                visible: hasAddress
                Layout.topMargin: hasAddressType ? 0 : 10
                implicitHeight: addressLabel.height + copyLabel.height
                height: addressLabel.height + copyLabel.height

                CoreText {
                    id: addressLabel
                    anchors.left: parent.left
                    anchors.top: parent.top
                    horizontalAlignment: Text.AlignLeft
                    width: 110
                    text: qsTr("Address")
                    font.pixelSize: 18
                }
                CoreText {
                    id: copyLabel
                    anchors.left: parent.left
                    anchors.top: addressLabel.bottom
                    horizontalAlignment: Text.AlignLeft
                    width: 110
                    text: qsTr("Copy")
                    font.pixelSize: 18
                    color: Theme.color.orange
                }

                CoreText {
                    id: addressValue
                    anchors.left: addressLabel.right
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    horizontalAlignment: Text.AlignLeft
                    font.pixelSize: 18
                    wrapMode: Text.WordWrap
                    textFormat: Text.RichText
                    text: root.request ? root.formatAddressRichText(root.request.address) : ""
                }

                MouseArea {
                    anchors.left: parent.left
                    anchors.top: addressLabel.bottom
                    anchors.right: addressLabel.right
                    anchors.bottom: parent.bottom
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.request) {
                            Clipboard.setText(root.request.address)
                            copiedToast.show()
                        }
                    }
                }
            }

            Pane {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 20
                Layout.preferredWidth: 250
                Layout.preferredHeight: 250
                padding: 0
                background: Rectangle {
                    color: Theme.color.neutral2
                    visible: root.request ? root.request.isEditing : true
                }
                contentItem: Item {
                    QRImage {
                        id: qrImage
                        objectName: "requestPaymentQRCode"
                        anchors.fill: parent
                        visible: root.request ? !root.request.isEditing : false
                        backgroundColor: "transparent"
                        foregroundColor: Theme.color.neutral9
                        code: root.request ? root.request.qrPayload : ""
                    }
                }
            }

            OutlineButton {
                objectName: "requestPaymentCopyButton"
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 10
                Layout.preferredWidth: 120
                visible: root.request ? !root.request.isEditing : false
                text: qsTr("Copy")
                onClicked: {
                    if (root.request) {
                        Clipboard.setText(root.request.qrPayload)
                        copiedToast.show()
                    }
                }
            }

            CopiedToast {
                id: copiedToast
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 4
            }

            ContinueButton {
                id: generateButton
                objectName: "requestPaymentGenerateButton"
                Layout.fillWidth: true
                Layout.topMargin: 30
                text: {
                    if (!root.request || root.request.isEditing) {
                        return root.request && root.request.id !== ""
                            ? qsTr("Update payment request")
                            : qsTr("Generate payment request")
                    }
                    return qsTr("New request")
                }
                onClicked: {
                    if (!root.request) return
                    if (root.request.isEditing) {
                        root.wallet.commitPaymentRequest()
                    } else {
                        root.request.clear()
                    }
                }
            }

            OutlineButton {
                objectName: "requestPaymentCancelButton"
                Layout.fillWidth: true
                Layout.topMargin: 10
                visible: root.request ? root.request.isEditing && root.request.id !== "" : false
                text: qsTr("Cancel")
                onClicked: {
                    if (root.request) {
                        root.wallet.loadPaymentRequest(root.request.id)
                    }
                }
            }

            OutlineButton {
                objectName: "requestPaymentEditButton"
                Layout.fillWidth: true
                Layout.topMargin: 10
                visible: root.request ? !root.request.isEditing : false
                text: qsTr("Edit")
                onClicked: {
                    if (root.request) {
                        root.request.edit()
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 20
            }

            Connections {
                target: root.request
                function onIsEditingChanged() {
                    if (root.request) {
                        amountInput.text = root.request.amount.display
                        nameInput.text = root.request.label
                        messageInput.text = root.request.message
                        noteSelfInput.text = root.request.noteSelf
                    }
                }
            }

            Connections {
                target: walletController
                function onSelectedWalletChanged() {
                    if (root.request) {
                        root.request.clear()
                    }
                }
            }
        }
    }
}
