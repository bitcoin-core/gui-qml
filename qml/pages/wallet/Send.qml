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
    objectName: "sendPage"
    vertical: true

    property WalletQmlModel wallet: walletController.selectedWallet
    property SendRecipient recipient: wallet.recipients.current
    property string prepareTransactionErrorText: ""
    readonly property bool externalSignerWallet: wallet !== null && wallet.hasExternalSigner

    signal transactionPrepared(bool multipleRecipientsEnabled)

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
            settings.multipleRecipientsEnabled = false
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
        }
        function onCurrentRecipientChanged() {
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

    initialItem: Page {
        id: sendPage
        objectName: "walletSendPage"
        background: null

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
            paymentRequestMessage = result.hasMessage ? result.uriMessage : ""
            paymentRequestStatus = qsTr("Payment request imported from %1.").arg(source)
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
            Overlay.modal: Rectangle {
                color: Qt.rgba(0.25, 0.25, 0.25, 0.9)
            }

            background: Rectangle {
                color: Theme.color.background
                border.color: Theme.color.neutral3
                border.width: 1
                radius: 8
            }
            contentItem: ColumnLayout {
                spacing: 12

                CoreText {
                    text: qsTr("Payment request")
                    font.pixelSize: 16
                    bold: true
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
            property alias coinControlEnabled: sendOptionsPopup.coinControlEnabled
            property alias multipleRecipientsEnabled: sendOptionsPopup.multipleRecipientsEnabled

            onMultipleRecipientsEnabledChanged: {
                if (!multipleRecipientsEnabled) {
                    root.wallet.recipients.clearToFront()
                } else {
                    root.wallet.recipients.add()
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

                spacing: 10

                enabled: walletController.initialized

                Item {
                    id: titleRow
                    Layout.fillWidth: true
                    Layout.topMargin: 30
                    Layout.bottomMargin: root.externalSignerWallet ? 10 : 20

                    CoreText {
                        id: title
                        objectName: "walletSendTitle"
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Send bitcoin")
                        font.pixelSize: 21
                        color: Theme.color.neutral9
                        bold: true
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
                    }
                }

                CoreText {
                    visible: root.externalSignerWallet
                    Layout.fillWidth: true
                    Layout.bottomMargin: 10
                    horizontalAlignment: Text.AlignLeft
                    wrap: true
                    text: qsTr("Make sure you have your external signer at hand to approve this transaction.")
                    font.pixelSize: 18
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
                                text: qsTr("There's a payment request on your clipboard.")
                                font.pixelSize: 14
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

                            IconButton {
                                objectName: "clipboardUriDismissButton"
                                iconSource: "image://images/cross"
                                iconColor: Theme.color.neutral9
                                size: 28
                                background: null
                                Accessible.name: qsTr("Dismiss")
                                Accessible.role: Accessible.Button
                                onClicked: {
                                    // Hard-suppress: banner only re-appears if the
                                    // clipboard changes to a different URI.
                                    sendPage.m_dismissedUri = sendPage.m_pendingClipboardUri
                                    sendPage.showClipboardUriBanner = false
                                }
                            }
                        }
                    }
                }

                // Payment request message (from URI "message=" field)
                Rectangle {
                    Layout.fillWidth: true
                    visible: sendPage.paymentRequestMessage.length > 0
                    color: Theme.color.neutral1
                    radius: 5
                    implicitHeight: paymentRequestMessageContent.implicitHeight + 20

                    RowLayout {
                        id: paymentRequestMessageContent
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 10
                        spacing: 8

                        Icon {
                            source: "image://images/check"
                            size: 18
                            color: Theme.color.neutral7
                        }

                        CoreText {
                            objectName: "sendPaymentRequestMessageText"
                            text: sendPage.paymentRequestMessage
                            font.pixelSize: 14
                            color: Theme.color.neutral7
                            horizontalAlignment: Text.AlignLeft
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // Payment request import status (success or error)
                Rectangle {
                    Layout.fillWidth: true
                    visible: sendPage.paymentRequestStatus.length > 0
                    color: Theme.color.neutral1
                    radius: 5
                    implicitHeight: paymentRequestStatusContent.implicitHeight + 20

                    RowLayout {
                        id: paymentRequestStatusContent
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.margins: 10
                        spacing: 8

                        Icon {
                            source: sendPage.paymentRequestIsError
                                ? "image://images/alert-filled"
                                : "image://images/circle-green-check"
                            size: 18
                            color: sendPage.paymentRequestIsError ? Theme.color.red : Theme.color.green
                        }

                        CoreText {
                            objectName: "sendPaymentRequestStatusText"
                            text: sendPage.paymentRequestStatus
                            font.pixelSize: 14
                            color: sendPage.paymentRequestIsError ? Theme.color.red : Theme.color.neutral7
                            horizontalAlignment: Text.AlignLeft
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                        }

                        IconButton {
                            objectName: "clearPaymentRequestStatusButton"
                            size: 28
                            iconSource: "image://images/cross"
                            iconColor: Theme.color.neutral9
                            background: null
                            Accessible.name: qsTr("Clear status")
                            Accessible.role: Accessible.Button
                            onClicked: {
                                sendPage.paymentRequestStatus = ""
                                sendPage.paymentRequestIsError = false
                            }
                        }
                    }
                }

                RowLayout {
                    id: selectAndAddRecipients
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    Layout.bottomMargin: 10
                    visible: settings.multipleRecipientsEnabled

                    CoreText {
                        id: selectAndAddRecipientsLabel
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignLeft
                        text: qsTr("Recipient %1 of %2").arg(wallet.recipients.currentIndex).arg(wallet.recipients.count)
                        horizontalAlignment: Text.AlignLeft
                        font.pixelSize: 18
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

                    IconButton {
                        objectName: "sendRecipientRemoveButton"
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: 30
                        size: 30
                        iconSource: "image://images/minus"
                        enabled: wallet.recipients.count > 1
                        onClicked: {
                            wallet.recipients.remove()
                        }
                    }
                }

                Separator {
                    visible: settings.multipleRecipientsEnabled
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

                ColumnLayout {
                    Layout.fillWidth: true

                    Item {
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
                            objectName: "sendAmountInput"
                            anchors.left: amountLabel.right
                            anchors.verticalCenter: parent.verticalCenter
                            leftPadding: 0
                            font.family: "BitcoinCoreSans"
                            font.styleName: "Regular"
                            font.pixelSize: 18
                            color: Theme.color.neutral9
                            placeholderTextColor: enabled ? Theme.color.neutral7 : Theme.color.neutral4
                            background: Item {}
                            placeholderText: root.recipient.amount.unit === BitcoinAmount.SAT ? "0" : "0.00000000"
                            selectByMouse: true
                            text: root.recipient.amount.display
                            onTextChanged: {
                                root.clearPrepareTransactionError()
                                if (text !== root.recipient.amount.display) {
                                    root.recipient.amount.display = text
                                }
                                root.scheduleFeeEstimates()
                            }
                            onTextEdited: root.recipient.amount.display = text
                            onEditingFinished: {
                                root.recipient.amount.format()
                                root.scheduleFeeEstimates()
                            }
                            onActiveFocusChanged: {
                                if (!activeFocus) {
                                    root.recipient.amount.format()
                                    root.scheduleFeeEstimates()
                                }
                            }
                            validator: RegularExpressionValidator {
                                regularExpression: root.recipient.amount.unit === BitcoinAmount.BTC
                                    ? /^(0|[1-9]\d{0,7})(\.\d{0,8})?$/
                                    : /^(0|[1-9]\d{0,15})$/
                            }
                            maximumLength: root.recipient.amount.unit === BitcoinAmount.BTC ? 17 : 16
                        }
                        Item {
                            objectName: "sendAmountUnitToggle"
                            width: unitLabel.width + flipIcon.width
                            height: Math.max(unitLabel.height, flipIcon.height)
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            function click() {
                                root.recipient.amount.flipUnit()
                            }
                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.recipient.amount.flipUnit()
                            }
                            CoreText {
                                id: unitLabel
                                objectName: "sendAmountUnitLabel"
                                anchors.right: flipIcon.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.recipient.amount.unitLabel
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

                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.recipient.amountError.length > 0

                        Icon {
                            source: "image://images/alert-filled"
                            size: 22
                            color: Theme.color.red
                        }

                        CoreText {
                            text: root.recipient.amountError
                            font.pixelSize: 15
                            color: Theme.color.red
                            horizontalAlignment: Text.AlignLeft
                            Layout.fillWidth: true
                        }
                    }
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
                    placeholderText: qsTr("Enter note…")
                    text: root.recipient.label
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

                RowLayout {
                    objectName: "sendFeeIncludedNote"
                    Layout.fillWidth: true
                    visible: root.recipient && root.recipient.subtractFeeFromAmount

                    Icon {
                        source: "image://images/check"
                        size: 18
                        color: Theme.color.green
                    }

                    CoreText {
                        objectName: "sendFeeIncludedNoteText"
                        Layout.fillWidth: true
                        text: qsTr("Fee is included in the amount")
                        font.pixelSize: 15
                        color: Theme.color.neutral7
                        horizontalAlignment: Text.AlignLeft
                    }
                }

                Separator {
                    Layout.fillWidth: true
                }

                RowLayout {
                    objectName: "sendPrepareTransactionError"
                    Layout.fillWidth: true
                    visible: root.prepareTransactionErrorText.length > 0

                    Icon {
                        source: "image://images/alert-filled"
                        size: 22
                        color: Theme.color.red
                    }

                    CoreText {
                        objectName: "sendPrepareTransactionErrorText"
                        text: root.prepareTransactionErrorText
                        font.pixelSize: 15
                        color: Theme.color.red
                        horizontalAlignment: Text.AlignLeft
                        Layout.fillWidth: true
                    }
                }

                ContinueButton {
                    id: continueButton
                    objectName: "sendReviewButton"
                    Layout.fillWidth: true
                    Layout.topMargin: 30
                    text: root.externalSignerWallet ? qsTr("Review transaction") : qsTr("Review")
                    enabled: root.recipient.isValid
                        && (!root.wallet || !root.wallet.customFeeEnabled || root.wallet.customFeeRateValid)
                    onClicked: {
                        root.clearPrepareTransactionError()
                        if (root.wallet.prepareTransaction()) {
                            root.transactionPrepared(settings.multipleRecipientsEnabled)
                        } else if (root.wallet.transactionNeedsUnlock) {
                            reviewPassphrasePopup.errorText = ""
                            reviewPassphrasePopup.open()
                        } else {
                            root.prepareTransactionErrorText = root.wallet.transactionError.length > 0
                                ? root.wallet.transactionError
                                : qsTr("Amount plus fee exceeds available balance")
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
                root.transactionPrepared(settings.multipleRecipientsEnabled)
                return
            }
            reviewPassphrasePopup.busy = false
            reviewPassphrasePopup.errorText = root.wallet.transactionError
        }
    }
}
