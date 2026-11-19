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

    property WalletQmlModel wallet: null
    readonly property WalletQmlModelTransaction transaction: wallet ? wallet.currentTransaction : null
    readonly property var flowDetails: wallet ? wallet.currentTransactionFlowDetails : null
    readonly property bool detailsContextValid: wallet !== null && wallet.available && transaction !== null
        && flowDetails !== null && flowDetails.context === transaction
        && flowDetails.revision === wallet.currentTransactionFlowRevision
    readonly property bool detailsReady: detailsContextValid && !flowDetails.pending && flowDetails.error.length === 0
    readonly property bool importedPsbt: wallet && wallet.currentTransactionIsImportedPsbt === true
    readonly property bool canSend: wallet && wallet.currentTransactionCanSend
        && !wallet.currentTransactionNeedsRevalidation
    readonly property bool feePending: wallet && wallet.transactionFeePending
    readonly property bool canBroadcast: wallet && wallet.currentTransactionCanBroadcast
    readonly property bool showBroadcast: importedPsbt && canBroadcast
    readonly property bool showSend: !showBroadcast && canSend
    readonly property string reviewMessage: wallet && wallet.currentTransactionNeedsRevalidation
        ? qsTr("The draft changed after review. Prepare it again before sending.")
        : wallet ? wallet.currentTransactionReviewMessage : ""
    readonly property bool showWarning: (wallet && wallet.currentTransactionNeedsRevalidation)
        || reviewMessage.length > 0 || (wallet && wallet.keySchemeKind === WalletQmlModel.WatchOnly && !canBroadcast)
    readonly property real sidePadding: SizeClass.isCompact(width) ? Theme.space.pageCompact : Theme.space.pageRegular
    readonly property real reviewContentWidth: Math.max(0, Math.min(1100, width - sidePadding * 2))
    readonly property bool sendBusy: wallet !== null && (wallet.sendBusy || wallet.psbtActionBusy)
    property WalletQmlModelTransaction sendReview: null
    property WalletQmlModel sendWallet: null
    property double sendTicket: 0
    property bool sending: false
    property double psbtTicket: 0
    property WalletQmlModelTransaction psbtReview: null
    property bool psbtSaving: false
    property string saveStatus: ""
    property bool saveError: false

    onVisibleChanged: {
        if (!visible) {
            cancelPendingSendPrompt()
            sweepAlert.close()
            reviewEntryPopup.close()
            sendPassphrasePopup.close()
            if (wallet && wallet.fileExportOperation && wallet.fileExportOperation.pending)
                wallet.fileExportOperation.retire()
            sending = false
            sendTicket = 0
            saveStatus = ""
            saveError = false
        }
    }

    signal back()
    signal transactionSent(string txid, int submissionState, int recordState)

    // The review is a full-window sheet: Escape goes back like its close button.
    Keys.onEscapePressed: function(event) {
        if (!root.sending) root.back()
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

    onWalletChanged: {
        cancelPendingSendPrompt()
        if (sendWallet && sendWallet.fileExportOperation && sendWallet.fileExportOperation.pending)
            sendWallet.fileExportOperation.retire()
        psbtTicket = 0; psbtReview = null; sendTicket = 0; sendReview = null; sendWallet = null
        sweepAlert.close(); sendPassphrasePopup.close()
    }
    onTransactionChanged: {
        if (sendReview !== null && sendReview !== transaction) cancelPendingSendPrompt()
        sweepAlert.close(); reviewEntryPopup.close()
    }
    onDetailsContextValidChanged: if (!detailsContextValid) reviewEntryPopup.close()

    function commitSend() {
        if (sending || sendBusy || !wallet || !detailsReady) return
        sendWallet = wallet
        sendReview = wallet.currentTransaction
        sendTicket = 0
        sendTicket = wallet.requestSendTransaction()
        handleSendOutcome()
    }

    function signOnly() {
        if (sending || sendBusy || !wallet || !detailsReady || !wallet.currentTransactionCanSignOnly) return
        sendWallet = wallet
        sendReview = wallet.currentTransaction
        sendTicket = 0
        sendTicket = wallet.requestSignOnlyTransaction()
        handleSendOutcome()
    }

    // The send confirmations authorize the transaction that was shown when
    // they opened. A new review or PSBT import replaces the transaction
    // object, which closes them and makes a pending submit stale; signing
    // updates the same object.
    function isCurrentTransaction(reviewed) {
        return visible && wallet !== null && wallet.available && reviewed !== null && reviewed === wallet.currentTransaction
    }

    function cancelPendingSendPrompt() {
        if (sendWallet && sendTicket) {
            sendWallet.cancelSendTransaction(sendTicket)
        }
    }

    function handleSendOutcome() {
            if (!root.sendTicket || !root.wallet || root.wallet.sendOperationId !== root.sendTicket || !root.isCurrentTransaction(root.sendReview) || root.sending) return
            if (root.wallet.sendState === WalletQmlModel.SendNeedsConfirmation) {
                sweepAlert.reviewedTransaction = root.sendReview
                if (!sweepAlert.opened) sweepAlert.open()
            } else if (root.wallet.sendState === WalletQmlModel.SendNeedsUnlock) {
                sendPassphrasePopup.reviewedTransaction = root.sendReview
                sendPassphrasePopup.errorText = root.wallet.transactionError
                if (!sendPassphrasePopup.opened) sendPassphrasePopup.open()
            } else if (root.wallet.sendState === WalletQmlModel.SendCommitted) {
                sendPassphrasePopup.close()
                root.sending = true
                root.transactionSent(root.wallet.sendResultTxid, root.wallet.sendSubmission, root.wallet.sendRecordState)
            } else if (sendPassphrasePopup.opened && !root.sendBusy) {
                sendPassphrasePopup.errorText = root.wallet.transactionError
            }
    }

    Connections {
        target: root.wallet
        function onSendOperationChanged() { root.handleSendOutcome() }
        function onPsbtActionChanged() { root.handlePsbtOutcome() }
        function onCurrentTransactionChanged() {
            if (!root.isCurrentTransaction(sweepAlert.reviewedTransaction)) sweepAlert.close()
            if (!root.isCurrentTransaction(sendPassphrasePopup.reviewedTransaction)) sendPassphrasePopup.close()
        }
    }

    function handlePsbtOutcome() {
        if (!psbtTicket || !wallet || wallet.psbtActionId !== psbtTicket || wallet.psbtActionBusy) return
        psbtTicket = 0
        if (!root.isCurrentTransaction(psbtReview)) return
        if (psbtSaving) {
            saveError = wallet.psbtActionState !== WalletQmlModel.PsbtSaved
            saveStatus = saveError ? wallet.psbtActionError : qsTr("Saved.")
        } else if (wallet.psbtActionState === WalletQmlModel.PsbtSubmitted && !sending) {
            sending = true
            transactionSent(wallet.psbtSubmittedTxid, WalletQmlModel.SubmissionAccepted, WalletQmlModel.SendRecordUnknown)
        }
    }
    function commitBroadcast() {
        if (sending || sendBusy || !wallet || !detailsReady || wallet.psbtActionBusy) return
        psbtSaving = false
        psbtReview = transaction
        psbtTicket = 0
        psbtTicket = wallet.requestBroadcastCurrentTransaction()
        handlePsbtOutcome()
    }

    function defaultSavePsbtFileUrl() {
        return "file://" + ApplicationModels.walletController.homePath() + "/transaction-"
            + Qt.formatDateTime(new Date(), "yyyy-MM-dd-HHmm") + ".psbt"
    }

    function savePsbt(path, signerResultToken) {
        if (!wallet || !String(path).length || wallet.psbtActionBusy) return
        psbtSaving = true
        psbtReview = transaction
        psbtTicket = 0
        psbtTicket = signerResultToken
            ? wallet.requestSaveUnverifiedSignerResult(signerResultToken, String(path))
            : wallet.requestSaveCurrentTransactionAsPsbt(String(path))
        if (!psbtTicket) { saveStatus = qsTr("Another wallet action is still running."); saveError = true }
        else handlePsbtOutcome()
    }

    function startSavePsbt() {
        saveStatus = ""
        saveError = false
        if (savePsbtAutomationPath.text.length) {
            const path = savePsbtAutomationPath.text
            savePsbtAutomationPath.text = ""
            savePsbt(path)
        } else {
            savePsbtDialog.currentFile = defaultSavePsbtFileUrl()
            savePsbtDialog.targetWallet = wallet
            savePsbtDialog.signerResultToken = ""
            savePsbtDialog.open()
        }
    }

    function startSaveUnverifiedSignerResult() {
        if (!wallet || !wallet.signerUnverifiedResultToken.length) return
        const token = wallet.signerUnverifiedResultToken
        if (savePsbtAutomationPath.text.length) {
            const path = savePsbtAutomationPath.text
            savePsbtAutomationPath.text = ""
            savePsbt(path, token)
        } else {
            savePsbtDialog.currentFile = defaultSavePsbtFileUrl()
            savePsbtDialog.targetWallet = wallet
            savePsbtDialog.signerResultToken = token
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
        closeButtonObjectName: "sendTransactionReviewCloseButton"
        closeButtonSize: CloseButton.Large
        onCloseClicked: root.back()
    }

    AppFileDialog {
        id: savePsbtDialog
        objectName: "sendTransactionReviewSaveDialog"
        title: signerResultToken.length ? qsTr("Export unverified signer result") : qsTr("Save transaction as PSBT")
        fileMode: AppFileDialog.SaveFile
        currentFolder: "file://" + ApplicationModels.walletController.homePath()
        defaultSuffix: "psbt"
        nameFilters: [qsTr("Partially Signed Bitcoin Transactions (*.psbt)"), qsTr("All files (*)")]
        property var targetWallet: null
        property string signerResultToken: ""
        onAccepted: {
            const target = targetWallet
            const token = signerResultToken
            targetWallet = null
            signerResultToken = ""
            if (target && target === root.wallet) root.savePsbt(selectedFile.toString(), token)
        }
        onRejected: { targetWallet = null; signerResultToken = "" }
    }

    Instantiator {
        model: root.wallet && root.wallet.fileExportOperation ? [root.wallet.fileExportOperation] : []
        delegate: SaveConfirmationPopup {
            required property FileExportOperationModel modelData
            objectName: "psbtExportOverwritePopup"
            parent: root.Overlay.overlay
            visible: root.visible && modelData.confirmationRequired
            replacing: modelData.replacing
            finalPath: modelData.finalPath
            saveTitle: qsTr("Save PSBT file?")
            replaceTitle: qsTr("Replace PSBT file?")
            pathObjectName: "psbtExportFinalPathText"
            approveObjectName: "psbtExportApproveButton"
            refuseObjectName: "psbtExportRejectButton"
            onApproved: modelData.approve()
            onRefused: modelData.refuse()
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
                wallet: root.wallet
                canSend: root.canSend
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
                    visible: root.wallet && root.wallet.signerUnverifiedResultToken.length > 0
                    enabled: root.wallet && !!root.transaction && !root.sending && !root.sendBusy && !root.wallet.psbtActionBusy
                    buttonSize: NeutralButton.Large
                    width: parent.width
                    height: Theme.size.controlLarge
                    text: qsTr("Export unverified signer result")
                    onClicked: root.startSaveUnverifiedSignerResult()
                }
                NeutralButton {
                    objectName: "sendTransactionReviewSignOnlyButton"
                    visible: root.wallet && root.wallet.currentTransactionCanSignOnly
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
                onDismissed: if (!root.saveError) root.saveStatus = ""
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
                onClicked: root.wallet.requestRecheckCurrentTransaction()
            }

            ToastBanner {
                objectName: "sendTransactionErrorBanner"
                shown: text.length > 0
                Layout.fillWidth: true
                tone: ToastBanner.Error
                text: root.wallet ? root.wallet.transactionError : ""
            }

            Item { Layout.preferredHeight: 24 }
        }
    }

    function openReviewEntry(side, row) {
        if (!root.detailsReady) return
        if (root.flowDetails.requestEntryDetails(side, row, root.transaction, root.wallet.currentTransactionFlowRevision)) reviewEntryPopup.open()
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
        property WalletQmlModelTransaction reviewedTransaction: null
        //: Final confirmation before sending a transaction that uses all available wallet funds.
        title: qsTr("Send all available funds?")
        //: Warns that a full-wallet send cannot be undone after confirmation on the network.
        //: %1 is the prepared transaction's recipient amount with its unit, excluding the fee.
        message: qsTr("This will send all available funds (%1) from this wallet. Once confirmed, the transaction cannot be undone.").arg(root.transaction ? root.transaction.amountAmount.displayWithUnit : "—")
        // Return outside a button keeps the review instead of sending everything.
        returnAction: sweepCancelAction
        AlertAction {
            id: sweepCancelAction
            //: Dismiss the full-wallet send confirmation and keep the prepared transaction open for review.
            text: qsTr("Back to review")
            role: AlertAction.Cancel
            buttonObjectName: "sendSweepCancelButton"
            onTriggered: root.cancelPendingSendPrompt()
        }
        AlertAction {
            //: Confirm and send the prepared transaction using all available wallet funds.
            text: qsTr("Send all funds")
            buttonObjectName: "sendSweepConfirmButton"
            // Confirm before closing: onClosed cancels an unconfirmed send.
            closesPopup: false
            onTriggered: {
                if (root.isCurrentTransaction(sweepAlert.reviewedTransaction) && root.wallet && root.sendTicket) {
                    root.wallet.confirmSendTransaction(root.sendTicket)
                }
                sweepAlert.close()
            }
        }
        onClosed: root.cancelPendingSendPrompt()
    }

    WalletPassphrasePopup {
        id: sendPassphrasePopup
        popupObjectName: "sendTransactionPassphrasePopup"
        parent: Overlay.overlay
        titleText: qsTr("Enter wallet password")
        descriptionText: actionWallet && actionWallet.sendSignOnly
            ? qsTr("Enter your wallet password to sign this PSBT. The transaction will not be sent.")
            : qsTr("Enter your wallet password to send this transaction.")
        confirmText: actionWallet && actionWallet.sendSignOnly ? qsTr("Unlock and sign") : qsTr("Unlock and send")
        busyConfirmText: actionWallet && actionWallet.sendSignOnly ? qsTr("Signing…") : qsTr("Unlocking…")
        property WalletQmlModel actionWallet: null
        property WalletQmlModelTransaction reviewedTransaction: null
        contextValid: root.visible && actionWallet !== null
            && actionWallet === root.wallet && actionWallet.available
            && reviewedTransaction !== null && reviewedTransaction === actionWallet.currentTransaction
        onAboutToShow: actionWallet = root.wallet
        busy: actionWallet !== null && actionWallet.sendBusy
        onClosed: root.cancelPendingSendPrompt()
        onSubmitted: (passphrase) => {
            if (!opened || !contextValid) {
                close()
                return
            }
            root.sendReview = reviewedTransaction
            root.sendTicket = 0
            root.sendTicket = actionWallet.requestSendTransactionWithPassphrase(passphrase)
            root.handleSendOutcome()
        }
    }
}
