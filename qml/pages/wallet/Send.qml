// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtCore
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
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
    readonly property bool externalSignerWallet: wallet !== null && wallet.hasExternalSigner
    readonly property string recipientValidationError: wallet ? wallet.recipients.validationError : ""
    readonly property bool selectedInputsActive: wallet !== null
        && wallet.coinsListModel !== null
        && wallet.coinsListModel.selectedCoinsCount > 0
    readonly property string availableBalanceErrorText: qsTr("Amount plus fee exceeds available balance")
    readonly property string selectedInputsBalanceErrorText: qsTr("Selected inputs do not cover the amount plus fee")
    readonly property string feeBalanceErrorText: wallet && wallet.sendAmountExhaustsBalance
        ? (selectedInputsActive ? selectedInputsBalanceErrorText : availableBalanceErrorText)
        : ""
    readonly property string formErrorText: recipientValidationError.length > 0
        ? recipientValidationError
        : (feeBalanceErrorText.length > 0 ? feeBalanceErrorText : prepareTransactionErrorText)

    signal transactionPrepared(bool multipleRecipientsEnabled)
    signal viewTransactionInActivity(string txid)

    function clearPrepareTransactionError() {
        if (prepareTransactionErrorText.length > 0) {
            prepareTransactionErrorText = ""
        }
    }

    function scheduleFeeEstimates() {
        if (root.wallet) {
            root.wallet.scheduleFeeEstimates()
        }
    }

    // Re-check the clipboard whenever the Send tab becomes visible so the
    // banner appears even if the URI was copied before navigating here.
    onVisibleChanged: if (visible) sendPage.checkClipboard()

    Connections {
        target: walletController
        function onSelectedWalletChanged() {
            if (reviewOnlyPsbtPopup.opened) {
                reviewOnlyPsbtPopup.close()
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
        }
    }

    Connections {
        target: root.wallet ? root.wallet.recipients : null
        function onListCleared() {
            root.clearPrepareTransactionError()
            sendOptionsPopup.multipleRecipientsEnabled = false
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

        function handlePsbtImportResult(result) {
            sendOptionsPopup.close()
            if (result === WalletQmlModel.WalletCanSign) {
                const multipleRecipientsEnabled = root.wallet.recipients.count > 1
                sendOptionsPopup.multipleRecipientsEnabled = multipleRecipientsEnabled
                root.transactionPrepared(multipleRecipientsEnabled)
            } else if (result === WalletQmlModel.WalletCannotSign) {
                reviewOnlyPsbtPopup.reviewWallet = root.wallet
                reviewOnlyPsbtPopup.open()
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

        Popup {
            id: reviewOnlyPsbtPopup
            objectName: "reviewOnlyPsbtPopup"
            parent: Overlay.overlay
            x: 0
            y: 0
            modal: true
            closePolicy: Popup.NoAutoClose
            padding: 0
            width: Overlay.overlay.width
            height: Overlay.overlay.height

            property WalletQmlModel reviewWallet: null
            property bool broadcastSucceeded: false

            onClosed: {
                const wallet = reviewWallet
                const showBroadcastSuccess = broadcastSucceeded
                reviewWallet = null
                broadcastSucceeded = false
                if (wallet) {
                    wallet.discardCurrentTransaction()
                }
                if (showBroadcastSuccess) {
                    broadcastSuccessPopup.open()
                }
            }

            background: Rectangle {
                color: Theme.color.background
            }

            contentItem: SendReview {
                inspectionMode: true
                width: reviewOnlyPsbtPopup.availableWidth
                height: reviewOnlyPsbtPopup.availableHeight
                wallet: reviewOnlyPsbtPopup.reviewWallet
                onBack: reviewOnlyPsbtPopup.close()
                onTransactionSent: {
                    reviewOnlyPsbtPopup.broadcastSucceeded = true
                    reviewOnlyPsbtPopup.close()
                }
            }
        }

        AlertPopup {
            id: broadcastSuccessPopup
            objectName: "psbtBroadcastSuccessPopup"
            title: qsTr("Transaction broadcast")
            message: qsTr("The transaction was submitted to the Bitcoin network.")
            messageObjectName: "psbtBroadcastSuccessMessage"

            AlertAction {
                text: qsTr("OK")
                buttonObjectName: "psbtBroadcastSuccessOkButton"
            }
        }

        FileDialog {
            id: psbtOpenDialog
            title: qsTr("Import PSBT")
            fileMode: FileDialog.OpenFile
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
                    && (!filled.hasLabel  || root.recipient.label === filled.label)
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
            root.recipient.address.setAddress(result.address, 0)
            // Only fields present in the URI are applied. Amount is intentionally
            // not cleared when the URI omits 'amount='. This matches Bitcoin Core
            // Qt's URI import behaviour: only populate what the URI specifies.
            if (result.hasAmount) {
                root.recipient.amount.satoshi = result.amountSats
            }
            if (result.hasLabel) {
                root.recipient.label = result.label
            }
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
                uriImportInput.text = ""
                sendUriImportPopup.open()
            }
        }

        // Manual URI entry popup
        Popup {
            id: sendUriImportPopup
            objectName: "sendUriImportPopup"
            anchors.centerIn: Overlay.overlay
            width: Math.min(sendPage.width - 40, 420)
            modal: true
            padding: 20

            background: Item {
                anchors.fill: parent
                Rectangle {
                    color: Theme.color.neutral0
                    border.color: Theme.color.neutral4
                    radius: 5
                    border.width: 1
                    anchors.fill: parent
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
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    OutlineButton {
                        Layout.fillWidth: true
                        text: qsTr("Cancel")
                        onClicked: sendUriImportPopup.close()
                    }

                    ContinueButton {
                        objectName: "sendUriImportApplyButton"
                        Layout.fillWidth: true
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

        Settings {
            id: settings
            property bool coinControlEnabled: false
            property bool multipleRecipientsEnabled: false
        }

        Connections {
            target: sendOptionsPopup

            function onMultipleRecipientsEnabledChanged() {
                settings.multipleRecipientsEnabled = sendOptionsPopup.multipleRecipientsEnabled
                if (!sendOptionsPopup.multipleRecipientsEnabled) {
                    root.wallet.recipients.clearToFront()
                } else if (root.wallet.recipients.count === 1) {
                    root.wallet.recipients.add()
                }
            }

            function onCoinControlEnabledChanged() {
                settings.coinControlEnabled = sendOptionsPopup.coinControlEnabled
                if (sendOptionsPopup.coinControlEnabled && root.wallet) {
                    root.wallet.coinsListModel.update()
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
                width: 520
                anchors.horizontalCenter: parent.horizontalCenter

                readonly property int rowHeight: 56

                spacing: 0

                enabled: walletController.initialized

                Item {
                    id: titleRow
                    Layout.fillWidth: true
                    Layout.preferredHeight: title.implicitHeight
                    Layout.topMargin: 36
                    Layout.bottomMargin: root.externalSignerWallet ? 24 : 36

                    CoreText {
                        id: title
                        objectName: "walletSendTitle"
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Send bitcoin")
                        font: Theme.text.subtitle.font
                        lineHeight: Theme.text.subtitle.lineHeight
                        lineHeightMode: Text.FixedHeight
                        color: Theme.color.neutral9
                    }

                    IconButton {
                        id: menuButton
                        objectName: "sendOptionsButton"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        checked: sendOptionsPopup.opened
                        iconSource: "image://images/ellipsis"
                        onClicked: {
                            if (sendOptionsPopup.opened) {
                                sendOptionsPopup.close()
                            } else {
                                sendOptionsPopup.open()
                            }
                        }
                    }

                    SendOptionsPopup {
                        id: sendOptionsPopup
                        x: menuButton.x - width + menuButton.width
                        y: menuButton.y + menuButton.height
                        coinControlEnabled: settings.coinControlEnabled
                        multipleRecipientsEnabled: settings.multipleRecipientsEnabled
                        onClearFormRequested: {
                            sendOptionsPopup.close()
                            root.wallet.recipients.clear()
                        }
                        onImportPsbtFromFileRequested: {
                            if (psbtAutomationPathField.text.length > 0) {
                                const automatedPath = psbtAutomationPathField.text
                                psbtAutomationPathField.text = ""
                                sendPage.handlePsbtImportResult(root.wallet.importPsbtFromFile(automatedPath))
                                return
                            }
                            sendOptionsPopup.close()
                            psbtOpenDialog.open()
                        }
                    }
                }

                CoreText {
                    visible: root.externalSignerWallet
                    Layout.fillWidth: true
                    Layout.bottomMargin: 16
                    horizontalAlignment: Text.AlignLeft
                    wrap: true
                    text: qsTr("Make sure you have your external signer at hand to approve this transaction.")
                    font: Theme.text.body.font
                    lineHeight: Theme.text.body.lineHeight
                    lineHeightMode: Text.FixedHeight
                    color: Theme.color.neutral7
                }

                // Clipboard URI detection card
                Rectangle {
                    objectName: "clipboardUriBanner"
                    Layout.fillWidth: true
                    visible: sendPage.showClipboardUriBanner
                    color: Theme.color.neutral1
                    radius: 5
                    implicitHeight: clipboardBannerContent.implicitHeight + 20

                    ColumnLayout {
                        id: clipboardBannerContent
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 10
                        spacing: 10

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12

                            CoreText {
                                text: qsTr("You have a Bitcoin invoice in your clipboard.")
                                font: Theme.text.description.font
                                color: Theme.color.neutral9
                                horizontalAlignment: Text.AlignLeft
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                            }

                            ContinueButton {
                                objectName: "clipboardUriPasteButton"
                                text: qsTr("Fill")
                                bold: false
                                textFontPixelSize: 14
                                implicitHeight: 38
                                Layout.preferredWidth: 90
                                onClicked: {
                                    sendPage.showClipboardUriBanner = false
                                    // Use the cached URI captured when the banner appeared,
                                    // not Clipboard.text(), to avoid applying a different URI
                                    // if the clipboard changed before the user clicked Fill.
                                    sendPage.applyPaymentRequestFromText(sendPage.m_pendingClipboardUri, qsTr("clipboard"))
                                    // Soft-suppress: banner stays hidden while the form
                                    // still reflects what the URI specified.
                                    sendPage.m_filledUri = sendPage.m_pendingClipboardUri
                                }
                            }

                            OutlineButton {
                                objectName: "clipboardUriDismissButton"
                                text: qsTr("Dismiss")
                                bold: false
                                textFontPixelSize: 14
                                implicitHeight: 38
                                Layout.preferredWidth: 90
                                onClicked: {
                                    // Hard-suppress: banner only reappears if the
                                    // clipboard changes to a different URI.
                                    sendPage.m_dismissedUri = sendPage.m_pendingClipboardUri
                                    sendPage.showClipboardUriBanner = false
                                }
                            }
                        }
                    }
                }

                // Payment request status (title) + URI "message=" field (body)
                InfoBanner {
                    objectName: "sendPaymentRequestMessageBanner"
                    Layout.fillWidth: true
                    visible: sendPage.paymentRequestMessage.length > 0
                        || (!sendPage.paymentRequestIsError && sendPage.paymentRequestStatus.length > 0)
                    title: sendPage.paymentRequestIsError ? "" : sendPage.paymentRequestStatus
                    message: sendPage.paymentRequestMessage
                    messageObjectName: "sendPaymentRequestMessageText"
                    showsCloseButton: true
                    contentMargin: 16
                    onDismissClicked: {
                        sendPage.paymentRequestMessage = ""
                        if (!sendPage.paymentRequestIsError) {
                            sendPage.paymentRequestStatus = ""
                        }
                    }
                }

                // Payment request import error (success is shown in the InfoBanner above)
                ToastBanner {
                    Layout.fillWidth: true
                    visible: sendPage.paymentRequestIsError && sendPage.paymentRequestStatus.length > 0
                    backgroundColor: Theme.color.red
                    iconSource: "image://images/alert-filled"
                    text: sendPage.paymentRequestStatus
                    textObjectName: "sendPaymentRequestStatusText"
                    showsCloseButton: true
                    onDismissed: {
                        sendPage.paymentRequestStatus = ""
                        sendPage.paymentRequestIsError = false
                    }
                }

                RowLayout {
                    id: selectAndAddRecipients
                    objectName: "sendMultipleRecipientsRow"
                    Layout.fillWidth: true
                    Layout.preferredHeight: columnLayout.rowHeight
                    spacing: 8
                    visible: sendOptionsPopup.multipleRecipientsEnabled

                    CoreText {
                        id: selectAndAddRecipientsLabel
                        text: qsTr("Recipient %1 of %2").arg(wallet.recipients.currentIndex).arg(wallet.recipients.count)
                        horizontalAlignment: Text.AlignLeft
                        font: Theme.text.body.font
                        lineHeight: Theme.text.body.lineHeight
                        lineHeightMode: Text.FixedHeight
                        color: Theme.color.neutral9
                    }

                    IconButton {
                        objectName: "sendRecipientPrevButton"
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        size: 30
                        iconSource: "image://images/caret-left"
                        enabled: wallet.recipients.currentIndex - 1 > 0
                        onClicked: {
                            wallet.recipients.prev()
                        }
                    }

                    IconButton {
                        objectName: "sendRecipientNextButton"
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        size: 30
                        iconSource: "image://images/caret-right"
                        enabled: wallet.recipients.currentIndex < wallet.recipients.count
                        onClicked: {
                            wallet.recipients.next()
                        }
                    }

                    IconButton {
                        objectName: "sendRecipientRemoveButton"
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        size: 30
                        iconSource: "image://images/cross"
                        enabled: wallet.recipients.count > 1
                        onClicked: {
                            wallet.recipients.remove()
                        }
                    }

                    Item { Layout.fillWidth: true }

                    IconButton {
                        objectName: "sendRecipientAddButton"
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        size: 30
                        iconSource: "image://images/plus-big-filled"
                        enabled: wallet.recipients.count < 25
                        onClicked: {
                            wallet.recipients.add()
                        }
                    }
                }

                Separator {
                    visible: sendOptionsPopup.multipleRecipientsEnabled
                    Layout.fillWidth: true
                }

                BitcoinAddressInputField {
                    objectName: "sendAddressField"
                    Layout.fillWidth: true
                    inputObjectName: "sendAddressInput"
                    enabled: walletController.initialized
                    address: root.recipient.address
                    errorText: root.recipient.addressError
                    onTextChanged: {
                        root.clearPrepareTransactionError()
                        root.scheduleFeeEstimates()
                    }
                    onEditingFinished: root.scheduleFeeEstimates()
                }

                Separator {
                    Layout.fillWidth: true
                }

                LabeledTextInput {
                    id: label
                    objectName: "sendNoteField"
                    inputObjectName: "sendNoteInput"
                    Layout.fillWidth: true
                    labelText: qsTr("Note to self")
                    placeholderText: qsTr("Recommended")
                    text: root.recipient.label
                    onTextEdited: root.recipient.label = label.text
                }

                Separator {
                    Layout.fillWidth: true
                }

                BitcoinAmountInputField {
                    id: amountInput
                    Layout.fillWidth: true
                    inputObjectName: "sendAmountInput"
                    unitToggleObjectName: "sendAmountUnitToggle"
                    unitLabelObjectName: "sendAmountUnitLabel"
                    errorTextObjectName: "sendAmountErrorText"
                    amount: root.recipient ? root.recipient.amount : null
                    errorText: root.recipient ? root.recipient.amountError : ""
                    onInputTextChanged: {
                        root.clearPrepareTransactionError()
                        root.scheduleFeeEstimates()
                    }
                    onTextEdited: root.clearPrepareTransactionError()
                    onEditingFinished: root.scheduleFeeEstimates()
                }

                Separator {
                    visible: !sendOptionsPopup.multipleRecipientsEnabled
                    Layout.fillWidth: true
                }

                Item {
                    id: transactionSectionHeader
                    objectName: "sendTransactionSectionHeader"
                    Layout.fillWidth: true
                    Layout.topMargin: 24
                    Layout.bottomMargin: 8
                    Layout.preferredHeight: transactionSectionTitle.implicitHeight
                    visible: sendOptionsPopup.multipleRecipientsEnabled

                    CoreText {
                        id: transactionSectionTitle
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Transaction")
                        font: Theme.text.heading.font
                        lineHeight: Theme.text.heading.lineHeight
                        lineHeightMode: Text.FixedHeight
                        color: Theme.color.neutral9
                    }
                }

                Separator {
                    visible: sendOptionsPopup.multipleRecipientsEnabled
                    Layout.fillWidth: true
                }

                LabeledCoinControlButton {
                    objectName: "sendCoinControlButton"
                    valueObjectName: "sendCoinControlButtonText"
                    visible: sendOptionsPopup.coinControlEnabled
                    Layout.fillWidth: true
                    coinsSelected: wallet.coinsListModel.selectedCoinsCount
                    coinCount: wallet.coinsListModel.coinCount
                    onOpenCoinControl: {
                        root.wallet.coinsListModel.update()
                        root.push(coinSelectionPage)
                    }
                }

                Separator {
                    visible: sendOptionsPopup.coinControlEnabled
                    Layout.fillWidth: true
                }

                FeeSelection {
                    id: feeSelection
                    Layout.fillWidth: true
                    walletModel: root.wallet
                    includeFeeInAmount: root.recipient ? root.recipient.subtractFeeFromAmount : false
                    currentTarget: root.wallet ? root.wallet.targetBlocks : 2

                    onFeeChanged: function(target) {
                        root.clearPrepareTransactionError()
                        if (root.wallet) {
                            root.wallet.targetBlocks = target
                        }
                    }

                    onIncludeFeeInAmountToggled: function(checked) {
                        root.clearPrepareTransactionError()
                        if (root.recipient && root.recipient.subtractFeeFromAmount !== checked) {
                            root.recipient.subtractFeeFromAmount = checked
                            root.scheduleFeeEstimates()
                        }
                    }
                }

                Separator {
                    Layout.fillWidth: true
                }

                Item {
                    id: totalAmountRow
                    objectName: "sendTotalAmountRow"
                    Layout.fillWidth: true
                    Layout.preferredHeight: columnLayout.rowHeight
                    visible: sendOptionsPopup.multipleRecipientsEnabled

                    CoreText {
                        id: totalAmountLabel
                        width: 128
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignLeft
                        text: qsTr("Total amount")
                        font: Theme.text.body.font
                        lineHeight: Theme.text.body.lineHeight
                        lineHeightMode: Text.FixedHeight
                    }

                    CoreText {
                        objectName: "sendTotalAmountValue"
                        anchors.left: totalAmountLabel.right
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignLeft
                        text: recipientsTotalAmount.displayWithUnit
                        font: Theme.text.body.font
                        lineHeight: Theme.text.body.lineHeight
                        lineHeightMode: Text.FixedHeight
                        color: Theme.color.neutral9
                    }
                }

                Separator {
                    visible: sendOptionsPopup.multipleRecipientsEnabled
                    Layout.fillWidth: true
                }

                RowLayout {
                    objectName: "sendFeeIncludedNote"
                    Layout.fillWidth: true
                    Layout.topMargin: visible ? 12 : 0
                    visible: root.recipient && root.recipient.subtractFeeFromAmount
                    spacing: 8

                    Icon {
                        source: "image://images/check"
                        size: 18
                        color: Theme.color.neutral7
                    }

                    CoreText {
                        objectName: "sendFeeIncludedNoteText"
                        Layout.fillWidth: true
                        text: qsTr("Fee is included in the amount")
                        font: Theme.text.description.font
                        lineHeight: Theme.text.description.lineHeight
                        lineHeightMode: Text.FixedHeight
                        color: Theme.color.neutral7
                        horizontalAlignment: Text.AlignLeft
                    }
                }

                RowLayout {
                    objectName: "sendPrepareTransactionError"
                    Layout.fillWidth: true
                    Layout.topMargin: visible ? 12 : 0
                    visible: root.formErrorText.length > 0
                    spacing: 8

                    Icon {
                        source: "image://images/alert-filled"
                        size: 22
                        color: Theme.color.red
                    }

                    CoreText {
                        objectName: "sendPrepareTransactionErrorText"
                        text: root.formErrorText
                        font: Theme.text.description.font
                        lineHeight: Theme.text.description.lineHeight
                        lineHeightMode: Text.FixedHeight
                        color: Theme.color.red
                        horizontalAlignment: Text.AlignLeft
                        Layout.fillWidth: true
                    }
                }

                ContinueButton {
                    id: continueButton
                    objectName: "sendReviewButton"
                    Layout.fillWidth: true
                    Layout.topMargin: 36
                    Layout.bottomMargin: 24
                    text: root.externalSignerWallet ? qsTr("Review transaction") : qsTr("Review")
                    enabled: root.wallet
                        && root.wallet.recipients.allValid
                        && !root.wallet.sendAmountExhaustsBalance
                        && (!root.wallet.customFeeEnabled || root.wallet.customFeeRateValid)
                    onClicked: {
                        root.clearPrepareTransactionError()
                        if (root.wallet.prepareTransaction()) {
                            root.transactionPrepared(sendOptionsPopup.multipleRecipientsEnabled)
                        } else if (root.wallet.transactionNeedsUnlock) {
                            reviewPassphrasePopup.errorText = ""
                            reviewPassphrasePopup.open()
                        } else {
                            root.prepareTransactionErrorText = root.wallet.transactionError.length > 0
                                ? root.wallet.transactionError
                                : (root.selectedInputsActive ? root.selectedInputsBalanceErrorText : root.availableBalanceErrorText)
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

    Component {
        id: coinSelectionPage
        CoinSelection {
            onDone: root.pop()
        }
    }

    WalletPassphrasePopup {
        id: reviewPassphrasePopup
        parent: Overlay.overlay
        width: Math.min(420, root.width - 40)
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
                root.transactionPrepared(sendOptionsPopup.multipleRecipientsEnabled)
                return
            }
            reviewPassphrasePopup.busy = false
            reviewPassphrasePopup.errorText = root.wallet.transactionError
        }
    }
}
