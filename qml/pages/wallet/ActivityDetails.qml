// Copyright (c) 2024-2025 The Bitcoin Core developers
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
    objectName: "activityDetailsPage"

    signal showTransaction(string txid)

    property string txid: ""
    property int outputIndex: -1
    property bool canBump: false
    property string replacedByTxid: ""
    property string message: ""
    property string amount: ""
    property string label: ""
    property string address: ""
    property string direction: ""
    property string date: ""
    property int depth: 0
    property int type: 0
    property int status: 0
    property bool countsForBalance: true
    property var paymentRequests: []
    readonly property int paymentRequestCount: root.paymentRequests ? root.paymentRequests.length : 0

    ActivityTransactionVisuals {
        id: transactionVisuals
        transactionType: root.type
        transactionStatus: root.status
        countsForBalance: root.countsForBalance
    }

    background: null

    function paymentRequestTitle(request) {
        if (request && request.label && request.label.length > 0) {
            return request.label
        }
        return qsTr("Payment request")
    }

    function paymentRequestSubtitle(request) {
        var amount = request && request.amountDisplay && request.amountDisplay.length > 0
            ? request.amountDisplay
            : qsTr("No amount")
        if (request && request.date && request.date.length > 0) {
            return qsTr("%1 - %2").arg(amount).arg(request.date)
        }
        return amount
    }

    function openPaymentRequestDetail(requestId) {
        if (!walletController.selectedWallet || !root.StackView.view || requestId.length === 0) {
            return
        }
        if (walletController.selectedWallet.loadPaymentRequestDetail(requestId)) {
            root.StackView.view.push(paymentRequestDetailPage)
        }
    }

    header: NavigationBar2 {
        id: navbar
        leftItem: NavButton {
            objectName: "activityDetailsBackButton"
            iconSource: "image://images/caret-left"
            text: qsTr("Back")
            onClicked: {
                root.StackView.view.pop()
            }
        }
        centerItem: Item {
            id: header
            Layout.fillWidth: true

            CoreText {
                anchors.left: parent.left
                text: qsTr("Transaction")
                font.pixelSize: 18
                bold: true
            }
        }
    }

    ScrollView {
        clip: true
        width: parent.width
        height: parent.height
        contentWidth: width

        ColumnLayout {
            id: columnLayout
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width, 450)
            opacity: root.replacedByTxid !== "" ? 0.4 : 1.0
            spacing: 0

            Rectangle {
                Layout.topMargin: 25
                Layout.bottomMargin: 25
                width: 60
                height: 60
                Layout.alignment: Qt.AlignHCenter
                radius: 30
                color: transactionVisuals.iconColor

                Icon {
                    anchors.centerIn: parent
                    source: transactionVisuals.iconSource
                    color: Theme.color.white
                    size: 30
                }
            }

            CoreText {
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 5
                text: root.amount
                color: transactionVisuals.amountColor
                font.family: optionsModel.moneyFont.family
                font.weight: optionsModel.moneyFont.weight
                font.pixelSize: 28
            }

            CoreText {
                Layout.alignment: Qt.AlignHCenter
                text: root.date
                color: Theme.color.neutral7
                font.pixelSize: 18
            }

            CoreText {
                objectName: "activityDetailsConfirmations"
                Layout.alignment: Qt.AlignHCenter
                Layout.bottomMargin: 10
                //: Confirmation state of a transaction that is not yet included in a block
                text: transactionVisuals.zeroConf ? qsTr("Pending confirmation")
                                                  //: Number of blocks confirming the transaction
                                                  : qsTr("%n confirmation(s)", "", root.depth)
                color: Theme.color.neutral7
                font.pixelSize: 18
            }

            Column {
                id: thirdPartyLinks
                objectName: "activityDetailsThirdPartyLinks"
                visible: root.txid.length > 0 && optionsModel.thirdPartyTransactionLinks(root.txid).length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                Layout.bottomMargin: visible ? 20 : 0
                spacing: 8

                Repeater {
                    model: optionsModel.thirdPartyTransactionLinks(root.txid)
                    delegate: ExternalLink {
                        required property var modelData
                        width: thirdPartyLinks.width
                        parentState: "FILLED"
                        description: qsTr("Show in %1").arg(modelData.host)
                        link: modelData.url
                    }
                }
            }

            LabeledTextInput {
                id: labelTextInput
                Layout.fillWidth: true
                Layout.bottomMargin: 20
                labelText: qsTr("Note to self")
                visible: root.label != ""
                enabled: false
                text: root.label
            }

            LabeledTextInput {
                id: messageTextInput
                Layout.fillWidth: true
                Layout.bottomMargin: 20
                labelText: qsTr("Message")
                visible: root.message != ""
                enabled: false
                text: root.message
            }

            AddressDetailRow {
                Layout.fillWidth: true
                Layout.bottomMargin: 20
                address: root.address
            }

            Column {
                id: paymentRequestsSection
                objectName: "activityDetailsPaymentRequestsSection"
                visible: root.paymentRequestCount > 0
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                Layout.topMargin: 10
                Layout.bottomMargin: 20
                spacing: 0

                Repeater {
                    model: root.paymentRequests

                    delegate: ItemDelegate {
                        id: paymentRequestDelegate
                        required property int index
                        required property var modelData

                        objectName: "activityDetailsPaymentRequest_" + paymentRequestDelegate.index
                        width: paymentRequestsSection.width
                        leftPadding: 0
                        rightPadding: 0
                        topPadding: 4
                        bottomPadding: 4
                        hoverEnabled: AppMode.isDesktop
                        background: Item {
                            Rectangle {
                                anchors.fill: parent
                                color: paymentRequestDelegate.hovered ? Theme.color.neutral1 : "transparent"
                                radius: 4
                            }
                        }
                        onClicked: root.openPaymentRequestDetail(paymentRequestDelegate.modelData.requestId
                            ? String(paymentRequestDelegate.modelData.requestId)
                            : "")

                        contentItem: RowLayout {
                            spacing: 10

                            Icon {
                                Layout.alignment: Qt.AlignVCenter
                                source: "qrc:/icons/triangle-down"
                                color: Theme.color.purple
                                size: 14
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2

                                CoreText {
                                    objectName: "activityDetailsPaymentRequestTitle_" + paymentRequestDelegate.index
                                    Layout.fillWidth: true
                                    text: root.paymentRequestTitle(paymentRequestDelegate.modelData)
                                    color: paymentRequestDelegate.hovered ? Theme.color.orange : Theme.color.neutral9
                                    font.pixelSize: 16
                                    elide: Text.ElideRight
                                }

                                CoreText {
                                    objectName: "activityDetailsPaymentRequestSubtitle_" + paymentRequestDelegate.index
                                    Layout.fillWidth: true
                                    text: root.paymentRequestSubtitle(paymentRequestDelegate.modelData)
                                    color: Theme.color.neutral7
                                    font.pixelSize: 14
                                    elide: Text.ElideRight
                                }
                            }

                            CaretRightIcon {
                                Layout.alignment: Qt.AlignVCenter
                                color: paymentRequestDelegate.hovered ? Theme.color.orange : Theme.color.neutral7
                            }
                        }
                    }
                }
            }

            InfoBanner {
                objectName: "speedUpBanner"
                visible: root.canBump
                Layout.alignment: Qt.AlignHCenter
                Layout.fillWidth: true
                Layout.maximumWidth: 600
                Layout.topMargin: 20
                bannerLayout: root.paymentRequestCount > 0 ? InfoBanner.Layout.Horizontal : InfoBanner.Layout.Vertical
                contentMargin: 18
                contentSpacing: 10
                message: qsTr("This transaction is still unconfirmed. You can speed it up by increasing the fee.")
                primaryButtonText: qsTr("Speed up")
                onPrimaryClicked: speedUpOverlay.open()
            }

        }
    }

    Component {
        id: paymentRequestDetailPage
        PaymentRequestDetail {}
    }

    InfoBanner {
        objectName: "replacedBanner"
        visible: root.replacedByTxid !== ""
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        anchors.bottomMargin: 30
        title: qsTr("This transaction was updated with a faster one")
        message: qsTr("You increased the fee while this transaction was still unconfirmed. Only the new one will confirm on-chain.")
        primaryButtonText: qsTr("View updated transaction")
        onPrimaryClicked: root.showTransaction(root.replacedByTxid)
    }

    SpeedUpOverlay {
        id: speedUpOverlay
        txid: root.txid
        anchors.centerIn: parent
        onBumpSucceeded: {
            var page = root.StackView.view.push("SendResult.qml", {
                resultType: SendResult.ResultType.SpeedUp,
                txid: speedUpOverlay.newTxid
            })
            page.done.connect(function() {
                if (walletController.selectedWallet) {
                    walletController.selectedWallet.activityListModel.reload()
                }
                root.StackView.view.pop(null)
            })
            page.viewNewTransaction.connect(function(txid) {
                if (walletController.selectedWallet) {
                    walletController.selectedWallet.activityListModel.reload()
                }
                root.StackView.view.pop(null)
                root.showTransaction(txid)
            })
        }
    }
}
