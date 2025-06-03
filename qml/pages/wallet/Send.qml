// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Qt.labs.settings 1.0
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"

PageStack {
    id: root
    vertical: true

    property WalletQmlModel wallet: walletController.selectedWallet
    property SendRecipient recipient: wallet.recipients.current

    signal transactionPrepared(bool multipleRecipientsEnabled)

    Connections {
        target: walletController
        function onSelectedWalletChanged() {
            root.pop()
        }
    }

    initialItem: Page {
        background: null

        Settings {
            id: settings
            property alias coinControlEnabled: sendOptionsPopup.coinControlEnabled
            property alias multipleRecipientsEnabled: sendOptionsPopup.multipleRecipientsEnabled
        }

        ScrollView {
            clip: true
            width: parent.width
            height: parent.height
            contentWidth: width

            ColumnLayout {
                id: columnLayout
                width: 450
                anchors.horizontalCenter: parent.horizontalCenter

                spacing: 10

                enabled: walletController.initialized

                Item {
                    id: titleRow
                    Layout.fillWidth: true
                    Layout.topMargin: 30
                    Layout.bottomMargin: 20

                    CoreText {
                        id: title
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Send bitcoin")
                        font.pixelSize: 21
                        color: Theme.color.neutral9
                        bold: true
                    }

                    EllipsisMenuButton {
                        id: menuButton
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        checked: sendOptionsPopup.opened
                        onClicked: {
                            sendOptionsPopup.open()
                        }
                    }

                    SendOptionsPopup {
                        id: sendOptionsPopup
                        x: menuButton.x - width + menuButton.width
                        y: menuButton.y + menuButton.height
                    }
                }

                RowLayout {
                    id: selectAndAddRecipients
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    Layout.bottomMargin: 10
                    visible: settings.multipleRecipientsEnabled

                    NavButton {
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        iconWidth: 30
                        iconHeight: 30
                        iconSource: "image://images/caret-left"
                        onClicked: {
                            wallet.recipients.prev()
                        }
                    }

                    NavButton {
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        iconWidth: 30
                        iconHeight: 30
                        iconSource: "image://images/caret-right"
                        onClicked: {
                            wallet.recipients.next()
                        }
                    }

                    CoreText {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignLeft
                        id: selectAndAddRecipientsLabel
                        text: qsTr("Recipient %1 of %2").arg(wallet.recipients.currentIndex).arg(wallet.recipients.count)
                        font.pixelSize: 18
                        color: Theme.color.neutral9
                    }

                    NavButton {
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        iconWidth: 20
                        iconHeight: 20
                        iconSource: "image://images/plus"
                        onClicked: {
                            wallet.recipients.add()
                        }
                    }
                    NavButton {
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        iconWidth: 20
                        iconHeight: 20
                        iconSource: "image://images/minus"
                        visible: wallet.recipients.count > 1
                        onClicked: {
                            wallet.recipients.remove()
                        }
                    }
                }

                Separator {
                    visible: settings.multipleRecipientsEnabled
                    Layout.fillWidth: true
                }

                LabeledTextInput {
                    id: address
                    Layout.fillWidth: true
                    labelText: qsTr("Send to")
                    placeholderText: qsTr("Enter address...")
                    text: root.recipient.address
                    onTextEdited: root.recipient.address = address.text
                }

                Separator {
                    Layout.fillWidth: true
                }

                Item {
                    BitcoinAmount {
                        id: bitcoinAmount
                    }

                    height: amountInput.height
                    Layout.fillWidth: true
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
                        onTextEdited: {
                            amountInput.text = bitcoinAmount.amount = bitcoinAmount.sanitize(amountInput.text)
                            root.recipient.amount = bitcoinAmount.satoshiAmount
                        }
                    }
                    Item {
                        width: unitLabel.width + flipIcon.width
                        height: Math.max(unitLabel.height, flipIcon.height)
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (bitcoinAmount.unit == BitcoinAmount.BTC) {
                                    amountInput.text = bitcoinAmount.convert(amountInput.text, BitcoinAmount.BTC)
                                    bitcoinAmount.unit = BitcoinAmount.SAT
                                } else {
                                    amountInput.text = bitcoinAmount.convert(amountInput.text, BitcoinAmount.SAT)
                                    bitcoinAmount.unit = BitcoinAmount.BTC
                                }
                            }
                        }
                        CoreText {
                            id: unitLabel
                            anchors.right: flipIcon.left
                            anchors.verticalCenter: parent.verticalCenter
                            text: bitcoinAmount.unitLabel
                            font.pixelSize: 18
                            color: enabled ? Theme.color.neutral7 : Theme.color.neutral4
                        }
                        Icon {
                            id: flipIcon
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            source: "image://images/flip-vertical"
                            icon.color: unitLabel.enabled ? Theme.color.neutral8 : Theme.color.neutral4
                            size: 30
                        }
                    }
                }

                Separator {
                    Layout.fillWidth: true
                }

                LabeledTextInput {
                    id: label
                    Layout.fillWidth: true
                    labelText: qsTr("Note to self")
                    placeholderText: qsTr("Enter ...")
                    onTextEdited: root.recipient.label = label.text
                }

                Separator {
                    Layout.fillWidth: true
                }

                LabeledCoinControlButton {
                    visible: settings.coinControlEnabled
                    Layout.fillWidth: true
                    coinsSelected: wallet.coinsListModel.selectedCoinsCount
                    coinCount: wallet.coinsListModel.coinCount
                    onOpenCoinControl: {
                        root.wallet.coinsListModel.update()
                        root.push(coinSelectionPage)
                    }
                }

                Separator {
                    visible: settings.coinControlEnabled
                    Layout.fillWidth: true
                }

                Item {
                    height: feeLabel.height + feeValue.height
                    Layout.fillWidth: true
                    CoreText {
                        id: feeLabel
                        anchors.left: parent.left
                        anchors.top: parent.top
                        text: "Fee"
                        font.pixelSize: 15
                    }

                    CoreText {
                        id: feeValue
                        anchors.right: parent.right
                        anchors.top: parent.top
                        text: qsTr("Default (~2,000 sats)")
                        font.pixelSize: 15
                    }
                }

                ContinueButton {
                    id: continueButton
                    Layout.fillWidth: true
                    Layout.topMargin: 30
                    text: qsTr("Review")
                    onClicked: {
                        if (root.wallet.prepareTransaction()) {
                            root.transactionPrepared(settings.multipleRecipientsEnabled);
                        }
                    }
                }
            }
        }
    }

    Component {
        id: coinSelectionPage
        CoinSelection {
            onDone: root.pop()
        }
    }
}
