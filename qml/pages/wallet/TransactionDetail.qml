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
    objectName: "activityDetailsPage"
    property var wallet: walletController.selectedWallet
    property var transactionData: ({})
    property int outputIndex: -1
    property bool detailsExpanded: false
    onTxidChanged: detailsExpanded = false
    readonly property string txid: transactionData.txid || ""
    readonly property bool detailsLoading: !!transactionData.detailsLoading
    readonly property string detailsError: transactionData.detailsError || ""
    readonly property string amount: transactionData.amount || ""
    readonly property int depth: transactionData.depth || 0
    readonly property bool statusKnown: !!transactionData.statusKnown
    readonly property bool inactive: !!transactionData.isInactive
    readonly property bool incoming: transactionData.netAmountSat > 0
    readonly property string replacedByTxid: inactive && depth <= 0 ? transactionData.replacedByTxid || "" : ""
    readonly property bool canBump: statusKnown && depth === 0 && !inactive && !!transactionData.canBump
    readonly property var flow: transactionData.flow || ({})
    readonly property string inactiveLabel: replacedByTxid ? qsTr("Replaced")
        : transactionData.status === Transaction.Abandoned ? qsTr("Cancelled")
        : transactionData.status === Transaction.Conflicted ? qsTr("Conflicted")
        : transactionData.status === Transaction.NotAccepted ? qsTr("Not accepted") : ""
    readonly property string actionLabel: {
        switch (transactionData.activityType) {
        case TransactionActivityModel.Send: return qsTr("Sent")
        case TransactionActivityModel.Receive: return qsTr("Received")
        case TransactionActivityModel.Multiple: return qsTr("Multiple actions")
        case TransactionActivityModel.Consolidation: return qsTr("Consolidation")
        case TransactionActivityModel.Split: return qsTr("Split")
        case TransactionActivityModel.InternalTransfer: return qsTr("Sent to yourself")
        case TransactionActivityModel.Mined: return qsTr("Mining reward")
        default: return qsTr("Transaction")
        }
    }
    readonly property real sidePadding: width < 640 ? 16 : 40
    readonly property real detailContentWidth: Math.max(0, Math.min(1100, width - sidePadding * 2))
    signal showTransaction(string txid)

    background: Rectangle { color: Theme.color.neutral0 }
    padding: 0
    title: qsTr("Transaction")

    header: NavigationBar2 {
        objectName: "transactionDetailNavigation"
        leftPadding: (root.width - root.detailContentWidth) / 2
        rightPadding: leftPadding
        topPadding: 24
        bottomPadding: 4
        leftItem: NavButton {
            objectName: "activityDetailsBackButton"
            text: root.width < 640 ? "" : qsTr("Activity")
            Accessible.name: qsTr("Back to Activity")
            iconSource: "image://images/caret-left"
            onClicked: if (root.StackView.view) root.StackView.view.pop()
        }
        centerItem: CoreText {
            objectName: "transactionDetailTitle"
            text: root.title
            font: Theme.text.heading.font
            wrap: false
        }
        rightItem: OverflowMenuButton {
            id: moreButton
            objectName: "transactionDetailMoreButton"
            onClicked: moreMenu.open()
            ContextMenu {
                id: moreMenu
                objectName: "transactionDetailMoreMenu"
                x: moreButton.width - width
                y: moreButton.height + 8
                ContextMenuButton {
                    objectName: "transactionCopyIdMenuButton"
                    text: qsTr("Copy transaction ID")
                    iconSource: "qrc:/icons/copy"
                    onTriggered: Clipboard.setText(root.txid)
                }
                Repeater {
                    model: optionsModel.thirdPartyTransactionLinks(root.txid)
                    delegate: ContextMenuButton {
                        required property var modelData
                        text: qsTr("Show in %1").arg(modelData.host)
                        iconSource: "image://images/export"
                        onTriggered: Qt.openUrlExternally(modelData.url)
                    }
                }
            }
        }
    }

    function openPaymentRequestDetail(requestId) {
        if (root.wallet && root.StackView.view && requestId && root.wallet.loadPaymentRequestDetail(requestId)) {
            root.StackView.view.push("PaymentRequestDetail.qml")
        }
    }

    ScrollView {
        id: scroll
        objectName: "transactionDetailScroll"
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        contentHeight: content.implicitHeight + 64
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        Item {
            width: scroll.availableWidth
            height: scroll.contentHeight
            ColumnLayout {
                id: content
                x: (parent.width - width) / 2
                y: 24
                width: root.detailContentWidth
                spacing: 28

                TransactionSummary {
                    visible: root.amount.length > 0
                    Layout.fillWidth: true
                    Layout.bottomMargin: 8
                    amount: root.amount
                    amountColor: root.inactive ? Theme.color.neutral7 : root.incoming ? Theme.color.green : Theme.color.neutral9
                    subtitle: root.actionLabel + (root.transactionData.timestamp
                        ? " · " + Qt.formatDateTime(new Date(root.transactionData.timestamp * 1000), "MMM d, h:mm AP") : "")
                    iconComponent: ActivityIcon {
                        objectName: "transactionDetailIcon"
                        iconSize: 48
                        activityType: root.transactionData.activityType === undefined ? TransactionActivityModel.Other : root.transactionData.activityType
                        incoming: root.incoming
                        pending: root.statusKnown && root.depth === 0
                        inactive: root.inactive
                    }
                    statusComponent: RowLayout {
                        spacing: 8
                        ConfirmationPill {
                            objectName: "transactionConfirmationPill"
                            confirmations: root.depth
                            statusKnown: root.statusKnown
                            inactive: root.inactive
                        }
                        Rectangle {
                            visible: root.inactiveLabel.length > 0
                            implicitWidth: inactiveText.implicitWidth + 24
                            implicitHeight: 30
                            radius: 15
                            color: Theme.color.neutral2
                            CoreText {
                                id: inactiveText
                                anchors.centerIn: parent
                                text: root.inactiveLabel
                                color: Theme.color.neutral7
                                font: Theme.text.captionStrong.font
                            }
                        }
                    }
                }

                BusyIndicator {
                    objectName: "transactionDetailsLoading"
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.detailsLoading
                    running: visible
                    Accessible.name: qsTr("Loading transaction details")
                }
                InfoBanner {
                    objectName: "transactionDetailsError"
                    Layout.fillWidth: true
                    visible: root.detailsError.length > 0
                    message: root.detailsError
                    primaryButtonText: qsTr("Try again")
                    onPrimaryClicked: if (root.wallet) root.wallet.transactionActivityModel.requestTransactionDetails(root.txid)
                }
                InfoBanner {
                    objectName: "speedUpBanner"
                    visible: root.canBump
                    Layout.fillWidth: true
                    backgroundColor: Theme.color.neutral1
                    radius: 16
                    contentMargin: 20
                    bannerLayout: width < 640 ? InfoBanner.Layout.Vertical : InfoBanner.Layout.Horizontal
                    messageFontPixelSize: Theme.text.description.font.pixelSize
                    messageLineHeight: Theme.text.description.lineHeight
                    message: qsTr("This transaction is still unconfirmed. You can speed it up by increasing the fee")
                    primaryButtonText: qsTr("Speed up")
                    onPrimaryClicked: speedUpOverlay.open()
                }
                InfoBanner {
                    objectName: "replacedBanner"
                    visible: root.replacedByTxid.length > 0
                    Layout.fillWidth: true
                    backgroundColor: Theme.color.neutral1
                    radius: 16
                    contentMargin: 20
                    bannerLayout: width < 640 ? InfoBanner.Layout.Vertical : InfoBanner.Layout.Horizontal
                    title: qsTr("Replaced by a newer transaction")
                    message: root.replacedByTxid.substring(0, 12) + "…" + root.replacedByTxid.slice(-12)
                    primaryButtonText: qsTr("View replacement")
                    primaryButtonOutlined: true
                    onPrimaryClicked: root.showTransaction(root.replacedByTxid)
                }
                InfoBanner {
                    objectName: "transactionMaturityBanner"
                    visible: !root.inactive && root.transactionData.blocksToMaturity > 0
                    Layout.fillWidth: true
                    backgroundColor: Theme.color.neutral1
                    contentMargin: 20
                    message: qsTr("This mining reward will be spendable after %1 more blocks.").arg(root.transactionData.blocksToMaturity || 0)
                }

                TransactionFlow {
                    objectName: "transactionDetailFlow"
                    Layout.fillWidth: true
                    visible: !!root.flow.inputs && !!root.flow.outputs
                    flow: root.flow
                    transactionId: root.txid
                    displayUnit: optionsModel.displayUnit
                    walletName: root.wallet && root.wallet.name ? root.wallet.name : qsTr("Your wallet")
                    selectedEntryId: root.outputIndex >= 0 ? "output:" + root.outputIndex : ""
                    onPaymentRequestRequested: function(requestId) { root.openPaymentRequestDetail(requestId) }
                }
                DropdownButton {
                    objectName: "transactionDetailsToggle"
                    Layout.alignment: Qt.AlignHCenter
                    text: root.detailsExpanded ? qsTr("Hide details") : qsTr("Show details")
                    opened: root.detailsExpanded
                    onClicked: root.detailsExpanded = !root.detailsExpanded
                }
                TransactionDetailsSection {
                    visible: root.detailsExpanded
                    Layout.fillWidth: true
                    details: root.transactionData
                }
            }
        }
    }

    SpeedUpOverlay {
        id: speedUpOverlay
        txid: root.txid
        bumpModel: root.wallet ? root.wallet.bumpModel : null
        onBumpSucceeded: {
            const stack = root.StackView.view
            if (!stack) return
            const page = stack.push("SendResult.qml", {
                resultType: SendResult.ResultType.SpeedUp,
                txid: speedUpOverlay.newTxid
            })
            page.done.connect(function() {
                if (root.wallet) root.wallet.transactionActivityModel.reload()
                stack.pop(null)
            })
            page.viewNewTransaction.connect(function(txid) {
                if (root.wallet) root.wallet.transactionActivityModel.reload()
                root.showTransaction(txid)
            })
        }
    }
}
