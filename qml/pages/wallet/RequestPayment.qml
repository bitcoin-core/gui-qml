// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

PageStack {
    id: root

    signal viewPreviousRequests()

    property WalletQmlModel wallet: walletController.selectedWallet
    property PaymentRequest request: wallet ? wallet.currentPaymentRequest : null
    property var receiveHistory: wallet ? wallet.receiveRequests : null

    Connections {
        target: walletController
        function onSelectedWalletChanged() {
            root.pop(null)
            if (root.request) {
                root.request.clear()
            }
        }
    }

    initialItem: Page {
        id: formPage
        objectName: "requestPaymentPage"
        background: null

        // Hidden mirror so the test bridge can poll the history count.
        Item {
            id: requestHistoryCount
            objectName: "requestHistoryCount"
            property int count: root.receiveHistory ? root.receiveHistory.count : 0
            visible: false
        }

        ScrollView {
            id: scrollView
            clip: true
            anchors.fill: parent
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            ColumnLayout {
                id: outerColumn
                width: scrollView.availableWidth
                spacing: 0

                ColumnLayout {
                    id: formColumn

                    enabled: walletController.initialized && root.request !== null

                    Layout.topMargin: 20
                    Layout.bottomMargin: 20
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.maximumWidth: 470
                    Layout.alignment: Qt.AlignHCenter

                    spacing: 10

                    // Title row — inside formColumn so it shares the same max-width
                    Item {
                        id: titleRow
                        Layout.fillWidth: true
                        Layout.bottomMargin: 20
                        Layout.preferredHeight: Math.max(title.implicitHeight, menuButton.implicitHeight)

                        CoreText {
                            id: title
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Request bitcoin")
                            font.pixelSize: 21
                            color: Theme.color.neutral9
                            bold: true
                        }

                        IconButton {
                            id: menuButton
                            objectName: "receiveOptionsButton"
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            checked: optionsPopup.opened
                            iconSource: "image://images/ellipsis"
                            onClicked: optionsPopup.open()
                        }

                        ReceiveOptionsPopup {
                            id: optionsPopup
                            x: menuButton.x - width + menuButton.width
                            y: menuButton.y + menuButton.height
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        implicitHeight: amountInput.height

                        CoreText {
                            id: amountLabel
                            width: 110
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            horizontalAlignment: Text.AlignLeft
                            text: qsTr("Amount")
                            font.family: "BitcoinCoreSans"
                            font.styleName: "Regular"
                            font.pixelSize: 18
                        }

                        TextField {
                            id: amountInput
                            objectName: "requestPaymentAmountInput"
                            anchors.left: amountLabel.right
                            anchors.right: unitFlipItem.left
                            anchors.verticalCenter: parent.verticalCenter
                            leftPadding: 0
                            font.family: "Inter"
                            font.styleName: "Regular"
                            font.pixelSize: 18
                            color: Theme.color.neutral9
                            placeholderTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
                            background: Item {}
                            placeholderText: !root.request || root.request.amount.unit === BitcoinAmount.BTC
                                ? "0.00000000" : "0"
                            selectByMouse: true
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
                            id: unitFlipItem
                            width: unitLabel.width + flipIcon.width
                            height: Math.max(unitLabel.height, flipIcon.height)
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
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

                    Connections {
                        target: root.request ? root.request.amount : null
                        function onDisplayChanged() {
                            amountInput.text = root.request ? root.request.amount.display : ""
                        }
                        function onAmountChanged() {
                            amountInput.text = root.request ? root.request.amount.display : ""
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
                        color: Theme.color.neutral5
                    }

                    LabeledTextInput {
                        id: labelInput
                        objectName: "requestPaymentYourNameInput"
                        Layout.fillWidth: true
                        labelText: qsTr("Label")
                        placeholderText: qsTr("Add a description...")
                        text: root.request ? root.request.label : ""
                        onTextEdited: {
                            if (root.request) {
                                root.request.label = labelInput.text
                            }
                        }
                    }

                    Connections {
                        target: root.request
                        function onLabelChanged() {
                            if (labelInput.text !== root.request.label) {
                                labelInput.text = root.request.label
                            }
                        }
                        function onMessageChanged() {
                            if (messageInput.text !== root.request.message) {
                                messageInput.text = root.request.message
                            }
                        }
                    }

                    Separator {
                        Layout.fillWidth: true
                        color: Theme.color.neutral5
                    }

                    LabeledTextInput {
                        id: messageInput
                        objectName: "requestPaymentMessageInput"
                        Layout.fillWidth: true
                        labelText: qsTr("Message")
                        placeholderText: qsTr("Enter message...")
                        text: root.request ? root.request.message : ""
                        onTextEdited: {
                            if (root.request) {
                                root.request.message = messageInput.text
                            }
                        }
                    }

                    ContinueButton {
                        id: createButton
                        objectName: "requestPaymentCreateButton"
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 30
                        Layout.preferredWidth: Math.min(implicitWidth + 80, formColumn.width)
                        text: qsTr("Create payment request")
                        enabled: root.request !== null && root.request.amount.satoshi > 0
                        onClicked: {
                            if (!root.request || !root.wallet) return
                            if (root.wallet.commitPaymentRequest()) {
                                root.push(detailPage)
                            }
                        }
                    }

                    ContinueButton {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 15
                        Layout.preferredWidth: Math.min(createButton.implicitWidth + 80, formColumn.width)
                        text: qsTr("View previous requests")
                        textColor: Theme.color.neutral9
                        backgroundColor: "transparent"
                        backgroundHoverColor: Theme.color.neutral2
                        backgroundPressedColor: Theme.color.neutral3
                        onClicked: root.viewPreviousRequests()
                    }
                }
            }
        }
    }

    Component {
        id: detailPage
        PaymentRequestDetail {
            onDone: root.pop()
        }
    }
}
