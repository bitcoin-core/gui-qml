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

    property WalletQmlModel wallet: walletController.selectedWallet
    property SendRecipient recipient: wallet.recipients.current
    property string prepareTransactionErrorText: ""
    property bool manualCoinSelection: false
    readonly property bool externalSignerWallet: wallet !== null && wallet.hasExternalSigner
    readonly property string recipientValidationError: wallet ? wallet.recipients.validationError : ""
    readonly property bool selectedInputsActive: wallet !== null
        && wallet.coinsListModel !== null
        && wallet.coinsListModel.selectedCoinsCount > 0
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
    readonly property string formErrorText: recipientValidationError.length > 0
        ? recipientValidationError
        : (feeBalanceErrorText.length > 0 ? feeBalanceErrorText : prepareTransactionErrorText)

    signal viewTransactionInActivity(string txid)

    function confirmTransactionReview() {
        if (!root.wallet) return
        if (root.wallet.sendDraftSweepsWallet) reviewSweepAlert.open()
        else root.prepareTransactionForReview()
    }

    function prepareTransactionForReview() {
        root.clearPrepareTransactionError()
        if (root.wallet.prepareTransaction()) root.openTransactionReview()
        else if (root.wallet.transactionNeedsUnlock) {
            reviewPassphrasePopup.errorText = ""
            reviewPassphrasePopup.open()
        } else root.prepareTransactionErrorText = root.wallet.transactionError.length > 0
            ? root.displayTransactionError(root.wallet.transactionError)
            : root.selectedInputsActive ? root.selectedInputsBalanceErrorText : root.availableBalanceErrorText
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
        Qt.callLater(sendPage.openPaymentRequestImport)
    }

    function openPsbtFileImport() {
        root.returnToSendForm()
        Qt.callLater(sendPage.openPsbtFileImport)
    }

    function clearPrepareTransactionError() {
        if (prepareTransactionErrorText.length > 0) {
            prepareTransactionErrorText = ""
        }
    }

    function resetSendForm() {
        if (!root.wallet) return
        root.wallet.recipients.clear()
        root.wallet.clearSelectedCoins()
        root.wallet.coinsListModel.update()
        root.manualCoinSelection = false
        root.clearPrepareTransactionError()
        sendPage.expandedRecipient = 0
        sendPage.paymentRequestStatus = ""
        sendPage.paymentRequestIsError = false
        sendPage.paymentRequestMessage = ""
        sendPage.m_pendingClipboardUri = ""
        sendPage.m_filledUri = ""
        sendPage.m_dismissedUri = ""
        sendPage.showClipboardUriBanner = false
        sendPage.clearPendingPaymentUriPaste()
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
        if (visible) sendPage.checkClipboard()
        else reviewSweepAlert.close()
    }
    onWalletChanged: reviewSweepAlert.close()

    Connections {
        target: walletController
        function onSelectedWalletChanged() {
            if (transactionReviewPopup.opened) {
                transactionReviewPopup.close()
            }
            root.pop()
            // Clear URI import state so stale results from the previous wallet
            // are not shown when the user switches wallets and returns to Send.
            sendPage.paymentRequestStatus = ""
            sendPage.paymentRequestIsError = false
            sendPage.paymentRequestMessage = ""
            sendPage.showClipboardUriBanner = false
            sendPage.m_pendingClipboardUri = ""
            sendPage.m_filledUri = ""
            sendPage.m_dismissedUri = ""
            sendPage.m_applyingUri = false
            sendPage.clearPendingPaymentUriPaste()
            paymentUriOverwritePopup.close()
        }
    }

    Connections {
        target: root.wallet ? root.wallet.recipients : null
        function onListCleared() {
            root.clearPrepareTransactionError()
            sendPage.expandedRecipient = 0
            if (root.wallet) {
                root.wallet.scheduleFeeEstimates()
            }
        }
        function onCountChanged() {
            root.clearPrepareTransactionError()
            root.scheduleFeeEstimates()
        }
        function onCurrentRecipientChanged() {
            root.clearPrepareTransactionError()
            root.scheduleFeeEstimates()
            sendPage.paymentRequestStatus = ""
            sendPage.paymentRequestIsError = false
            sendPage.paymentRequestMessage = ""
            sendPage.expandedRecipient = root.wallet.recipients.currentIndex - 1
            sendPage.checkClipboard()
            sendPage.clearPendingPaymentUriPaste()
            paymentUriOverwritePopup.close()
        }
    }

    Connections {
        target: root.wallet ? root.wallet.coinsListModel : null
        function onSelectedCoinsCountChanged() {
            root.clearPrepareTransactionError()
            root.scheduleFeeEstimates()
        }
    }

    Connections {
        target: root.wallet
        function onCurrentTransactionChanged() { reviewSweepAlert.close() }
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
        value: optionsModel.displayUnit
        when: root.recipient !== null
    }

    BitcoinAmount {
        id: recipientsTotalAmount
        satoshi: root.wallet && root.wallet.recipients
            ? root.wallet.recipients.totalAmountSatoshi
            : 0
        unit: optionsModel.displayUnit
    }

    initialItem: Page {
        id: sendPage
        objectName: "walletSendPage"
        background: null
        readonly property real pageSidePadding: width < 640 ? 16 : 40

        function handlePsbtImportResult(result) {
            sendOptionsPopup.close()
            if (result === WalletQmlModel.WalletCanSign) {
                root.openTransactionReview()
            } else if (result === WalletQmlModel.WalletCannotSign) {
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
            psbtOpenDialog.open()
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
            width: Overlay.overlay.width
            height: Overlay.overlay.height
            onOpened: if (reviewStack.currentItem) reviewStack.currentItem.focusInitialControl()

            enter: Transition {
                NumberAnimation {
                    property: "verticalOffset"
                    from: transactionReviewPopup.height
                    to: 0
                    duration: 350
                    easing.type: Easing.OutCubic
                }
            }
            exit: Transition {
                NumberAnimation {
                    property: "verticalOffset"
                    from: 0
                    to: transactionReviewPopup.height
                    duration: 280
                    easing.type: Easing.InCubic
                }
            }

            property WalletQmlModel reviewWallet: null
            property bool importedReview: false

            onClosed: {
                const wallet = reviewWallet
                const wasImported = importedReview
                reviewStack.pop(null, StackView.Immediate)
                reviewWallet = null
                importedReview = false
                if (wallet && wasImported) {
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
                    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180; easing.type: Easing.OutCubic }
                }
                pushExit: Transition {
                    // Keep Review visible under the fading Complete page until
                    // the Complete background is fully opaque.
                    NumberAnimation { property: "opacity"; from: 1; to: 1; duration: 180 }
                }

                Component {
                    id: reviewPage
                    SendTransactionReview {
                        wallet: transactionReviewPopup.reviewWallet
                        onBack: transactionReviewPopup.close()
                        onTransactionSent: (txid) => {
                            reviewStack.push(sendCompletePage, {
                                "txid": txid,
                                "targetBlocks": !transactionReviewPopup.importedReview && wallet && !wallet.customFeeEnabled
                                    ? wallet.currentTransaction.reviewTargetBlocks : 0
                            })
                        }
                    }
                }

                Component {
                    id: sendCompletePage
                    SendComplete {
                        onDone: {
                            root.resetSendForm()
                            transactionReviewPopup.close()
                        }
                        onViewNewTransaction: (txid) => {
                            root.resetSendForm()
                            transactionReviewPopup.close()
                            root.viewTransactionInActivity(txid)
                        }
                    }
                }
            }
        }

        AppFileDialog {
            id: psbtOpenDialog
            title: qsTr("Import PSBT")
            fileMode: AppFileDialog.OpenFile
            nameFilters: [qsTr("Partially Signed Bitcoin Transactions (*.psbt)"), qsTr("All files (*)")]
            onAccepted: sendPage.handlePsbtImportResult(root.wallet.importPsbtFromFile(selectedFile.toString()))
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
            title: qsTr("Cannot import transaction")
            message: qsTr("This transaction is already in your wallet spent history. View it in Activity")
            messageObjectName: "knownPsbtTxPopupMessage"

            property string txid: ""

            onClosed: root.wallet.importedPsbt.clear()

            AlertAction {
                text: qsTr("View transaction")
                buttonObjectName: "knownPsbtTxViewButton"
                onTriggered: root.viewTransactionInActivity(knownPsbtTxPopup.txid)
            }

            AlertAction {
                text: qsTr("Close")
                role: AlertAction.Cancel
                buttonObjectName: "knownPsbtTxCloseButton"
            }
        }

        // URI import state
        property string paymentRequestStatus: ""
        property bool paymentRequestIsError: false
        property string paymentRequestMessage: ""

        // Clipboard URI detection
        property bool showClipboardUriBanner: false
        // Cache the clipboard text at detection time to avoid a TOCTOU race:
        // the "Fill" button applies this value rather than re-reading the
        // clipboard, which may have changed since the banner appeared.
        property string m_pendingClipboardUri: ""
        // Soft suppress (Fill): URI that was most recently applied to the form.
        // The banner stays hidden while all URI-specified fields still match the
        // form; as soon as any field diverges the banner re-appears.
        property string m_filledUri: ""
        // Hard suppress (Dismiss): URI the user explicitly dismissed.
        // The banner only re-appears when the clipboard contains a different URI.
        property string m_dismissedUri: ""
        // Guard that prevents field-change Connections from triggering a
        // re-check while a programmatic URI fill is writing to the form.
        property bool m_applyingUri: false
        property var m_pendingPastedPaymentRequest: null
        property string m_pendingPastedPaymentRequestText: ""

        function looksLikePaymentUri(text) {
            return String(text).trim().toLowerCase().startsWith("bitcoin:")
        }

        function pasteIntoFocusedRecipientField(field, editor) {
            if (!editor) return
            if (field === "address") {
                editor.addressField.paste()
            } else if (field === "label") {
                editor.noteField.paste()
            } else if (field === "amount") {
                editor.amountField.paste()
            }
        }

        function paymentUriConflicts(result, sourceField) {
            if (!root.recipient) return false

            const addressConflict = sourceField !== "address"
                && root.recipient.address.address.length > 0
                && root.recipient.address.address !== result.address
            const amountConflict = sourceField !== "amount"
                && result.hasAmount
                && root.recipient.amount.satoshi !== 0
                && root.recipient.amount.satoshi !== result.amountSats
            return addressConflict || amountConflict
        }

        function clearPendingPaymentUriPaste() {
            m_pendingPastedPaymentRequest = null
            m_pendingPastedPaymentRequestText = ""
        }

        function applyPastedPaymentRequest(result, text) {
            applyParsedPaymentRequest(result, qsTr("clipboard"))
            if (result.success && Clipboard.text() === text) {
                m_filledUri = text
                showClipboardUriBanner = false
            }
        }

        function handlePaymentUriPaste(text, sourceField) {
            const result = BitcoinUri.parseBitcoinUri(text)
            if (!result.success) {
                applyParsedPaymentRequest(result, qsTr("clipboard"))
                return
            }
            if (paymentUriConflicts(result, sourceField)) {
                m_pendingPastedPaymentRequest = result
                m_pendingPastedPaymentRequestText = text
                paymentUriOverwritePopup.open()
                return
            }
            applyPastedPaymentRequest(result, text)
        }

        function handleClipboardPaste(field, editor) {
            const text = Clipboard.text()
            if (looksLikePaymentUri(text)) {
                handlePaymentUriPaste(text, field)
            } else {
                pasteIntoFocusedRecipientField(field, editor)
            }
        }

        function checkClipboard() {
            // Skip parsing when the Send tab is not visible or no wallet is
            // loaded (recipient fields depend on the wallet being present).
            if (!root.visible || root.wallet === null) {
                showClipboardUriBanner = false
                return
            }

            const text = Clipboard.text()
            const parsed = BitcoinUri.parseBitcoinUri(text)
            if (!parsed.success) {
                showClipboardUriBanner = false
                m_pendingClipboardUri = ""
                return
            }

            // Hard suppress: user dismissed this exact URI.
            // Only lifts when the clipboard changes to a different URI.
            if (text === m_dismissedUri) {
                showClipboardUriBanner = false
                return
            }

            // Soft suppress: user filled this URI. Hide the banner while every
            // field the URI specified still matches its current form value.
            // As soon as any field diverges the banner re-appears automatically.
            if (text === m_filledUri) {
                const filled = BitcoinUri.parseBitcoinUri(m_filledUri)
                const formMatches =
                    root.recipient.address.address === filled.address
                    && (!filled.hasAmount || root.recipient.amount.satoshi === filled.amountSats)
                    && (!filled.hasLabel  || root.recipient.paymentRequestLabel === filled.label)
                if (formMatches) {
                    showClipboardUriBanner = false
                    return
                }
                // Form diverged — lift fill suppression.
                m_filledUri = ""
            }

            m_pendingClipboardUri = text
            showClipboardUriBanner = true
        }

        // Apply a pre-parsed URI result to the current recipient form fields.
        function applyParsedPaymentRequest(result, source) {
            if (!result.success) {
                paymentRequestStatus = result.error
                paymentRequestIsError = true
                return
            }
            m_applyingUri = true
            sendPage.expandedRecipient = root.wallet.recipients.currentIndex - 1
            root.recipient.applyPaymentRequest(result.address, result.hasLabel ? result.label : "", result.hasMessage ? result.uriMessage : "")
            // Only fields present in the URI are applied. Amount is intentionally
            // not cleared when the URI omits 'amount='. This matches Bitcoin Core
            // Qt's URI import behaviour: only populate what the URI specifies.
            if (result.hasAmount) {
                root.recipient.amount.satoshi = result.amountSats
            }
            if (amountInput) amountInput.syncFromAmount(true)
            // Lower the guard only after all writes are done. Field-change
            // Connections must not fire checkClipboard() before the Fill
            // handler has had a chance to set m_filledUri.
            m_applyingUri = false
            root.scheduleFeeEstimates()
            paymentRequestMessage = result.hasMessage ? result.uriMessage : ""
            paymentRequestStatus = qsTr("Payment request imported from %1").arg(source)
            paymentRequestIsError = false
        }

        // Parse a URI from text and apply to form.
        function applyPaymentRequestFromText(text, source) {
            const result = BitcoinUri.parseBitcoinUri(text)
            applyParsedPaymentRequest(result, source)
            // If the applied URI matches what's on the clipboard, treat it like
            // the Fill button: soft-suppress the banner so it stays hidden while
            // the form still reflects what the URI specified.
            if (result.success && Clipboard.text() === text) {
                m_filledUri = text
                showClipboardUriBanner = false
            }
        }

        // Parse a URI from a file path and apply to form.
        function applyPaymentRequestFromFile(path) {
            const result = BitcoinUri.parseBitcoinUriFromFile(path)
            applyParsedPaymentRequest(result, qsTr("file"))
        }

        AlertPopup {
            id: paymentUriOverwritePopup
            objectName: "sendPaymentUriOverwritePopup"
            parent: Overlay.overlay
            title: qsTr("Confirm paste?")
            message: qsTr("The payment request from the clipboard contains information that differs from the currently populated values. Pasting will replace the values.")
            messageObjectName: "sendPaymentUriOverwriteMessage"

            AlertAction {
                text: qsTr("Cancel")
                role: AlertAction.Cancel
                buttonObjectName: "sendPaymentUriOverwriteCancelButton"
                closesPopup: false
                onTriggered: {
                    sendPage.clearPendingPaymentUriPaste()
                    paymentUriOverwritePopup.close()
                }
            }

            AlertAction {
                text: qsTr("Paste")
                buttonObjectName: "sendPaymentUriOverwriteConfirmButton"
                closesPopup: false
                onTriggered: {
                    const result = sendPage.m_pendingPastedPaymentRequest
                    const text = sendPage.m_pendingPastedPaymentRequestText
                    sendPage.clearPendingPaymentUriPaste()
                    paymentUriOverwritePopup.close()
                    if (result) sendPage.applyPastedPaymentRequest(result, text)
                }
            }
        }

        Connections {
            target: Clipboard
            function onDataChanged() { sendPage.checkClipboard() }
        }

        // Re-check the clipboard whenever any form field changes due to user
        // input. checkClipboard() compares current values against the filled URI
        // and re-shows the banner if any specified field has diverged.
        // The m_applyingUri guard prevents these from firing during a programmatic
        // fill, which would trigger a re-check before m_filledUri is set.
        Connections {
            target: root.recipient.address
            function onAddressChanged() {
                if (!sendPage.m_applyingUri) {
                    if (root.recipient.address.address === "") {
                        sendPage.paymentRequestStatus = ""
                        sendPage.paymentRequestIsError = false
                        sendPage.paymentRequestMessage = ""
                    }
                    sendPage.checkClipboard()
                }
            }
        }
        Connections {
            target: root.recipient.amount
            function onAmountChanged() {
                if (!sendPage.m_applyingUri) sendPage.checkClipboard()
            }
        }
        Connections {
            target: root.recipient
            function onLabelChanged() {
                if (!sendPage.m_applyingUri) sendPage.checkClipboard()
            }
        }

        Component.onCompleted: sendPage.checkClipboard()

        Connections {
            target: sendOptionsPopup
            function onOpenPaymentRequest() {
                sendPage.openPaymentRequestImport()
            }
        }

        // Manual URI entry popup
        Popup {
            id: sendUriImportPopup
            objectName: "sendUriImportPopup"
            anchors.centerIn: Overlay.overlay
            width: Math.min(sendPage.width - 40, 420)
            modal: true
            dim: true
            padding: 20

            Overlay.modal: Rectangle {
                color: Qt.rgba(0, 0, 0, 0.5)
            }

            background: Rectangle {
                color: Theme.color.neutral1
                border.color: Theme.color.neutral2
                radius: 5
                border.width: 1
                SurfaceGradientBorder {
                    anchors.fill: parent
                    surfaceColor: parent.color
                    cornerRadius: parent.radius
                }
            }

            contentItem: ColumnLayout {
                spacing: 12

                CoreText {
                    text: qsTr("Open payment request")
                    font: Theme.text.subheading.font
                    color: Theme.color.neutral9
                    Layout.fillWidth: true
                }

                CoreTextField {
                    id: uriImportInput
                    objectName: "sendUriImportInput"
                    Layout.fillWidth: true
                    placeholderText: qsTr("bitcoin:address?amount=…")
                    background: Rectangle {
                        color: Theme.color.neutral2
                        radius: 5
                        border.width: 0
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    OutlineButton {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        text: qsTr("Cancel")
                        onClicked: sendUriImportPopup.close()
                    }

                    ContinueButton {
                        objectName: "sendUriImportApplyButton"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 0
                        text: qsTr("Apply")
                        enabled: uriImportInput.text.trim().length > 0
                        onClicked: {
                            sendUriImportPopup.close()
                            sendPage.applyPaymentRequestFromText(uriImportInput.text, qsTr("manual entry"))
                        }
                    }
                }
            }
        }

        // Drag-and-drop support
        DropArea {
            objectName: "sendDropArea"
            anchors.fill: parent
            keys: ["text/uri-list", "text/plain"]
            onDropped: (drop) => {
                if (drop.hasUrls && drop.urls.length > 0) {
                    const url = drop.urls[0].toString()
                    if (url.startsWith("file://")) {
                        // Pass the raw file:// URL; C++ uses QUrl::toLocalFile()
                        // to derive the correct local path on all platforms.
                        sendPage.applyPaymentRequestFromFile(url)
                    } else {
                        sendPage.applyPaymentRequestFromText(url, qsTr("drag and drop"))
                    }
                } else if (drop.hasText) {
                    sendPage.applyPaymentRequestFromText(drop.text, qsTr("drag and drop"))
                }
            }
        }

        property int expandedRecipient: root.wallet ? root.wallet.recipients.currentIndex - 1 : 0
        readonly property var currentEditor: {
            const revision = root.wallet ? root.wallet.recipients.currentIndex : 0
            const row = recipientRepeater.itemAt(expandedRecipient)
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
            contentHeight: formColumn.implicitHeight + 64
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            Item {
                width: sendScroll.availableWidth
                height: formColumn.implicitHeight + 64
                ColumnLayout {
                    id: formColumn
                    width: Math.max(0, Math.min(1100, sendScroll.availableWidth - sendPage.pageSidePadding * 2))
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top; anchors.topMargin: 28
                    spacing: 24
                    enabled: walletController.initialized
                    RowLayout {
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true; spacing: 6
                            //: Main heading of the transaction creation form.
                            CoreText { objectName: "walletSendTitle"; Layout.fillWidth: true; text: qsTr("Send bitcoin"); font: Theme.text.headline.font; horizontalAlignment: Text.AlignLeft }
                            RowLayout {
                                id: availableBalanceRow
                                Layout.fillWidth: true
                                spacing: 4
                                //: Available wallet balance excludes locked coins.
                                CoreText {
                                    id: availableBalanceCaption
                                    objectName: "sendAvailableCaption"
                                    text: qsTr("Available")
                                    font: Theme.text.description.font
                                    color: Theme.color.neutral7
                                    horizontalAlignment: Text.AlignLeft
                                    Layout.alignment: Qt.AlignBaseline
                                }
                                BitcoinAmountDisplayLabel {
                                    objectName: "sendAvailableAmount"
                                    amount: availableAmount.localizedDisplay
                                    unit: availableAmount.unitLabel
                                    font: Theme.text.description.font
                                    color: Theme.color.neutral7
                                    horizontalAlignment: Text.AlignLeft
                                    Layout.alignment: Qt.AlignBaseline
                                    animationAvailableWidth: Math.max(0, availableBalanceRow.parent.width
                                        - availableBalanceCaption.width - availableBalanceRow.spacing)
                                }
                                BitcoinAmount { id: availableAmount; satoshi: root.wallet ? (root.wallet.coinsListModel.coinCount, root.wallet.availableSendBalanceSatoshi) : 0; unit: optionsModel.displayUnit }
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
                                        sendPage.handlePsbtImportResult(root.wallet.importPsbtFromFile(automatedPath))
                                    } else sendPage.openPsbtFileImport()
                                }
                            }
                        }
                    }
                    //: Reminder for wallets that sign on a separate hardware device.
                    CoreText { visible: root.externalSignerWallet; Layout.fillWidth: true; text: qsTr("Have your external signer ready to approve this transaction."); horizontalAlignment: Text.AlignLeft; font: Theme.text.description.font; color: Theme.color.neutral7 }
                    GridLayout {
                        id: sendSections
                        Layout.fillWidth: true
                        columns: width >= 900 ? 2 : 1
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
                                    delegate: Pane {
                                        id: recipientCard
                                        required property int index
                                        required property var recipientObject
                                        readonly property var walletModel: root.wallet
                                        readonly property var pageController: sendPage
                                        readonly property bool expanded: sendPage.expandedRecipient === index
                                        readonly property var editorItem: editorLoader.item
                                        objectName: "sendRecipientCard_" + index
                                        width: recipientColumn.width
                                        implicitWidth: 0
                                        padding: 12
                                        implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
                                        background: Rectangle {
                                            color: Theme.color.neutral1
                                            radius: 10
                                        }
                                        contentItem: ColumnLayout {
                                            spacing: 12
                                            RowLayout {
                                                visible: root.wallet.recipients.count > 1
                                                Layout.fillWidth: true; spacing: 10
                                                AbstractButton {
                                                    objectName: "sendEditRecipient_" + recipientCard.index
                                                    Layout.fillWidth: true; implicitHeight: 36
                                                    //: Accessible action to expand or collapse one recipient in the stack.
                                                    Accessible.name: qsTr("Edit recipient %1").arg(recipientCard.index + 1)
                                                    onClicked: {
                                                        const wasExpanded = recipientCard.expanded
                                                        root.wallet.recipients.setCurrentIndex(recipientCard.index)
                                                        sendPage.expandedRecipient = wasExpanded ? -1 : recipientCard.index
                                                    }
                                                    contentItem: RowLayout {
                                                        spacing: 10
                                                        Rectangle {
                                                            implicitWidth: 28; implicitHeight: 28; radius: 14
                                                            color: recipientCard.expanded ? Theme.color.orange : Theme.color.neutral3
                                                            CoreText { anchors.centerIn: parent; text: recipientCard.index + 1; font: Theme.text.subheading.font; color: recipientCard.expanded ? "#000000" : Theme.color.neutral9 }
                                                        }
                                                        CoreText {
                                                            Layout.fillWidth: true
                                                            Layout.alignment: Qt.AlignVCenter
                                                            horizontalAlignment: Text.AlignLeft
                                                            wrap: false
                                                            elide: Text.ElideRight
                                                            font: Theme.text.subheading.font
                                                            text: recipientCard.recipientObject.label || recipientCard.recipientObject.paymentRequestLabel || qsTr("Recipient %1").arg(recipientCard.index + 1)
                                                        }
                                                        BitcoinAmountDisplayLabel { visible: !recipientCard.expanded; amount: recipientCard.recipientObject.amount.localizedDisplay; unit: recipientCard.recipientObject.amount.unitLabel; font: Theme.text.monoDescription.font }
                                                        Icon {
                                                            source: "image://images/caret-right"
                                                            rotation: recipientCard.expanded ? 90 : 0
                                                            size: 9
                                                            color: Theme.color.neutral6
                                                            Behavior on rotation { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                                                        }
                                                    }
                                                }
                                            }
                                            Loader {
                                                id: editorLoader
                                                active: recipientCard.expanded || root.wallet.recipients.count === 1
                                                visible: active
                                                Layout.fillWidth: true
                                                sourceComponent: SendRecipientEditor {
                                                    wallet: root.wallet; recipient: recipientCard.recipientObject
                                                    clipboardRequest: sendPage.showClipboardUriBanner
                                                    canRemove: root.wallet.recipients.count > 1
                                                    removeButtonObjectName: "sendRemoveRecipient_" + recipientCard.index
                                                    onPasteRequested: function(field) { sendPage.handleClipboardPaste(field, editorLoader.item) }
                                                    onEdited: { root.clearPrepareTransactionError(); root.scheduleFeeEstimates() }
                                                    onRemoveRequested: {
                                                        const walletModel = recipientCard.walletModel
                                                        const pageController = recipientCard.pageController
                                                        walletModel.recipients.removeAt(recipientCard.index)
                                                        pageController.expandedRecipient = walletModel.recipients.currentIndex - 1
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            CoreText {
                                objectName: "sendPaymentRequestStatusText"
                                visible: sendPage.paymentRequestIsError && sendPage.paymentRequestStatus.length > 0
                                Layout.leftMargin: 16; Layout.rightMargin: 16
                                Layout.fillWidth: true; text: sendPage.paymentRequestStatus; horizontalAlignment: Text.AlignLeft; color: Theme.color.red; font: Theme.text.description.font
                            }

                            NeutralButton {
                                objectName: "sendAddRecipientButton"
                                Layout.fillWidth: true
                                text: qsTr("+ Add another")
                                enabled: root.wallet.recipients.count < 25
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
                                        implicitWidth: 190; implicitHeight: 36
                                        model: [qsTr("Automatic"), qsTr("Manual")]
                                        currentIndex: root.manualCoinSelection || root.selectedInputsActive ? 1 : 0
                                        onSelected: function(index, option) {
                                            root.manualCoinSelection = index === 1
                                            if (index === 0) {
                                                if (root.selectedInputsActive) {
                                                    root.wallet.clearSelectedCoins()
                                                    root.wallet.coinsListModel.update()
                                                    root.scheduleFeeEstimates()
                                                }
                                            }
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
                                    text: root.selectedInputsActive ? (root.wallet.coinsListModel.selectedCoinsCount === 1 ? qsTr("1 input selected") : qsTr("%1 inputs selected").arg(root.wallet.coinsListModel.selectedCoinsCount))
                                        : qsTr("%1 inputs selected automatically").arg(root.wallet.estimatedInputCount)
                                }
                                OutlineButton {
                                    objectName: "sendUseAutomaticInputsButton"
                                    visible: false
                                    text: qsTr("Use automatic selection")
                                    onClicked: { root.wallet.clearSelectedCoins(); root.wallet.coinsListModel.update(); root.scheduleFeeEstimates() }
                                }
                            }

                            FormSection {
                                Layout.fillWidth: true
                                title: qsTr("Summary")
                                BitcoinAmount { id: summaryFee; satoshi: Math.max(0, root.wallet.estimatedFeeSatoshi); unit: optionsModel.displayUnit }
                                BitcoinAmount { id: summaryTotal; satoshi: root.wallet.sendTotalSatoshi; unit: optionsModel.displayUnit }
                                ValueRow { objectName: "sendTotalFeesValue"; Layout.fillWidth: true; amountValue: true; title: qsTr("Total fees"); value: !root.wallet.feeEstimatePending && root.wallet.estimatedFeeSatoshi >= 0 ? summaryFee.localizedDisplay : "—"; valueUnit: summaryFee.unitLabel; valueTextStyle: Theme.text.monoDescription }
                                ValueRow { objectName: "sendTotalAmountValue"; Layout.fillWidth: true; amountValue: true; title: qsTr("Total to send"); showDivider: false; value: !root.wallet.feeEstimatePending && root.wallet.estimatedFeeSatoshi >= 0 ? summaryTotal.localizedDisplay : recipientsTotalAmount.localizedDisplay; valueUnit: !root.wallet.feeEstimatePending && root.wallet.estimatedFeeSatoshi >= 0 ? summaryTotal.unitLabel : recipientsTotalAmount.unitLabel; valueTextStyle: Theme.text.monoBody }
                            }

                            CoreText { objectName: "sendPrepareTransactionErrorText"; Layout.fillWidth: true; visible: root.formErrorText.length > 0; text: root.formErrorText; horizontalAlignment: Text.AlignLeft; font: Theme.text.description.font; color: Theme.color.red }
                            ContinueButton {
                                objectName: "sendReviewButton"
                                Layout.fillWidth: true
                                text: qsTr("Review Transaction")
                                enabled: root.wallet && root.wallet.recipients.allValid && !root.wallet.feeEstimatePending
                                    && !root.wallet.sendAmountExhaustsBalance && (!root.wallet.customFeeEnabled || root.wallet.customFeeRateValid)
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
            active: testAutomationEnabled
            anchors.fill: parent
            sourceComponent: Component {
                Item {
                    anchors.fill: parent

                    CoreTextField {
                        id: fileImportPathInput
                        objectName: "sendImportPaymentRequestFilePathInput"
                        visible: false
                    }

                    Button {
                        objectName: "sendApplyPaymentRequestFilePathButton"
                        visible: false
                        onClicked: sendPage.applyPaymentRequestFromFile(fileImportPathInput.text)
                    }

                    CoreTextField {
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
                    CoreTextField {
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
            onTriggered: root.prepareTransactionForReview()
        }
    }

    SendCoinSelection { id: coinSelectionPopup; wallet: root.wallet }

    WalletPassphrasePopup {
        id: reviewPassphrasePopup
        parent: Overlay.overlay
        width: Math.min(480, root.width - 40)
        popupObjectName: "reviewPassphrasePopup"
        passphraseFieldObjectName: "reviewPassphraseField"
        errorTextObjectName: "reviewPassphraseErrorText"
        cancelButtonObjectName: "reviewPassphraseCancelButton"
        confirmButtonObjectName: "reviewPassphraseConfirmButton"
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter your wallet password to prepare this transaction for review.")
        confirmText: qsTr("Unlock and continue")
        busyConfirmText: qsTr("Unlocking...")
        onSubmitted: (passphrase) => {
            reviewPassphrasePopup.busy = true
            if (root.wallet.prepareTransactionWithPassphrase(passphrase)) {
                reviewPassphrasePopup.busy = false
                reviewPassphrasePopup.close()
                root.openTransactionReview()
                return
            }
            reviewPassphrasePopup.busy = false
            reviewPassphrasePopup.errorText = root.wallet.transactionError
        }
    }
}
