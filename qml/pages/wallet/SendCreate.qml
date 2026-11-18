// Copyright (c) 2024 The Bitcoin Core developers
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
    objectName: "sendPage"
    vertical: true

    property WalletQmlModel wallet: null
    property SendRecipient recipient: wallet ? wallet.recipients.current : null
    property string prepareTransactionErrorText: ""
    readonly property bool manualCoinSelection: root.wallet !== null
        && root.wallet.coinsListModel !== null && root.wallet.coinsListModel.manualInputSelection
    readonly property bool externalSignerWallet: wallet !== null && wallet.hasExternalSigner
    readonly property string recipientValidationError: wallet ? wallet.recipients.budgetError || wallet.recipients.validationError : ""
    readonly property bool selectedInputsActive: wallet !== null
        && wallet.coinsListModel !== null
        && (wallet.coinsListModel.manualInputSelection || wallet.coinsListModel.selectedCoinsCount > 0)
    readonly property bool hasLockedCoins: wallet !== null
        && wallet.coinsListModel !== null
        && wallet.coinsListModel.lockedSatoshi > 0
    readonly property string baseAvailableBalanceErrorText: qsTr("Amount plus fee exceeds available balance")
    readonly property string walletBalanceErrorText: qsTr("The wallet does not have enough balance for this transaction.")
    readonly property string availableBalanceErrorText: hasLockedCoins
        ? qsTr("Amount plus fee exceeds available balance. Some of your coins are locked.")
        : baseAvailableBalanceErrorText
    readonly property string selectedInputsBalanceErrorText: qsTr("Selected inputs do not cover the amount plus fee")
    readonly property string feeBalanceErrorText: wallet && wallet.sendAmountExhaustsBalance
        ? (selectedInputsActive ? selectedInputsBalanceErrorText : availableBalanceErrorText)
        : ""
    readonly property string formErrorText: wallet && wallet.fees && !wallet.fees.estimatePending && wallet.fees.errorText.length > 0
        ? wallet.fees.errorText : recipientValidationError.length > 0
        ? recipientValidationError
        : (wallet && wallet.coinsListModel && wallet.coinsListModel.selectionReconciliationRequired
           ? qsTr("Some selected coins are no longer available. Choose Automatic inputs or update the selection.")
           : (feeBalanceErrorText.length > 0 ? feeBalanceErrorText : prepareTransactionErrorText))

    signal viewTransactionInActivity(string txid)

    property double psbtImportTicket: 0
    property WalletQmlModel psbtImportWallet: null
    property int psbtImportHiddenCount: -1
    property string psbtImportDraftRevision: ""
    property string psbtImportSelectionToken: ""
    function requestPsbtImport(path) {
        if (!root.wallet || root.wallet.psbtActionBusy) return
        psbtImportWallet = root.wallet
        psbtImportHiddenCount = root.hiddenCount
        psbtImportDraftRevision = root.draftRevision
        psbtImportSelectionToken = root.wallet.coinsListModel.selectionToken
        psbtImportTicket = 0
        psbtImportTicket = root.wallet.requestImportPsbtFromFile(path)
        handlePsbtOutcome()
    }
    function handlePsbtOutcome() {
        if (!psbtImportTicket || !root.wallet || psbtImportWallet !== root.wallet || root.wallet.psbtActionId !== psbtImportTicket || root.wallet.psbtActionBusy) return
        psbtImportTicket = 0
        if (!root.visible || psbtImportHiddenCount !== root.hiddenCount || psbtImportDraftRevision !== root.draftRevision
            || root.wallet.coinsListModel.selectionPending || psbtImportSelectionToken !== root.wallet.coinsListModel.selectionToken
            || root.wallet.psbtActionState === WalletQmlModel.PsbtStale) return
        sendPage.handlePsbtImportResult(root.wallet.psbtImportResult)
    }

    function confirmTransactionReview() {
        if (!root.wallet || !root.wallet.available || sendPage.uriActionPending) return
        if (root.wallet.sendDraftSweepsWallet) {
            reviewSweepAlert.draftRevision = root.draftRevision
            reviewSweepAlert.open()
        }
        else root.prepareTransactionForReview()
    }

    property bool preparingReview: false
    property double preparationTicket: 0
    property WalletQmlModel preparationWallet: null
    property string preparationDraftRevision: ""
    property int preparationHiddenCount: -1

    function prepareTransactionForReview() {
        if (!root.wallet || !root.wallet.available || root.wallet.preparationBusy || sendPage.uriActionPending) return
        root.clearPrepareTransactionError()
        preparationWallet = root.wallet
        preparationDraftRevision = root.draftRevision
        preparationHiddenCount = root.hiddenCount
        preparingReview = true
        preparationTicket = 0
        preparationTicket = root.wallet.requestPrepareTransaction()
        if (!preparationTicket) {
            preparingReview = false
            root.prepareTransactionErrorText = qsTr("Another wallet action is still running.")
        } else root.handlePreparationOutcome()
    }

    function handlePreparationOutcome() {
        if (!preparingReview || !preparationTicket || preparationWallet !== root.wallet || !root.wallet) return
        if (root.wallet.preparationId !== preparationTicket) { preparingReview = false; return }
        if (root.wallet.preparationBusy) return
        preparingReview = false
        if (!root.visible || preparationDraftRevision !== root.draftRevision || preparationHiddenCount !== root.hiddenCount) return
        if (root.wallet.preparationState === WalletQmlModel.PreparationReady) {
            reviewPassphrasePopup.close()
            root.openTransactionReview()
        } else if (root.wallet.preparationState === WalletQmlModel.PreparationNeedsUnlock) {
            reviewPassphrasePopup.open()
        } else if (root.wallet.preparationState === WalletQmlModel.PreparationStale) {
            // The draft on this page did not change, so the wallet or an import
            // did. Say so instead of ending Review without explanation.
            root.prepareTransactionErrorText = qsTr("Your balance or this transaction changed while it was being prepared. Review it again.")
        } else {
            root.prepareTransactionErrorText = root.wallet.transactionError.length > 0
                ? root.displayTransactionError(root.wallet.transactionError)
                : root.selectedInputsActive ? root.selectedInputsBalanceErrorText : root.availableBalanceErrorText
        }
    }

    function openTransactionReview() {
        transactionReviewPopup.reviewWallet = root.wallet
        transactionReviewPopup.importedReview = root.wallet.currentTransactionIsImportedPsbt === true
        transactionReviewPopup.open()
    }

    function returnToSendForm() {
        if (root.depth > 1) {
            root.pop(null, StackView.Immediate)
        }
    }

    function openPaymentRequestImport() {
        root.returnToSendForm()
        root.openWhileShown(sendPage.openPaymentRequestImport)
    }

    function acceptDesktopImport(mailbox: DesktopIntentModel): bool {
        if (!mailbox || !root.visible || !root.wallet || !root.wallet.available || !root.recipient
                || root.wallet.recipients.importPending) return false
        const originalWallet = root.wallet
        const originalRecipient = root.recipient
        sendPage.cancelUriAction()
        if (root.wallet !== originalWallet || root.recipient !== originalRecipient || !root.visible) return false
        const operation = mailbox.takeInspection(sendPage)
        if (!operation) return false
        sendPage.m_importOperation = operation
        sendPage.m_importWallet = originalWallet
        sendPage.m_importSource = qsTr("desktop")
        if (root.wallet !== originalWallet || root.recipient !== originalRecipient || !root.visible || !originalWallet.available) {
            operation.cancel()
            return false
        }
        operation.chooseTarget(originalWallet, originalRecipient)
        operation.use()
        return true
    }

    function openPsbtFileImport() {
        root.returnToSendForm()
        root.openWhileShown(sendPage.openPsbtFileImport)
    }

    // The draft owner stamps captured user intent. Validation echoes and
    // automatic Maximum observations do not create another user revision.
    readonly property string draftRevision: root.wallet ? root.wallet.recipients.revision : ""
    onDraftRevisionChanged: if (sendPage && reviewSweepAlert) root.draftChanged()

    function draftChanged() {
        if (!(root.wallet && root.wallet.recipients.importPending)) sendPage.cancelUriIntents()
        root.preparingReview = false
        reviewSweepAlert.close()
        sendPage.checkClipboard()
    }

    // Counts how often this page was hidden. A deferred open is dropped if
    // the page was hidden in between, even if it is shown again by then.
    property int hiddenCount: 0

    function openWhileShown(open) {
        const hiddenCount = root.hiddenCount
        Qt.callLater(function() {
            if (root.visible && hiddenCount === root.hiddenCount) open()
        })
    }

    function clearPrepareTransactionError() {
        if (prepareTransactionErrorText.length > 0) {
            prepareTransactionErrorText = ""
        }
    }

    function resetSendForm() {
        if (!root.wallet) return
        root.wallet.recipients.clear()
        root.wallet.coinsListModel.setManualInputSelection(false)
        root.wallet.coinsListModel.update()
        root.clearPrepareTransactionError()
        sendPage.expandedRecipient = 0
        sendPage.m_importAdmissionError = ""
    }

    function displayTransactionError(error) {
        if (hasLockedCoins && !selectedInputsActive
                && (error === baseAvailableBalanceErrorText || error === walletBalanceErrorText)) {
            return availableBalanceErrorText
        }
        return error
    }

    function scheduleFeeEstimates() {
        if (root.wallet) {
            root.wallet.scheduleFeeEstimates()
        }
    }

    function updateExpandedRecipient(index) {
        sendPage.expandedRecipient = index
    }

    // Re-check the clipboard whenever the Send tab becomes visible so the
    // banner appears even if the URI was copied before navigating here.
    onVisibleChanged: {
        if (visible) {
            sendPage.checkClipboard()
        } else {
            root.hiddenCount++
            sendPage.cancelUriIntents()
            reviewSweepAlert.close()
        }
    }
    onWalletChanged: {
        sendPage.cancelUriIntents()
        reviewSweepAlert.close()
        if (transactionReviewPopup.opened) {
            transactionReviewPopup.close()
        }
        root.pop()
        // Clear URI import state so stale results from the previous wallet
        // are not shown when this page is pointed at another wallet.
        sendPage.m_importAdmissionError = ""
        paymentUriOverwritePopup.close()
    }

    Connections {
        target: root.wallet ? root.wallet.recipients : null
        function onListCleared() {
            root.draftChanged()
            root.clearPrepareTransactionError()
            sendPage.expandedRecipient = 0
            if (root.wallet) {
                root.wallet.scheduleFeeEstimates()
            }
        }
        function onValidationChanged() { if (!root.wallet.recipients.importPending) sendPage.checkClipboard() }
        function onCountChanged() {
            root.draftChanged()
            root.clearPrepareTransactionError()
            root.scheduleFeeEstimates()
        }
        function onTotalAmountChanged() {
            // Only the bounded warning presentation follows this observation.
            // A validation echo does not retire the original import intent.
            if (root.wallet && !root.wallet.maximumRecipient) reviewSweepAlert.close()
        }
        function onCurrentRecipientChanged() {
            if (sendPage.m_importOperation) sendPage.cancelUriAction()
            root.draftChanged()
            root.clearPrepareTransactionError()
            root.scheduleFeeEstimates()
            sendPage.m_importAdmissionError = ""
            sendPage.expandedRecipient = root.wallet.recipients.currentIndex - 1
            sendPage.checkClipboard()
            paymentUriOverwritePopup.close()
        }
    }

    Connections {
        target: root.wallet ? root.wallet.coinsListModel : null
        function onSelectionSnapshotChanged() {
            root.draftChanged()
            root.clearPrepareTransactionError()
        }
    }

    Connections {
        target: root.wallet
        function onCurrentTransactionChanged() { reviewSweepAlert.close(); sendPage.presentImportedReview() }
        function onPreparationChanged() { root.handlePreparationOutcome() }
        function onAvailableChanged() { if (!root.wallet.available) sendPage.cancelUriIntents() }
        function onPsbtActionChanged() { root.handlePsbtOutcome() }
        function onMaximumRecipientChanged() { root.draftChanged() }
        function onCustomFeeEnabledChanged() {
            root.clearPrepareTransactionError()
        }
        function onCustomFeeRateChanged() {
            root.clearPrepareTransactionError()
        }
    }

    Binding {
        target: root.recipient ? root.recipient.amount : null
        property: "unit"
        value: ApplicationModels.optionsModel.displayUnit
        when: root.recipient !== null
    }

    BitcoinAmount {
        id: recipientsTotalAmount
        satoshi: root.wallet && root.wallet.recipients
            ? root.wallet.recipients.totalAmountSatoshi
            : 0
        unit: ApplicationModels.optionsModel.displayUnit
    }

    initialItem: Page {
        id: sendPage
        objectName: "walletSendPage"
        background: null
        readonly property real pageSidePadding: SizeClass.isCompact(width) ? Theme.space.pageCompact : Theme.space.pageRegular

        function handlePsbtImportResult(result) {
            sendOptionsPopup.close()
            if (result === WalletQmlModel.WalletCanSign) {
                root.openTransactionReview()
            } else if (result === WalletQmlModel.WalletCannotSign || result === WalletQmlModel.ReviewPending) {
                root.openTransactionReview()
            } else if (result === WalletQmlModel.TransactionAlreadyKnown) {
                knownPsbtTxPopup.txid = root.wallet.importedPsbt.matchedTxid
                knownPsbtTxPopup.open()
            } else if (result === WalletQmlModel.PsbtUnsupported) {
                unsupportedPsbtPopup.message = root.wallet.importedPsbt.error.length > 0
                    ? root.wallet.importedPsbt.error
                    : qsTr("This PSBT is not supported yet.")
                unsupportedPsbtPopup.open()
            }
        }

        function openPaymentRequestImport() {
            uriImportInput.text = ""
            sendUriImportPopup.open()
        }

        function openPsbtFileImport() {
            sendOptionsPopup.close()
            cancelUriAction()
            m_importWallet = root.wallet
            m_importSource = qsTr("file")
            m_importOperation = BitcoinUri.captureImport(root.wallet, root.recipient, sendPage)
            if (m_importOperation) psbtOpenDialog.open()
        }

        Popup {
            id: transactionReviewPopup
            objectName: "transactionReviewPopup"
            parent: Overlay.overlay
            x: 0
            y: 0
            property real verticalOffset: 0
            modal: true
            focus: true
            dim: false
            closePolicy: Popup.NoAutoClose
            padding: 0
            width: Overlay.overlay ? Overlay.overlay.width : 0
            height: Overlay.overlay ? Overlay.overlay.height : 0
            onOpened: {
                const review = reviewStack.currentItem as SendTransactionReview
                const complete = reviewStack.currentItem as SendComplete
                if (review) review.focusInitialControl()
                else if (complete) complete.focusInitialControl()
            }

            enter: Transition {
                enabled: Theme.motion.animated
                NumberAnimation {
                    property: "verticalOffset"
                    from: Theme.motion.reduced ? 0 : transactionReviewPopup.height
                    to: 0
                    duration: Theme.motion.travel(Theme.motion.slow)
                    easing.type: Theme.motion.easing.enter
                }
                NumberAnimation {
                    property: "opacity"
                    from: Theme.motion.reduced ? 0 : 1
                    to: 1
                    duration: Theme.motion.fade(Theme.motion.slow)
                    easing.type: Theme.motion.easing.enter
                }
            }
            exit: Transition {
                enabled: Theme.motion.animated
                NumberAnimation {
                    property: "verticalOffset"
                    to: Theme.motion.reduced ? 0 : transactionReviewPopup.height
                    duration: Theme.motion.travel(Theme.motion.exit)
                    easing.type: Theme.motion.easing.exit
                }
                NumberAnimation {
                    property: "opacity"
                    to: Theme.motion.reduced ? 0 : 1
                    duration: Theme.motion.fade(Theme.motion.exit)
                    easing.type: Theme.motion.easing.exit
                }
            }

            property WalletQmlModel reviewWallet: null
            property bool importedReview: false
            // Set once the payment is sent, until the form is cleared. The
            // popup can also close without Done, such as when the page is
            // hidden, and the sent payment must not stay in the form.
            property bool sent: false

            function clearSentForm() {
                if (!sent) return
                sent = false
                if (reviewWallet === root.wallet) root.resetSendForm()
            }
            // Qt 6.5 and older do not give focus back when a popup closes.
            property Item returnFocusItem: null

            onAboutToShow: {
                const window = contentItem ? contentItem.Window.window : null
                returnFocusItem = window ? window.activeFocusItem : null
            }
            onClosed: {
                clearSentForm()
                const focusTarget = returnFocusItem
                returnFocusItem = null
                if (focusTarget && focusTarget.visible && focusTarget.enabled) {
                    focusTarget.forceActiveFocus(Qt.OtherFocusReason)
                }
                const wallet = reviewWallet
                const wasImported = importedReview
                reviewStack.pop(null, StackView.Immediate)
                reviewWallet = null
                importedReview = false
                if (wallet && wasImported && !wallet.sendBusy) {
                    wallet.discardCurrentTransaction()
                }
            }

            background: null

            contentItem: PageStack {
                id: reviewStack
                objectName: "sendTransactionReviewStack"
                width: transactionReviewPopup.availableWidth
                height: transactionReviewPopup.availableHeight
                vertical: true
                initialItem: reviewPage
                transform: Translate { y: transactionReviewPopup.verticalOffset }
                pushEnter: Transition {
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.fade(Theme.motion.moderate); easing.type: Theme.motion.easing.enter }
                }
                pushExit: Transition {
                    // Keep Review visible under the fading Complete page until
                    // the Complete background is fully opaque.
                    NumberAnimation { property: "opacity"; from: 1; to: 1; duration: Theme.motion.fade(Theme.motion.moderate) }
                }

                Component {
                    id: reviewPage
                    SendTransactionReview {
                        wallet: transactionReviewPopup.reviewWallet
                        onBack: transactionReviewPopup.close()
                        onTransactionSent: (txid, submissionState, recordState) => {
                            transactionReviewPopup.sent = true
                            reviewStack.push(sendCompletePage, {
                                "txid": txid,
                                "submissionState": submissionState,
                                "recordState": recordState,
                                "targetBlocks": !transactionReviewPopup.importedReview && wallet && wallet.currentTransaction.reviewFeeSource === FeeSelectionModel.Estimated
                                    ? wallet.currentTransaction.reviewTargetBlocks : 0
                            })
                        }
                    }
                }

                Component {
                    id: sendCompletePage
                    SendComplete {
                        onDone: {
                            transactionReviewPopup.clearSentForm()
                            transactionReviewPopup.close()
                        }
                        onViewNewTransaction: (txid) => {
                            transactionReviewPopup.clearSentForm()
                            transactionReviewPopup.close()
                            root.viewTransactionInActivity(txid)
                        }
                    }
                }
            }
        }

        AppFileDialog {
            id: psbtOpenDialog
            objectName: "sendPsbtOpenDialog"
            title: qsTr("Import PSBT")
            fileMode: AppFileDialog.OpenFile
            nameFilters: [qsTr("Partially Signed Bitcoin Transactions (*.psbt)"), qsTr("All files (*)")]
            contextObject: sendPage.m_importOperation
            enabled: root.visible && root.wallet !== null && root.wallet.available && !root.wallet.psbtActionBusy
            onChosen: (context, file) => {
                const original = context as PaymentImportOperation
                if (original && original === sendPage.m_importOperation && original.requestFile(file)) original.use()
            }
        }

        // Kept hidden so functional tests can inject a PSBT path until the
        // native file dialog is automatable through the QML test bridge.
        TextField {
            id: psbtAutomationPathField
            objectName: "psbtImportPathField"
            visible: false
        }

        AlertPopup {
            id: unsupportedPsbtPopup
            objectName: "unsupportedPsbtPopup"
            parent: Overlay.overlay
            title: qsTr("Not supported")
            messageObjectName: "unsupportedPsbtPopupMessage"

            onClosed: root.wallet.importedPsbt.clear()

            AlertAction {
                text: qsTr("OK")
                buttonObjectName: "unsupportedPsbtPopupOkButton"
            }
        }

        AlertPopup {
            id: knownPsbtTxPopup
            objectName: "knownPsbtTxPopup"
            parent: Overlay.overlay
            title: qsTr("Cannot import transaction")
            message: qsTr("This transaction is already in your wallet spent history. View it in Activity.")
            messageObjectName: "knownPsbtTxPopupMessage"

            property string txid: ""

            onClosed: root.wallet.importedPsbt.clear()

            AlertAction {
                text: qsTr("Close")
                role: AlertAction.Cancel
                buttonObjectName: "knownPsbtTxCloseButton"
            }

            AlertAction {
                text: qsTr("View transaction")
                buttonObjectName: "knownPsbtTxViewButton"
                onTriggered: root.viewTransactionInActivity(knownPsbtTxPopup.txid)
            }
        }

        // URI import state
        property string m_importAdmissionError: ""
        readonly property string paymentRequestStatus: m_importOperation
            ? (m_importOperation.error.length > 0 ? m_importOperation.error
                : m_importOperation.state === PaymentImportOperation.Applied ? qsTr("Payment request imported from %1").arg(m_importSource) : "")
            : m_importAdmissionError
        readonly property bool paymentRequestIsError: m_importOperation ? m_importOperation.error.length > 0 : m_importAdmissionError.length > 0
        readonly property string paymentRequestMessage: m_importOperation
            ? (m_importOperation.state === PaymentImportOperation.Applied ? m_importOperation.message : "") : ""

        // Native clipboard capture and immutable Domain probe outcomes belong
        // to the original wallet, recipient, draft and editor selection.
        readonly property bool showClipboardUriBanner: root.visible && m_clipboardOperation !== null
            && m_clipboardWallet !== null && m_clipboardWallet.available
            && m_clipboardOperation.state === PaymentImportOperation.Inspecting
            && m_clipboardOperation.sourceKind === PaymentImportOperation.PaymentSource
            && m_clipboardOperation.canApply && !m_clipboardOperation.matchesExistingRequest
            && root.wallet === m_clipboardWallet && root.recipient === m_clipboardRecipient
            && root.draftRevision === m_clipboardRevision && root.hiddenCount === m_clipboardHidden
            && Clipboard.revision === m_clipboardCounter
        readonly property string m_pendingClipboardUri: showClipboardUriBanner ? m_clipboardText : ""
        property PaymentImportOperation m_importOperation: null
        property WalletQmlModel m_importWallet: null
        property string m_importSource: ""
        property string m_importPresentedResult: ""
        readonly property string importError: m_importOperation ? m_importOperation.error : ""
        property PaymentImportOperation m_clipboardOperation: null
        property WalletQmlModel m_clipboardWallet: null
        property SendRecipient m_clipboardRecipient: null
        property string m_clipboardRevision: ""
        property int m_clipboardHidden: -1
        property double m_clipboardCounter: -1
        property string m_clipboardText: ""
        readonly property bool uriActionPending: m_importOperation !== null && m_importOperation.pending

        Connections {
            target: sendPage.m_importOperation
            function onChanged() {
                const operation = sendPage.m_importOperation
                if (!operation) return
                if (operation.promptToken) paymentUriOverwritePopup.open()
                sendPage.presentImportedReview()
            }
        }
        function presentImportedReview() {
            const operation = m_importOperation
            if (!operation || operation.state !== PaymentImportOperation.ReviewOpened || !m_importWallet
                    || root.wallet !== m_importWallet || !m_importWallet.available
                    || m_importWallet.importedReviewToken !== operation.resultToken || m_importPresentedResult === operation.resultToken) return
            m_importPresentedResult = operation.resultToken
            root.openTransactionReview()
        }
        function cancelUriAction() {
            const operation = m_importOperation
            m_importOperation = null
            m_importWallet = null
            m_importPresentedResult = ""
            paymentUriOverwritePopup.close()
            if (operation) operation.cancel()
        }
        function cancelUriIntents() {
            cancelUriAction()
            if (m_clipboardOperation) m_clipboardOperation.cancel()
            m_clipboardOperation = null
        }
        // A payment URI pasted into a field of `recipient`. That field's own
        // value is not a conflict when the request replaces it.
        function importPastedUri(field, text, recipient) {
            cancelUriAction()
            if (!root.visible || !root.wallet || !root.wallet.available || !recipient) return
            m_importWallet = root.wallet
            m_importSource = qsTr("clipboard")
            m_importOperation = BitcoinUri.requestImportPaste(text, field, m_importWallet, recipient, sendPage)
            if (m_importOperation) m_importOperation.use()
            else m_importAdmissionError = qsTr("Payment URI services are unavailable. Try again.")
        }
        function checkClipboard() {
            if (m_clipboardOperation) m_clipboardOperation.cancel()
            m_clipboardOperation = null
            if (!root.visible || !root.wallet || !root.wallet.available || !root.recipient || root.wallet.recipients.importPending) return
            m_clipboardWallet = root.wallet
            m_clipboardRecipient = root.recipient
            m_clipboardRevision = root.draftRevision
            m_clipboardHidden = root.hiddenCount
            m_clipboardText = Clipboard.text()
            m_clipboardCounter = Clipboard.revision
            m_clipboardOperation = BitcoinUri.requestImportText(m_clipboardText, m_clipboardWallet, m_clipboardRecipient, sendPage)
        }

        // Parse a URI from text and apply to form.
        function applyPaymentRequestFromText(text, source) {
            cancelUriAction()
            m_importWallet = root.wallet; m_importSource = source
            m_importOperation = BitcoinUri.requestImportText(text, m_importWallet, root.recipient, sendPage)
            if (m_importOperation) m_importOperation.use()
            else { m_importAdmissionError = qsTr("Payment URI services are unavailable. Try again.") }
        }

        function applyPaymentRequestFromFile(path) {
            cancelUriAction()
            m_importWallet = root.wallet; m_importSource = qsTr("file")
            m_importOperation = BitcoinUri.requestImportFile(path, m_importWallet, root.recipient, sendPage)
            if (m_importOperation) m_importOperation.use()
            else { m_importAdmissionError = qsTr("Payment URI services are unavailable. Try again.") }
        }

        AlertPopup {
            id: paymentUriOverwritePopup
            objectName: "sendPaymentUriOverwritePopup"
            parent: Overlay.overlay
            title: qsTr("Replace payment details?")
            message: qsTr("The payment request differs from the current draft. Replace the payment details?")
            messageObjectName: "sendPaymentUriOverwriteMessage"

            AlertAction {
                text: qsTr("Cancel")
                role: AlertAction.Cancel
                buttonObjectName: "sendPaymentUriOverwriteCancelButton"
                closesPopup: false
                onTriggered: {
                    sendPage.cancelUriAction()
                }
            }

            AlertAction {
                text: qsTr("Replace")
                buttonObjectName: "sendPaymentUriOverwriteConfirmButton"
                closesPopup: false
                onTriggered: {
                    if (sendPage.m_importOperation) sendPage.m_importOperation.replace(sendPage.m_importOperation.promptToken)
                    paymentUriOverwritePopup.close()
                }
            }
        }

        Connections {
            target: Clipboard
            function onDataChanged() { sendPage.checkClipboard() }
        }

        // Capture a fresh observation after local input. Public metadata
        // equality is decided by the original target on Domain.
        Connections {
            target: root.recipient.address
            function onAddressChanged() {
                if (root.recipient.address.address === "") {
                    sendPage.m_importAdmissionError = ""
                }
                sendPage.checkClipboard()
            }
        }
        Connections {
            target: root.recipient.amount
            function onAmountChanged() {
                sendPage.checkClipboard()
            }
        }
        Connections {
            target: root.recipient
            function onLabelChanged() { sendPage.checkClipboard() }
            function onMessageChanged() { sendPage.checkClipboard() }
            function onPaymentRequestChanged() { sendPage.checkClipboard() }
        }

        Component.onCompleted: sendPage.checkClipboard()

        Connections {
            target: sendOptionsPopup
            function onOpenPaymentRequest() {
                sendPage.openPaymentRequestImport()
            }
        }

        // Manual URI entry popup
        AlertPopup {
            id: sendUriImportPopup
            objectName: "sendUriImportPopup"
            parent: Overlay.overlay
            title: qsTr("Open payment request")
            initialFocusItem: uriImportInput.field
            implicitWidth: Math.max(420, horizontalActionsWidth + 40)

            content: LabeledTextField {
                id: uriImportInput
                Layout.fillWidth: true
                label: qsTr("Payment request")
                fieldObjectName: "sendUriImportInput"
                placeholderText: qsTr("bitcoin:address?amount=…")
                fieldBackgroundColor: Theme.color.surface2
            }

            AlertAction {
                text: qsTr("Cancel")
                role: AlertAction.Cancel
            }

            AlertAction {
                id: uriImportApply
                text: qsTr("Apply")
                buttonObjectName: "sendUriImportApplyButton"
                enabled: uriImportInput.text.trim().length > 0
                onTriggered: sendPage.applyPaymentRequestFromText(uriImportInput.text, qsTr("manual entry"))
            }
        }

        // Drag-and-drop support
        DropArea {
            objectName: "sendDropArea"
            anchors.fill: parent
            keys: ["text/uri-list", "text/plain"]
            onDropped: (drop) => {
                sendPage.cancelUriAction()
                sendPage.m_importWallet = root.wallet
                sendPage.m_importSource = qsTr("drag and drop")
                sendPage.m_importOperation = BitcoinUri.requestImportDrop(drop.hasUrls ? drop.urls.length : 0,
                    drop.hasUrls && drop.urls.length ? drop.urls[0] : "", drop.hasUrls ? "" : drop.text, root.wallet, root.recipient, sendPage)
                if (sendPage.m_importOperation) sendPage.m_importOperation.use()
            }
        }

        function removeRecipient(index) {
            root.wallet.recipients.removeAt(index)
            expandedRecipient = root.wallet.recipients.currentIndex - 1
        }

        property int expandedRecipient: root.wallet ? root.wallet.recipients.currentIndex - 1 : 0
        readonly property var currentEditor: {
            const revision = root.wallet ? root.wallet.recipients.currentIndex : 0
            const row = recipientRepeater.itemAt(expandedRecipient) as SendRecipientCard
            return row ? row.editorItem : null
        }
        readonly property var addressField: currentEditor ? currentEditor.addressField : null
        readonly property var label: currentEditor ? currentEditor.noteField : null
        readonly property var amountInput: currentEditor ? currentEditor.amountField : null

        ScrollView {
            id: sendScroll
            objectName: "sendFormScrollView"
            anchors.fill: parent
            contentWidth: availableWidth
            contentHeight: formColumn.height + 64
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Item {
                width: sendScroll.availableWidth
                height: formColumn.height + 64
                ColumnLayout {
                    id: formColumn
                    width: Math.max(0, Math.min(1100, sendScroll.availableWidth - sendPage.pageSidePadding * 2))
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top; anchors.topMargin: 28
                    spacing: 24
                    enabled: ApplicationModels.walletController.initialized
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 6
                            //: Main heading of the transaction creation form.
                            CoreText { objectName: "walletSendTitle"; Layout.fillWidth: true; text: qsTr("Send bitcoin"); textStyle: Theme.text.headline; horizontalAlignment: Text.AlignLeft }
                            RowLayout {
                                id: availableBalanceRow
                                Layout.fillWidth: true
                                spacing: 4
                                //: Available wallet balance excludes locked coins.
                                CoreText {
                                    id: availableBalanceCaption
                                    objectName: "sendAvailableCaption"
                                    text: qsTr("Available")
                                    textStyle: Theme.text.description
                                    color: Theme.color.neutral7
                                    horizontalAlignment: Text.AlignLeft
                                    Layout.alignment: Qt.AlignBaseline
                                }
                                BitcoinAmountDisplayLabel {
                                    objectName: "sendAvailableAmount"
                                    amount: availableAmount.localizedDisplay
                                    unit: availableAmount.unitLabel
                                    textStyle: Theme.text.amountDescription
                                    color: Theme.color.neutral7
                                    horizontalAlignment: Text.AlignLeft
                                    Layout.alignment: Qt.AlignBaseline
                                    animationAvailableWidth: Math.max(0, availableBalanceRow.parent.width
                                        - availableBalanceCaption.width - availableBalanceRow.spacing)
                                }
                                BitcoinAmount {
                                    id: availableAmount
                                    satoshi: {
                                        const coinCount = root.wallet ? root.wallet.coinsListModel.coinCount : 0
                                        return root.wallet ? root.wallet.availableSendBalanceSatoshi : 0
                                    }
                                    unit: ApplicationModels.optionsModel.displayUnit
                                }
                            }
                        }
                        OverflowMenuButton {
                            id: menuButton; objectName: "sendOptionsButton"
                            checked: sendOptionsPopup.opened
                            onClicked: sendOptionsPopup.opened ? sendOptionsPopup.close() : sendOptionsPopup.open()
                            SendCreateOptionsPopup {
                                id: sendOptionsPopup
                                x: menuButton.width - width; y: menuButton.height + 6
                                onClearFormRequested: root.resetSendForm()
                                onImportPsbtFromFileRequested: {
                                    if (psbtAutomationPathField.text.length > 0) {
                                        const automatedPath = psbtAutomationPathField.text
                                        psbtAutomationPathField.text = ""
                                        root.requestPsbtImport(automatedPath)
                                    } else sendPage.openPsbtFileImport()
                                }
                            }
                        }
                    }
                    //: Reminder for wallets that sign on a separate hardware device.
                    CoreText { visible: root.externalSignerWallet; Layout.fillWidth: true; text: qsTr("Have your external signer ready to approve this transaction."); horizontalAlignment: Text.AlignLeft; textStyle: Theme.text.description; color: Theme.color.neutral7 }
                    GridLayout {
                        id: sendSections
                        Layout.fillWidth: true
                        columns: width >= SizeClass.wideWidthMin ? 2 : 1
                        columnSpacing: 24
                        rowSpacing: 24

                        FormSection {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: sendSections.columns === 2 ? sendSections.width * 0.6 - 12 : sendSections.width
                            title: root.wallet.recipients.count > 1 ? qsTr("Recipients") : qsTr("Recipient")
                            showBackground: false
                            rowSpacing: 12

                            // Keep changing recipient delegates out of Qt Quick Layouts.
                            // Qt 6.4 can read a freed delegate during a layout pass.
                            Column {
                                id: recipientColumn
                                Layout.fillWidth: true
                                spacing: 12

                                Repeater {
                                    id: recipientRepeater
                                    model: root.wallet.recipients
                                    delegate: SendRecipientCard {
                                        id: recipientDelegate
                                        wallet: root.wallet
                                        width: recipientColumn.width
                                        expanded: sendPage.expandedRecipient === recipientDelegate.index
                                        clipboardRequest: sendPage.showClipboardUriBanner
                                        clipboardText: sendPage.m_pendingClipboardUri
                                        onToggleExpanded: {
                                            const wasExpanded = recipientDelegate.expanded
                                            root.wallet.recipients.setCurrentIndex(recipientDelegate.index)
                                            sendPage.expandedRecipient = wasExpanded ? -1 : recipientDelegate.index
                                        }
                                        onPaymentUriPasted: function(field, text) {
                                            sendPage.importPastedUri(field, text, recipientDelegate.editorItem ? recipientDelegate.editorItem.recipient : root.recipient)
                                        }
                                        onEdited: {
                                            root.draftChanged()
                                            root.clearPrepareTransactionError()
                                            root.scheduleFeeEstimates()
                                        }
                                        onRemoveRequested: sendPage.removeRecipient(recipientDelegate.index)
                                    }
                                }
                            }
                            CoreText {
                                objectName: "sendPaymentRequestStatusText"
                                visible: sendPage.importError.length > 0 || (sendPage.paymentRequestIsError && sendPage.paymentRequestStatus.length > 0)
                                Layout.leftMargin: 16; Layout.rightMargin: 16
                                Layout.fillWidth: true; text: sendPage.importError.length > 0 ? sendPage.importError : sendPage.paymentRequestStatus; horizontalAlignment: Text.AlignLeft; color: Theme.color.dangerText; textStyle: Theme.text.description
                            }

                            NeutralButton {
                                objectName: "sendAddRecipientButton"
                                Layout.fillWidth: true
                                text: qsTr("+ Add another")
                                enabled: root.wallet.recipients.canAddRecipient
                                onClicked: { root.wallet.recipients.add(); sendPage.expandedRecipient = root.wallet.recipients.currentIndex - 1 }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            Layout.preferredWidth: sendSections.columns === 2 ? sendSections.width * 0.4 - 12 : sendSections.width
                            spacing: 24

                            FormSection {
                                objectName: "sendNetworkFeeSection"
                                Layout.fillWidth: true
                                title: qsTr("Network fee")
                                footerText: !root.wallet ? ""
                                    : root.wallet.feeEstimatePending ? qsTr("Estimating…")
                                    : root.wallet.estimatedFeeSatoshi >= 0 ? ""
                                    : root.wallet.recipients.allValid ? qsTr("Fee estimate unavailable")
                                    : qsTr("Enter a valid recipient address and amount to estimate the fee")
                                SendFeeSelection { Layout.fillWidth: true; walletModel: root.wallet; onFeeChanged: root.clearPrepareTransactionError() }
                            }

                            FormSection {
                                Layout.fillWidth: true
                                title: qsTr("Coin control")

                                FormRow {
                                    Layout.fillWidth: true
                                    title: qsTr("Selection")
                                    showDivider: root.manualCoinSelection || root.selectedInputsActive
                                    trailingItem: SegmentedPicker {
                                        objectName: "sendCoinControlPicker"
                                        implicitWidth: 190
                                        model: [qsTr("Automatic"), qsTr("Manual")]
                                        currentIndex: root.manualCoinSelection || root.selectedInputsActive ? 1 : 0
                                        onSelected: function(index, option) {
                                            if (root.wallet && root.wallet.coinsListModel)
                                                root.wallet.coinsListModel.setManualInputSelection(index === 1)
                                        }
                                    }
                                }

                                FormRow {
                                    visible: root.manualCoinSelection || root.selectedInputsActive
                                    Layout.fillWidth: true
                                    title: qsTr("Inputs")
                                    description: root.wallet.coinsListModel.selectedCoinsCount === 0 ? qsTr("No inputs selected")
                                        : root.wallet.coinsListModel.selectedCoinsCount === 1 ? qsTr("1 input selected") : qsTr("%1 inputs selected").arg(root.wallet.coinsListModel.selectedCoinsCount)
                                    showDivider: false
                                    trailingItem: NeutralButton {
                                        objectName: "sendSelectInputsButton"
                                        buttonSize: NeutralButton.Medium
                                        text: qsTr("Select coins")
                                        onClicked: coinSelectionPopup.open()
                                    }
                                }

                                CoreText {
                                    objectName: "sendInputsSelectedText"
                                    visible: false
                                    //: Status in the Send form showing how many transaction inputs are selected.
                                    text: root.manualCoinSelection && root.wallet.coinsListModel.selectedCoinsCount === 0 ? qsTr("No inputs selected")
                                        : root.selectedInputsActive ? (root.wallet.coinsListModel.selectedCoinsCount === 1 ? qsTr("1 input selected") : qsTr("%1 inputs selected").arg(root.wallet.coinsListModel.selectedCoinsCount))
                                        : qsTr("%1 inputs selected automatically").arg(root.wallet.estimatedInputCount)
                                }
                                NeutralButton {
                                    objectName: "sendUseAutomaticInputsButton"
                                    buttonSize: NeutralButton.Large
                                    visible: false
                                    text: qsTr("Use automatic selection")
                                    onClicked: { root.wallet.coinsListModel.setManualInputSelection(false); root.wallet.coinsListModel.update(); root.scheduleFeeEstimates() }
                                }
                            }

                            FormSection {
                                Layout.fillWidth: true
                                title: qsTr("Summary")
                                BitcoinAmount { id: summaryFee; satoshi: Math.max(0, root.wallet.estimatedFeeSatoshi); unit: ApplicationModels.optionsModel.displayUnit }
                                BitcoinAmount { id: summaryTotal; satoshi: root.wallet.sendTotalSatoshi; unit: ApplicationModels.optionsModel.displayUnit }
                                ValueRow { objectName: "sendTotalFeesValue"; Layout.fillWidth: true; amountValue: true; animateUnitChanges: false; title: qsTr("Total fees"); value: !root.wallet.feeEstimatePending && root.wallet.estimatedFeeSatoshi >= 0 ? summaryFee.localizedDisplay : "—"; valueUnit: summaryFee.unitLabel }
                                ValueRow { objectName: "sendTotalAmountValue"; Layout.fillWidth: true; amountValue: true; animateUnitChanges: false; title: qsTr("Total to send"); showDivider: false; value: !root.wallet.feeEstimatePending && root.wallet.estimatedFeeSatoshi >= 0 ? summaryTotal.localizedDisplay : recipientsTotalAmount.localizedDisplay; valueUnit: !root.wallet.feeEstimatePending && root.wallet.estimatedFeeSatoshi >= 0 ? summaryTotal.unitLabel : recipientsTotalAmount.unitLabel; valueTextStyle: Theme.text.amountBody }
                            }

                            CoreText { objectName: "sendPrepareTransactionErrorText"; Layout.fillWidth: true; visible: root.formErrorText.length > 0; text: root.formErrorText; horizontalAlignment: Text.AlignLeft; textStyle: Theme.text.description; color: Theme.color.dangerText }
                            PrimaryButton {
                                objectName: "sendReviewButton"
                                Layout.fillWidth: true
                                text: qsTr("Review transaction")
                                enabled: !sendPage.uriActionPending && root.wallet && root.wallet.available && root.wallet.recipients.allValid && !root.wallet.feeEstimatePending && !root.wallet.preparationBusy && !root.wallet.fees.policyPending && !root.wallet.customFeeTargetPending && !root.wallet.coinSelectionPending && !root.wallet.coinSelectionBlocked
                                    && !root.wallet.sendAmountExhaustsBalance && (!root.wallet.maximumRecipient || root.wallet.fees.errorText.length === 0) && (!root.wallet.customFeeEnabled || root.wallet.customFeeRateValid)
                                onClicked: root.confirmTransactionReview()
                            }
                        }
                    }
                }
            }
        }

        // Automation-only hooks for functional tests. Only loaded when the app
        // is built with -DENABLE_TEST_AUTOMATION=ON (testAutomationEnabled is a
        // C++ context property set at startup). In production builds the Loader
        // is inactive and no hook items exist in the QML object tree.
        Loader {
            active: ApplicationModels.testAutomationEnabled
            anchors.fill: parent
            sourceComponent: Component {
                Item {
                    anchors.fill: parent

                    TextField {
                        id: fileImportPathInput
                        objectName: "sendImportPaymentRequestFilePathInput"
                        visible: false
                    }

                    Button {
                        objectName: "sendApplyPaymentRequestFilePathButton"
                        visible: false
                        onClicked: sendPage.applyPaymentRequestFromFile(fileImportPathInput.text)
                    }

                    TextField {
                        id: dropUriInput
                        objectName: "sendDropUriInput"
                        visible: false
                    }

                    Button {
                        objectName: "sendApplyDropUriButton"
                        visible: false
                        onClicked: sendPage.applyPaymentRequestFromText(dropUriInput.text, qsTr("drag and drop"))
                    }

                    // Exercises the DropArea hasUrls + file:// branch.
                    // Passes the raw URL to applyPaymentRequestFromFile so C++
                    // handles the platform-correct toLocalFile() conversion.
                    TextField {
                        id: dropFileUrlInput
                        objectName: "sendDropFileUrlInput"
                        visible: false
                    }

                    Button {
                        objectName: "sendApplyDropFileUrlButton"
                        visible: false
                        onClicked: {
                            const url = dropFileUrlInput.text
                            if (url.startsWith("file://")) {
                                sendPage.applyPaymentRequestFromFile(url)
                            } else {
                                sendPage.applyPaymentRequestFromText(url, qsTr("drag and drop"))
                            }
                        }
                    }
                }
            }
        }

    }

    AlertPopup {
        id: reviewSweepAlert
        objectName: "sendReviewSweepAlert"
        parent: Overlay.overlay
        property string draftRevision: ""
        //: Confirmation before reviewing a transaction that sends the wallet's entire available balance.
        title: qsTr("Use all available funds?")
        //: %1 is the total recipient amount with its unit, excluding the transaction fee.
        message: qsTr("You’re preparing to send all available funds (%1) from this wallet. Do you want to proceed?").arg(recipientsTotalAmount.displayWithUnit)
        AlertAction {
            //: Stay on the Send form without changing the recipients, amounts, or selected coins.
            text: qsTr("Cancel")
            role: AlertAction.Cancel
            buttonObjectName: "sendReviewSweepCancelButton"
        }
        AlertAction {
            //: Confirm using all available funds before unlocking the wallet and reviewing; this does not send it.
            text: qsTr("Use maximum")
            buttonObjectName: "sendReviewSweepConfirmButton"
            onTriggered: {
                if (reviewSweepAlert.draftRevision === root.draftRevision) root.prepareTransactionForReview()
            }
        }
    }

    SendCoinSelection { id: coinSelectionPopup; parent: Overlay.overlay; wallet: root.wallet }

    WalletPassphrasePopup {
        id: reviewPassphrasePopup
        parent: Overlay.overlay
        popupObjectName: "reviewPassphrasePopup"
        passphraseFieldObjectName: "reviewPassphraseField"
        errorTextObjectName: "reviewPassphraseErrorText"
        cancelButtonObjectName: "reviewPassphraseCancelButton"
        confirmButtonObjectName: "reviewPassphraseConfirmButton"
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter your wallet password to prepare this transaction for review.")
        confirmText: qsTr("Unlock and continue")
        busyConfirmText: qsTr("Unlocking…")
        // The password answers the wallet and draft it was requested for.
        property WalletQmlModel actionWallet: null
        property string actionDraftRevision: ""
        contextValid: root.visible && actionWallet !== null
            && actionWallet === root.wallet && actionWallet.available && actionDraftRevision === root.draftRevision
        onAboutToShow: { actionWallet = root.wallet; actionDraftRevision = root.draftRevision }
        busy: opened && contextValid && actionWallet.preparationBusy
        errorText: contextValid && actionWallet.preparationState === WalletQmlModel.PreparationFailed ? actionWallet.transactionError : ""
        onSubmitted: (passphrase) => {
            if (!opened || !contextValid || actionWallet.preparationBusy) return
            root.preparationWallet = actionWallet
            root.preparationDraftRevision = root.draftRevision
            root.preparationHiddenCount = root.hiddenCount
            root.preparingReview = true
            root.preparationTicket = 0
            root.preparationTicket = actionWallet.requestPrepareTransactionWithPassphrase(passphrase)
            if (!root.preparationTicket) root.preparingReview = false
            else root.handlePreparationOutcome()
        }
    }

}
