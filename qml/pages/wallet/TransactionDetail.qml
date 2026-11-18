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
    property WalletQmlModel wallet: null
    readonly property bool actionContextAvailable: root.visible && root.wallet !== null && root.wallet.available
    onActionContextAvailableChanged: if (!actionContextAvailable) root.retireDialogs()
    property string transactionId: ""
    ActivityDetailController {
        id: detailView
        source: root.wallet ? root.wallet.transactionActivityModel : null
        transactionId: root.transactionId
        active: root.visible && root.wallet !== null && root.wallet.available
    }
    readonly property ActivityDetailModel activityModel: detailView.detail
    property var transactionData: activityModel && activityModel.selectedTransactionDetails.txid === transactionId
        ? activityModel.selectedTransactionDetails : ({txid: transactionId, detailsLoading: true})
    property int outputIndex: -1
    property bool walletViewActive: true
    property bool detailsExpanded: false
    onTxidChanged: { detailsExpanded = false; root.retireDialogs() }
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
        : transactionData.status === Transaction.NotAccepted ? qsTr("Not accepted")
        : inactive ? qsTr("Not in mempool") : ""
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
    readonly property real sidePadding: SizeClass.isCompact(width) ? Theme.space.pageCompact : Theme.space.pageRegular
    readonly property real detailContentWidth: Math.max(0, Math.min(1100, width - sidePadding * 2))
    signal showTransaction(string txid)

    background: Rectangle { color: Theme.color.background }
    padding: 0
    title: qsTr("Transaction")

    header: NavigationBar2 {
        objectName: "transactionDetailNavigation"
        leftPadding: (root.width - root.detailContentWidth) / 2
        rightPadding: leftPadding
        topPadding: 24
        bottomPadding: 4
        showBackButton: true
        backButtonObjectName: "activityDetailsBackButton"
        backButtonText: SizeClass.isCompact(root.width) ? "" : qsTr("Activity")
        backButtonAccessibleName: qsTr("Back to Activity")
        onBackClicked: if (root.StackView.view) root.StackView.view.pop()
        title: root.title
        titleObjectName: "transactionDetailTitle"
        rightItem: OverflowMenuButton {
            id: moreButton
            objectName: "transactionDetailMoreButton"
            checked: moreMenu.opened
            onClicked: moreMenu.openBelow(moreButton, Theme.space.m)
            TransactionActionsMenu {
                id: moreMenu
                objectName: "transactionDetailMoreMenu"
                txid: root.txid
                showsCopyRawTransaction: true
                copyRawTransactionEnabled: root.activityModel !== null && !!root.transactionData.rawAvailable
                onCopyRawTransactionRequested: root.activityModel.copyRawTransaction()
                onExplorerLinkRequested: function(url) { root.openExternalLink(url) }
            }
        }
    }

    function openPaymentRequestDetail(requestId) {
        if (requestId) paymentRequestModal.openRequest(requestId)
    }

    ReceiveRequestModal {
        id: paymentRequestModal
        objectName: "transactionPaymentRequestModal"
        wallet: root.wallet
        contextActive: root.walletViewActive
    }

    function openExternalLink(link) {
        externalLinkPopup.link = link
        externalLinkPopup.open()
    }

    ExternalPopup {
        id: externalLinkPopup
        objectName: "transactionExternalLinkPopup"
        parent: Overlay.overlay
    }

    ScrollView {
        id: scroll
        objectName: "transactionDetailScroll"
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        contentHeight: content.height + 64
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
                    hidesAmount: true
                    amountColor: root.inactive ? Theme.color.neutral7 : root.incoming ? Theme.color.txIncoming : Theme.color.neutral9
                    subtitle: root.actionLabel + (root.transactionData.timestamp
                        ? " · " + new Date(root.transactionData.timestamp * 1000).toLocaleString(Qt.locale(), Locale.ShortFormat) : "")
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
                        StatusPill {
                            objectName: "transactionConfirmationPill"
                            readonly property bool neutralStatus: !root.statusKnown || root.inactive || root.depth <= 0
                            accentColor: neutralStatus ? Theme.color.neutral7
                                : root.depth < 6 ? Theme.color.warning : Theme.color.success
                            backgroundColor: neutralStatus ? Theme.color.surface2 : Theme.color.tint(accentColor)
                            text: !root.statusKnown ? qsTr("Status unavailable")
                                : !root.inactive && root.depth === 0 ? qsTr("Unconfirmed")
                                : root.depth === 1 ? qsTr("1 confirmation") : qsTr("%1 confirmations").arg(Math.max(0, root.depth))
                        }
                        Rectangle {
                            visible: root.inactiveLabel.length > 0
                            implicitWidth: inactiveText.implicitWidth + 24
                            implicitHeight: 30
                            radius: height / 2
                            color: Theme.color.surface2
                            CoreText {
                                id: inactiveText
                                anchors.centerIn: parent
                                text: root.inactiveLabel
                                color: Theme.color.neutral7
                                textStyle: Theme.text.captionStrong
                            }
                        }
                    }
                }

                SpinningIndicator {
                    objectName: "transactionDetailsLoading"
                    size: 32
                    color: Theme.color.neutral7
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.detailsLoading
                    running: visible
                    Accessible.name: qsTr("Loading transaction details")
                }
                ToastBanner {
                    objectName: "transactionDetailsError"
                    Layout.fillWidth: true
                    shown: root.detailsError.length > 0
                    tone: ToastBanner.Error
                    text: root.detailsError
                    actionText: qsTr("Try again")
                    onActionTriggered: if (root.activityModel) root.activityModel.reload()
                }
                InfoBanner {
                    objectName: "speedUpBanner"
                    visible: root.canBump
                    Layout.fillWidth: true
                    bannerLayout: SizeClass.isCompact(width) ? InfoBanner.Layout.Vertical : InfoBanner.Layout.Horizontal
                    message: qsTr("This transaction is still unconfirmed. You can speed it up by increasing the fee.")
                    primaryButtonText: qsTr("Speed up")
                    onPrimaryClicked: speedUpOverlay.open()
                }
                InfoBanner {
                    objectName: "replacedBanner"
                    visible: root.replacedByTxid.length > 0
                    Layout.fillWidth: true
                    bannerLayout: SizeClass.isCompact(width) ? InfoBanner.Layout.Vertical : InfoBanner.Layout.Horizontal
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
                    message: qsTr("This mining reward will be spendable after %1 more blocks.").arg(root.transactionData.blocksToMaturity || 0)
                }

                TransactionFlow {
                    objectName: "transactionDetailFlow"
                    Layout.fillWidth: true
                    visible: !!root.flow.inputs && !!root.flow.outputs
                    flow: root.flow
                    transactionId: root.txid
                    displayUnit: ApplicationModels.optionsModel.displayUnit
                    hidesAmounts: true
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
                TransactionEntriesList {
                    objectName: "transactionAllInputs"
                    visible: root.detailsExpanded
                    Layout.fillWidth: true
                    title: qsTr("All inputs")
                    input: true
                    entryModel: root.activityModel ? root.activityModel.detailInputs : null
                    walletName: root.wallet && root.wallet.name ? root.wallet.name : qsTr("Your wallet")
                    onEntryRequested: (row) => root.openEntry("inputs", row)
                    onPaymentRequestRequested: (requestId) => root.openPaymentRequestDetail(requestId)
                }
                TransactionEntriesList {
                    objectName: "transactionAllOutputs"
                    visible: root.detailsExpanded
                    Layout.fillWidth: true
                    title: qsTr("All outputs")
                    entryModel: root.activityModel ? root.activityModel.detailOutputs : null
                    walletName: root.wallet && root.wallet.name ? root.wallet.name : qsTr("Your wallet")
                    onEntryRequested: (row) => root.openEntry("outputs", row)
                    onPaymentRequestRequested: (requestId) => root.openPaymentRequestDetail(requestId)
                }
                TransactionEntriesList {
                    objectName: "transactionAllActions"
                    visible: root.detailsExpanded
                    Layout.fillWidth: true
                    title: qsTr("All actions")
                    entryModel: root.activityModel ? root.activityModel.detailActions : null
                    walletName: root.wallet && root.wallet.name ? root.wallet.name : qsTr("Your wallet")
                    onEntryRequested: (row) => root.openEntry("actions", row)
                    onPaymentRequestRequested: (requestId) => root.openPaymentRequestDetail(requestId)
                }
                TransactionDetailsSection {
                    visible: root.detailsExpanded
                    Layout.fillWidth: true
                    hidesAmounts: true
                    details: root.transactionData
                    activityModel: root.activityModel
                    enabled: !!root.wallet && root.wallet.available
                }
            }
        }
    }

    function openEntry(side, row) {
        if (!root.wallet || !root.wallet.available || !root.activityModel) return
        if (root.activityModel.requestEntryDetails(side, row)) entryPopup.open()
    }
    TransactionEntryPopup {
        id: entryPopup
        objectName: "transactionEntryPopup"
        documentObjectName: "transactionEntryDocument"
        loading: !!root.activityModel && root.activityModel.entryDetailsBusy
        errorText: root.activityModel ? root.activityModel.entryDetailsError : ""
        document: root.activityModel ? root.activityModel.entryDocument : null
    }
    function retireDialogs() {
        if (entryPopup) entryPopup.close()
        if (speedUpOverlay) speedUpOverlay.close()
        if (speedUpComplete) speedUpComplete.close()
    }
    onWalletChanged: root.retireDialogs()

    SpeedUpOverlay {
        id: speedUpOverlay
        parent: Overlay.overlay
        txid: root.txid
        wallet: root.wallet
        onBumpSucceeded: function(newTxid, walletRecordState, walletSubmissionState) {
            if (!root.actionContextAvailable) return
            speedUpComplete.txid = newTxid
            speedUpComplete.walletRecordState = walletRecordState
            speedUpComplete.walletSubmissionState = walletSubmissionState
            speedUpComplete.open()
        }
        onViewNewTransaction: function(txid) {
            if (root.actionContextAvailable) root.showTransaction(txid)
        }
    }

    SpeedUpComplete {
        id: speedUpComplete
        parent: Overlay.overlay
        // Another wallet or a hidden page closes this (retireDialogs).
        onDone: close()
        onViewNewTransaction: function(txid) {
            close()
            if (root.actionContextAvailable) root.showTransaction(txid)
        }
    }
}
