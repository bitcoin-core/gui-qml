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
    objectName: "sendTransactionReviewPage"
    background: Rectangle { color: Theme.color.background }
    title: qsTr("Review transaction")

    property WalletModel wallet: null
    readonly property TransactionReviewModel transaction: wallet ? wallet.currentTransaction : null
    readonly property var flowDetails: wallet ? wallet.currentTransactionFlowDetails : null
    readonly property bool detailsContextValid: wallet !== null && wallet.available && transaction !== null
        && flowDetails !== null && flowDetails.context === transaction
    readonly property bool detailsReady: detailsContextValid && !flowDetails.pending && flowDetails.error.length === 0
    readonly property TransactionReviewInterface reviewInterface: sendController.reviewInterface
    readonly property bool importedPsbt: reviewInterface && reviewInterface.currentTransactionIsImportedPsbt === true
    readonly property bool canSend: reviewInterface && reviewInterface.currentTransactionCanSend
        && !reviewInterface.currentTransactionNeedsRevalidation
    readonly property bool feePending: reviewInterface && reviewInterface.transactionFeePending
    readonly property bool canBroadcast: reviewInterface && reviewInterface.currentTransactionCanBroadcast
    readonly property bool showBroadcast: importedPsbt && canBroadcast
    readonly property bool showSend: !showBroadcast && canSend
    readonly property string reviewMessage: reviewInterface && reviewInterface.currentTransactionNeedsRevalidation
        ? qsTr("The draft changed after review. Prepare it again before sending.")
        : reviewInterface ? reviewInterface.currentTransactionReviewMessage : ""
    readonly property bool showWarning: (reviewInterface && reviewInterface.currentTransactionNeedsRevalidation)
        || reviewMessage.length > 0 || (wallet && wallet.keySchemeKind === WalletModel.WatchOnly && !canBroadcast)
    readonly property real sidePadding: SizeClass.isCompact(width) ? Theme.space.pageCompact : Theme.space.pageRegular
    readonly property real reviewContentWidth: Math.max(0, Math.min(1100, width - sidePadding * 2))
    readonly property bool sendBusy: wallet !== null && (wallet.sendBusy || wallet.psbtActionBusy)
    property bool sending: false
    readonly property bool savingPsbt: sendController.psbtKind === PsbtOperation.Save
    readonly property string saveStatus: !savingPsbt ? ""
        : sendController.psbtState === PsbtOperation.Saved ? qsTr("Saved.") : sendController.psbtError
    readonly property bool saveError: savingPsbt && sendController.psbtState !== PsbtOperation.Saved && saveStatus.length > 0
    readonly property bool broadcastSubmitted: sendController.psbtKind === PsbtOperation.Broadcast
        && sendController.psbtState === PsbtOperation.Submitted
    onBroadcastSubmittedChanged: {
        if (!broadcastSubmitted || sending) return
        sending = true
        transactionSent(sendController.psbtTxid, WalletModel.SubmissionAccepted, WalletModel.SendRecordUnknown)
    }

    // Sends and signs the transaction on this page. Hiding the page, another
    // review or another wallet cancels its prompts.
    TransactionReviewController {
        id: sendController
        objectName: "sendTransactionReviewController"
        reviewInterface: root.wallet ? root.wallet.transactionReview : null
        active: root.visible
    }
    readonly property bool sendCommitted: sendController.sendState === SendOperation.Committed
    onSendCommittedChanged: {
        if (!sendCommitted || sending) return
        sending = true
        transactionSent(sendController.txid, sendController.submission, sendController.record)
    }

    onVisibleChanged: {
        if (!visible) {
            reviewEntryPopup.close()
            sending = false
        }
    }

    signal back()
    signal transactionSent(string txid, int submissionState, int recordState)

    // The review is a full-window sheet: Escape goes back like its close button.
    Keys.onEscapePressed: function(event) {
        if (!root.sending && sendController.canDismiss) root.back()
        event.accepted = true
    }

    function focusInitialControl() {
        reviewHeader.closeButton.forceActiveFocus(Qt.TabFocusReason)
    }

    Component {
        id: readyStatus
        StatusPill {
            objectName: "sendTransactionReviewReadyPill"
            text: root.showBroadcast ? qsTr("Ready to broadcast") : qsTr("Ready to send")
            accentColor: Theme.color.accent
        }
    }

    onTransactionChanged: reviewEntryPopup.close()
    onDetailsContextValidChanged: if (!detailsContextValid) reviewEntryPopup.close()

    function commitSend() {
        if (sending || sendBusy || !wallet || !detailsReady) return
        sendController.send()
    }

    function signOnly() {
        if (sending || sendBusy || !reviewInterface || !detailsReady || !reviewInterface.currentTransactionCanSignOnly) return
        sendController.signOnlyTransaction()
    }


    function commitBroadcast() {
        if (sending || sendBusy || !wallet || !detailsReady || wallet.psbtActionBusy) return
        sendController.broadcast()
    }

    function defaultSavePsbtFileUrl() {
        return "file://" + ApplicationModels.walletController.names.homePath() + "/transaction-"
            + Qt.formatDateTime(new Date(), "yyyy-MM-dd-HHmm") + ".psbt"
    }

    // The automation path skips the native dialog.
    function savePsbt(path, signerResult) {
        if (!String(path).length) return
        sendController.beginPsbtSave(!!signerResult)
        sendController.choosePsbtSaveFile(String(path))
    }

    function startSavePsbt() {
        if (savePsbtAutomationPath.text.length) {
            const path = savePsbtAutomationPath.text
            savePsbtAutomationPath.text = ""
            savePsbt(path)
        } else {
            savePsbtDialog.currentFile = defaultSavePsbtFileUrl()
            sendController.beginPsbtSave(false)
            savePsbtDialog.open()
        }
    }

    function startSaveUnverifiedSignerResult() {
        if (!sendController.signerResultAvailable) return
        if (savePsbtAutomationPath.text.length) {
            const path = savePsbtAutomationPath.text
            savePsbtAutomationPath.text = ""
            savePsbt(path, true)
        } else {
            savePsbtDialog.currentFile = defaultSavePsbtFileUrl()
            sendController.beginPsbtSave(true)
            savePsbtDialog.open()
        }
    }

    header: NavigationBar2 {
        id: reviewHeader
        implicitWidth: 0
        leftPadding: (root.width - root.reviewContentWidth) / 2
        rightPadding: leftPadding
        topPadding: 16
        bottomPadding: 12
        title: root.title
        titleObjectName: "sendTransactionReviewTitle"
        showCloseButton: true
        closeEnabled: sendController.canDismiss
        closeButtonObjectName: "sendTransactionReviewCloseButton"
        closeButtonSize: CloseButton.Large
        onCloseClicked: root.back()
    }

    AppFileDialog {
        id: savePsbtDialog
        objectName: "sendTransactionReviewSaveDialog"
        title: sendController.saveSignerResult ? qsTr("Export unverified signer result") : qsTr("Save transaction as PSBT")
        fileMode: AppFileDialog.SaveFile
        currentFolder: "file://" + ApplicationModels.walletController.names.homePath()
        defaultSuffix: "psbt"
        nameFilters: [qsTr("Partially Signed Bitcoin Transactions (*.psbt)"), qsTr("All files (*)")]
        // The controller knows which save this answers; another review or
        // wallet cancels it.
        onAccepted: sendController.choosePsbtSaveFile(selectedFile.toString())
        onRejected: sendController.cancelPsbtSave()
    }

    Instantiator {
        model: sendController.fileExportOperation ? [sendController.fileExportOperation] : []
        delegate: SaveConfirmationPopup {
            required property FileSaveOperation modelData
            objectName: "psbtExportOverwritePopup"
            // Keeps the overlay when the page leaves the window. A popup whose parent
            // goes null there loses its window while its item is still in the overlay,
            // and a hover event then crashes Qt 6.8.4 and newer.
            Binding on parent {
                when: root.Overlay.overlay !== null
                value: root.Overlay.overlay
                restoreMode: Binding.RestoreNone
            }
            operation: modelData
            active: root.visible
            saveTitle: qsTr("Save PSBT file?")
            replaceTitle: qsTr("Replace PSBT file?")
            pathObjectName: "psbtExportFinalPathText"
            approveObjectName: "psbtExportApproveButton"
            refuseObjectName: "psbtExportRejectButton"
        }
    }

    Item {
        id: savePsbtAutomationPath
        objectName: "sendTransactionReviewSavePsbtPathField"
        property string text: ""
        width: 0
        height: 0
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            id: content
            x: (scroll.availableWidth - width) / 2
            width: root.reviewContentWidth
            spacing: 24

            Item { Layout.preferredHeight: 8 }

            TransactionSummary {
                objectName: "sendTransactionReviewSummary"
                Layout.fillWidth: true
                amount: root.transaction ? root.transaction.total : ""
                statusComponent: root.showSend || root.showBroadcast ? readyStatus : null
                iconComponent: ActivityIcon {
                    objectName: "sendTransactionReviewIcon"
                    activityType: TransactionActivityModel.Send
                    incoming: false
                    pending: true
                    iconSize: 48
                }
            }

            InfoBanner {
                objectName: "sendTransactionReviewWarning"
                visible: root.showWarning
                Layout.fillWidth: true
                message: root.reviewMessage.length ? root.reviewMessage
                    : qsTr("This wallet does not have the keys to sign this transaction.")
            }

            FormSection {
                objectName: "sendTransactionReviewRecipients"
                Layout.fillWidth: true
                title: root.importedPsbt ? qsTr("Outputs") : qsTr("Recipients")
                CoreText {
                    objectName: "sendReviewImportedOutputsNotice"
                    Layout.fillWidth: true
                    visible: root.importedPsbt
                    text: qsTr("All transaction outputs are shown. Wallet-owned outputs may include change.")
                    wrapMode: Text.WordWrap
                    color: Theme.color.neutral7
                }
                ListView {
                    id: recipientRows
                    objectName: "sendTransactionReviewRecipientRows"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(360, Math.max(72, contentHeight))
                    model: root.detailsReady ? root.flowDetails.recipientRows : null
                    cacheBuffer: 0
                    reuseItems: true
                    clip: true
                    ScrollBar.vertical: ScrollBar {}
                    delegate: ColumnLayout {
                        id: recipientCell
                        objectName: "sendReviewRecipientPreview"
                        required property int index
                        required property var entry
                        width: recipientRows.width
                        spacing: 0
                        FormRow {
                            id: recipientRow
                            objectName: "sendTransactionReviewRecipient" + recipientCell.index
                            Layout.fillWidth: true
                            showDivider: recipientCell.index < recipientRows.count - 1
                            minimumRowHeight: 72
                            title: recipientCell.entry.label || ""
                            description: recipientCell.entry.formattedAddress || ""
                            titleTextStyle: Theme.text.subheading
                            descriptionTextStyle: Theme.text.monoCaption
                            descriptionColor: Theme.color.neutral9
                            showLeadingItem: !!recipientCell.entry.hasPaymentRequest
                            leadingItem: ActivityRequestBadge {
                                objectName: "sendTransactionReviewRecipient" + recipientCell.index + "RequestBadge"
                                inactive: true
                                surfaceColor: Theme.color.surface1
                                statusText: qsTr("Payment request recipient")
                            }
                            trailingItem: BitcoinAmountDisplayLabel {
                                objectName: "sendTransactionReviewRecipient" + recipientCell.index + "Amount"
                                text: recipientCell.entry.amount || ""
                                animateUnitChanges: false
                                textStyle: Theme.text.amountDescription
                                color: Theme.color.neutral9
                            }
                        }
                        TextButton {
                            objectName: "sendTransactionReviewRecipientFields" + recipientCell.index
                            text: recipientCell.entry.textTruncated ? qsTr("View complete recipient") : qsTr("View recipient fields")
                            onClicked: root.openReviewEntry("recipients", recipientCell.index)
                        }
                    }
                }
                BusyIndicator { visible: root.detailsContextValid && root.flowDetails.pending; running: visible }
                CoreText {
                    Layout.fillWidth: true
                    visible: root.detailsContextValid && root.flowDetails.error.length > 0
                    text: root.flowDetails ? root.flowDetails.error : ""
                    color: Theme.color.dangerText
                    wrap: true
                }
            }

            FormSection {
                objectName: "sendTransactionReviewNetworkFee"
                visible: root.wallet && !root.importedPsbt
                Layout.fillWidth: true
                title: qsTr("Network fee")
                ValueRow {
                    objectName: "sendTransactionReviewTargetBlocks"
                    visible: root.transaction && root.transaction.reviewFeeSource === FeeSelectionModel.Estimated
                    Layout.fillWidth: true
                    title: qsTr("Requested target")
                    value: root.transaction ? (root.transaction.reviewTargetBlocks >= 50
                        ? qsTr("50+ blocks") : qsTr("%1 blocks").arg(root.transaction.reviewTargetBlocks)) : ""
                }
                ValueRow {
                    objectName: "sendTransactionReviewFeeRate"
                    Layout.fillWidth: true
                    title: qsTr("Fee rate")
                    value: root.transaction && root.transaction.reviewFeeRate.length > 0
                        ? qsTr("%1 sat/vB").arg(root.transaction.reviewFeeRate) : "—"
                    valueTextStyle: Theme.text.monoDescription
                    showDivider: false
                }
            }

            CoreText {
                objectName: "sendReviewFallbackFeeWarning"
                Layout.fillWidth: true
                visible: root.transaction && root.transaction.reviewUsesFallback
                text: qsTr("The wallet fallback fee was used. A confirmation target is unavailable.")
                wrapMode: Text.WordWrap
                color: Theme.color.neutral7
            }

            FormSection {
                objectName: "sendTransactionReviewTotals"
                Layout.fillWidth: true
                ValueRow {
                    objectName: "sendTransactionReviewFee"
                    amountValue: true
                    animateUnitChanges: false
                    Layout.fillWidth: true
                    title: qsTr("Total fees")
                    value: root.transaction ? root.transaction.fee : ""
                    dividerColor: Theme.color.surfaceDivider
                }
                ValueRow {
                    objectName: "sendTransactionReviewTotal"
                    amountValue: true
                    animateUnitChanges: false
                    Layout.fillWidth: true
                    title: root.importedPsbt ? qsTr("Outputs plus fee") : qsTr("Total amount")
                    value: root.transaction ? root.transaction.total : ""
                    showDivider: false
                }
            }

            TransactionFlow {
                objectName: "sendTransactionReviewFlow"
                Layout.fillWidth: true
                flow: root.wallet && root.wallet.currentTransactionFlow ? root.wallet.currentTransactionFlow : ({})
                transactionId: root.transaction ? root.transaction.txid : ""
                displayUnit: ApplicationModels.optionsModel.displayUnit
                walletName: root.wallet && root.wallet.name ? root.wallet.name : qsTr("Your wallet")
                interactive: false
            }

            TransactionEntriesList {
                objectName: "sendReviewAllInputs"
                Layout.fillWidth: true
                title: qsTr("All inputs")
                entryModel: root.detailsReady ? root.flowDetails.inputRows : null
                input: true
                walletName: root.wallet && root.wallet.name ? root.wallet.name : qsTr("Your wallet")
                onEntryRequested: (row) => root.openReviewEntry("inputs", row)
            }
            TransactionEntriesList {
                objectName: "sendReviewAllOutputs"
                Layout.fillWidth: true
                title: qsTr("All outputs")
                entryModel: root.detailsReady ? root.flowDetails.outputRows : null
                walletName: root.wallet && root.wallet.name ? root.wallet.name : qsTr("Your wallet")
                onEntryRequested: (row) => root.openReviewEntry("outputs", row)
            }

            Separator { Layout.fillWidth: true }

            ExternalSignerReviewActions {
                id: externalSignerActions
                enabled: root.detailsReady
                statusObjectName: "sendTransactionReviewSignerStatus"
                buttonObjectName: "sendTransactionReviewSignerButton"
                visible: root.showSend && root.wallet && root.wallet.hasExternalSigner
                Layout.fillWidth: true
                approvalState: sendController.signerApprovalState
                errorMessage: sendController.signerApprovalError
                signerNotFound: sendController.signerNotFound
                reviewMessage: root.reviewInterface ? root.reviewInterface.currentTransactionReviewMessage : ""
                canSend: root.canSend
                onApprovalRequested: sendController.approveSigner()
                onSendRequested: root.commitSend()
            }

            Flow {
                Layout.fillWidth: true
                spacing: 12
                NeutralButton {
                    objectName: "sendTransactionReviewSaveButton"
                    buttonSize: NeutralButton.Large
                    enabled: !!root.transaction && !root.sending && !root.sendBusy
                    width: parent.width < SizeClass.stackColumnsMax || !root.showSend && !root.showBroadcast || externalSignerActions.visible
                        ? parent.width : (parent.width - 12) / 2
                    height: 46
                    text: qsTr("Save transaction")
                    onClicked: root.startSavePsbt()
                }
                NeutralButton {
                    objectName: "sendTransactionReviewExportUnverifiedSignerButton"
                    visible: sendController.signerResultAvailable
                    enabled: root.wallet && !!root.transaction && !root.sending && !root.sendBusy && !root.wallet.psbtActionBusy
                    buttonSize: NeutralButton.Large
                    width: parent.width
                    height: Theme.size.controlLarge
                    text: qsTr("Export unverified signer result")
                    onClicked: root.startSaveUnverifiedSignerResult()
                }
                NeutralButton {
                    objectName: "sendTransactionReviewSignOnlyButton"
                    visible: root.reviewInterface && root.reviewInterface.currentTransactionCanSignOnly
                    buttonSize: NeutralButton.Large
                    enabled: root.detailsReady && !root.sending && !root.sendBusy
                    width: parent.width < SizeClass.stackColumnsMax ? parent.width : (parent.width - 12) / 2
                    height: Theme.size.controlLarge
                    text: qsTr("Sign only")
                    onClicked: root.signOnly()
                }
                PrimaryButton {
                    objectName: "sendTransactionReviewSendButton"
                    visible: (root.showSend && !externalSignerActions.visible) || root.showBroadcast
                    enabled: root.detailsReady && !root.sending && !root.sendBusy && (root.showBroadcast || root.canSend)
                    width: parent.width < SizeClass.stackColumnsMax ? parent.width : (parent.width - 12) / 2
                    height: 46
                    text: root.showBroadcast ? qsTr("Broadcast transaction") : qsTr("Send transaction")
                    onClicked: root.showBroadcast ? root.commitBroadcast() : root.commitSend()
                }
            }

            ToastBanner {
                objectName: "sendTransactionReviewSaveToast"
                shown: root.saveStatus.length > 0 && !root.saveError
                Layout.fillWidth: true
                tone: ToastBanner.Success
                text: root.saveStatus
                dismissAfter: 3
                onDismissed: if (!root.saveError) sendController.dismissPsbtResult()
            }

            CoreText {
                objectName: "sendTransactionReviewSaveError"
                visible: root.saveError && root.saveStatus.length > 0
                Layout.fillWidth: true
                text: root.saveStatus
                color: Theme.color.dangerText
                textStyle: Theme.text.description
                wrap: true
            }

            CoreText {
                objectName: "sendReviewNodePolicy"
                visible: root.importedPsbt && text.length > 0
                Layout.fillWidth: true
                text: root.transaction ? root.transaction.nodePolicyStatus : ""
                textStyle: Theme.text.description
                wrap: true
            }

            NeutralButton {
                objectName: "sendReviewRecheck"
                visible: root.importedPsbt
                enabled: root.wallet !== null && root.wallet.available && root.transaction !== null && !root.sendBusy
                text: qsTr("Recheck transaction")
                onClicked: sendController.recheck()
            }

            ToastBanner {
                objectName: "sendTransactionErrorBanner"
                shown: text.length > 0
                Layout.fillWidth: true
                tone: ToastBanner.Error
                text: sendController.reviewError
            }

            Item { Layout.preferredHeight: 24 }
        }
    }

    function openReviewEntry(side, row) {
        if (!root.detailsReady) return
        if (root.flowDetails.requestEntryDetails(side, row, root.transaction)) reviewEntryPopup.open()
    }
    TransactionEntryPopup {
        id: reviewEntryPopup
        objectName: "sendReviewEntryPopup"
        documentObjectName: "sendReviewEntryDocument"
        loading: !!root.flowDetails && root.flowDetails.entryDetailsBusy
        errorText: root.flowDetails ? root.flowDetails.entryDetailsError : ""
        document: root.detailsContextValid ? root.flowDetails.entryDocument : null
    }

    AlertPopup {
        id: sweepAlert
        objectName: "sendSweepAlert"
        parent: Overlay.overlay
        shown: sendController.prompt === TransactionReviewController.SweepConsent
        //: Final confirmation before sending a transaction that uses all available wallet funds.
        title: qsTr("Send all available funds?")
        //: Warns that a full-wallet send cannot be undone after confirmation on the network.
        //: %1 is the prepared transaction's recipient amount with its unit, excluding the fee.
        message: qsTr("This will send all available funds (%1) from this wallet. Once confirmed, the transaction cannot be undone.").arg(root.transaction ? root.transaction.amountAmount.displayWithUnit : "—")
        AlertAction {
            //: Dismiss the full-wallet send confirmation and keep the prepared transaction open for review.
            text: qsTr("Back to review")
            role: AlertAction.Cancel
            buttonObjectName: "sendSweepCancelButton"
            closesPopup: false
            onTriggered: sendController.cancel()
        }
        AlertAction {
            //: Confirm and send the prepared transaction using all available wallet funds.
            text: qsTr("Send all funds")
            buttonObjectName: "sendSweepConfirmButton"
            closesPopup: false
            onTriggered: sendController.confirm()
        }
        onCloseRequested: sendController.cancel()
    }

    WalletPassphrasePopup {
        id: sendPassphrasePopup
        popupObjectName: "sendTransactionPassphrasePopup"
        parent: Overlay.overlay
        titleText: qsTr("Enter wallet password")
        shown: sendController.prompt === TransactionReviewController.Password
        descriptionText: sendController.signOnly
            ? qsTr("Enter your wallet password to sign this PSBT. The transaction will not be sent.")
            : qsTr("Enter your wallet password to send this transaction.")
        confirmText: sendController.signOnly ? qsTr("Unlock and sign") : qsTr("Unlock and send")
        busyConfirmText: sendController.signOnly ? qsTr("Signing…") : qsTr("Unlocking…")
        errorText: sendController.sendError
        busy: sendController.sending
        // Cancel and Escape answer the prompt with no.
        onCloseRequested: sendController.cancel()
        onSubmitted: (passphrase) => sendController.unlock(passphrase)
    }
}
