// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"
import "../pages/wallet"

Pane {
    id: root
    objectName: "paymentRequestCard"
    property var wallet
    property var request
    property bool modalView: false
    property bool showAmount: true
    property bool showMessage: true
    property bool showRecipientName: false
    property var clipboard: Clipboard
    readonly property int amountUnit: optionsModel.displayUnit
    property string amountErrorText: ""
    property string actionErrorText: ""
    property string addressErrorText: ""
    readonly property string errorText: amountErrorText || actionErrorText || addressErrorText
    onAmountUnitChanged: {
        if (amountInput && amountInSatoshis(amountInput.text) >= 0) amountErrorText = ""
    }
    readonly property bool saved: !!request && request.id !== ""
    readonly property bool paymentReceived: !!request && request.paymentReceived
    readonly property bool sharing: visible && saved && !paymentReceived && !!wallet
        && !wallet.receiveRequestReconciliationPending
    readonly property bool hasFields: amountInput.text.trim() !== "" || labelInput.text.trim() !== ""
        || messageInput.text.trim() !== "" || noteInput.text.trim() !== ""
    readonly property bool modified: amountInput.modified || labelInput.modified || messageInput.modified || noteInput.modified
    readonly property bool hasPublicFields: showAmount || showMessage || showRecipientName
    onShowAmountChanged: if (!showAmount) clearPublicField("amount", amountInput)
    onShowMessageChanged: if (!showMessage) clearPublicField("message", messageInput)
    onShowRecipientNameChanged: if (!showRecipientName) clearPublicField("label", labelInput)
    signal retryAddressRequested()
    signal requestAgain(string requestId)
    signal created(string requestId)
    signal closeRequested()
    signal deleted(string requestId)

    padding: modalView ? (width < 480 ? 16 : 24) : 0
    implicitWidth: 560
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    background: Rectangle {
        visible: root.modalView
        radius: Theme.radius.sheet
        color: Theme.color.surface1
        border.width: 1
        border.color: Theme.color.surfaceDivider
        SurfaceGradientBorder {
            anchors.fill: parent
            surfaceColor: parent.color
            cornerRadius: parent.radius
        }
    }

    function resetFields() {
        amountErrorText = ""
        actionErrorText = ""
        amountInput.reset()
        labelInput.reset()
        messageInput.reset()
        noteInput.reset()
    }
    function clearForm() {
        if (!request || saved) return
        request.amount.satoshi = 0
        request.label = ""
        request.message = ""
        request.noteSelf = ""
        resetFields()
    }
    function clearPublicField(name, field) {
        if (!request || saved || modalView || !field) return
        if (name === "amount") {
            request.amount.satoshi = 0
            amountErrorText = ""
        } else request[name] = ""
        field.reset()
    }
    function revealTemplateFields() {
        if (!request || saved || !request.isEditing) return
        if (request.amount.satoshi > 0) showAmount = true
        if (request.message.trim() !== "") showMessage = true
        if (request.label.trim() !== "") showRecipientName = true
    }
    onRequestChanged: {
        resetFields()
        if (request && request.label.trim() !== "") showRecipientName = true
        revealTemplateFields()
    }
    Component.onCompleted: resetFields()

    function amountInSatoshis(text) {
        if (text === "") return 0
        if (!/^\d*(\.\d*)?$/.test(text) || text === ".") return -1
        const decimals = amountUnit === BitcoinAmount.SAT ? 0
            : amountUnit === BitcoinAmount.MilliBTC ? 5 : amountUnit === BitcoinAmount.MicroBTC ? 2 : 8
        const parts = text.split(".")
        const fraction = parts.length > 1 ? parts[1] : ""
        if (fraction.length > decimals) return -1
        // Build the integer directly to avoid rounding a decimal BTC amount.
        const sats = Number((parts[0] || "0") + fraction.padEnd(decimals, "0"))
        return Number.isSafeInteger(sats) && sats <= 2100000000000000 ? sats : -1
    }
    function saveField(name, field) {
        if (!request || saved || !field.modified) return true
        const value = name === "amount" ? amountInSatoshis(field.text) : field.text
        if (name === "amount" && value < 0) {
            amountErrorText = amountUnit === BitcoinAmount.SAT
                ? qsTr("Enter a whole number of sats, up to 2,100,000,000,000,000.")
                : qsTr("Enter a valid amount, up to 21,000,000 BTC.")
            return false
        }
        if (name === "amount") request.amount.satoshi = value
        else request[name] = value
        field.modified = false
        actionErrorText = ""
        return true
    }
    function saveFields() {
        if (saved) {
            const amount = paymentReceived ? request.amount.satoshi : amountInSatoshis(amountInput.text)
            if (amount < 0) { amountErrorText = qsTr("Enter a valid amount."); return false }
            amountErrorText = ""
            if (!wallet.updatePaymentRequest(request.id, amount,
                paymentReceived ? request.label : labelInput.text,
                paymentReceived ? request.message : messageInput.text, noteInput.text)) {
                actionErrorText = qsTr("The changes could not be saved. Please try again.")
                return false
            }
            resetFields()
            return true
        }
        return saveField("amount", amountInput) && saveField("label", labelInput)
            && saveField("message", messageInput) && saveField("noteSelf", noteInput)
    }
    function toggleAmountUnit() {
        if (paymentReceived) return
        // Preserve a typed draft. An untouched saved amount simply follows
        // the app's display unit without becoming an edit to the request.
        if (!saved) amountInput.modified = true
        optionsModel.displayUnit = amountUnit === BitcoinAmount.SAT ? BitcoinAmount.BTC : BitcoinAmount.SAT
        if (!saved) saveField("amount", amountInput)
    }
    function createRequest() {
        if (!wallet || !request || saved || !saveFields()) return
        actionErrorText = ""
        if (wallet.commitReceivingPaymentRequest()) created(request.id)
        else actionErrorText = qsTr("The payment request could not be created. Please try again.")
    }
    function copyRequest() {
        if (!sharing || !request.qrPayload) return
        clipboard.setText(request.qrPayload)
    }

    function captureQR(callback) {
        if (!sharing || !request.qrPayload) return false
        if (qrImage.status !== Image.Ready) {
            actionErrorText = qsTr("The QR code is not ready. Please try again.")
            return false
        }
        const requestId = request.id
        const payload = request.qrPayload
        return qrPanel.grabToImage(function(result) {
            if (root.sharing && root.request.id === requestId && root.request.qrPayload === payload)
                callback(result)
        }, Qt.size(736, 736))
    }
    function copyQR() {
        return captureQR(function(result) {
            if (!root.clipboard.setImage(result.image)) root.actionErrorText = qsTr("The QR code could not be copied. Please try again.")
        })
    }
    function saveQRToFile(fileUrl) {
        return captureQR(function(result) {
            if (!result.saveToFile(decodeURIComponent(fileUrl.toString().replace(/^file:\/\//, ""))))
                root.actionErrorText = qsTr("The QR code could not be saved. Please try again.")
        })
    }
    // Asks first; the request is deleted once the user confirms.
    function deleteRequest() {
        if (!wallet || !saved) return false
        deleteConfirmation.confirmDelete(wallet, request.id)
        return true
    }

    PaymentRequestDeleteConfirmation {
        id: deleteConfirmation
        objectName: "requestPaymentDeleteConfirmation"
        onDeleted: function(requestId) {
            // Discard local edits so closing the modal cannot try to save a deleted request.
            root.resetFields()
            root.deleted(requestId)
        }
    }

    BitcoinAmount {
        id: displayAmount
        satoshi: root.request ? root.request.amount.satoshi : 0
        unit: root.amountUnit
    }
    BitcoinAmount {
        id: receivedAmount
        satoshi: root.request ? root.request.receivedAmountSatoshi : 0
        unit: optionsModel.displayUnit
    }
    Connections {
        target: root.request
        function onIsEditingChanged() { root.revealTemplateFields() }
        function onLabelChanged() {
            // Make a recipient name supplied by "Request again" visible in the draft.
            if (!root.modalView && root.request.label.trim() !== "") root.showRecipientName = true
        }
        function onPaymentReceivedChanged() {
            if (!root.paymentReceived) return
            amountInput.reset()
            labelInput.reset()
            messageInput.reset()
            root.amountErrorText = ""
            root.actionErrorText = ""
            saveDialog.close()
            moreMenu.close()
            qrMenu.close()
        }
        function onIdChanged() { if (!root.saved) root.resetFields() }
    }

    Component {
        id: customizationControl

        LinkButton {
            id: customizeButton
            objectName: "requestPaymentCustomizeButton"
            text: qsTr("Customize")
            iconSource: "qrc:/icons/slider.horizontal.svg"
            onClicked: fieldsMenu.opened ? fieldsMenu.close() : fieldsMenu.open()

            ContextMenu {
                id: fieldsMenu
                objectName: "requestPaymentFieldsMenu"
                x: customizeButton.width - width
                y: customizeButton.height + 6
                modal: true
                dim: false

                ContextMenuPicker {
                    objectName: "requestPaymentFieldsPicker"
                    objectNameRole: "objectName"
                    multiSelect: true
                    selectedValues: {
                        const fields = []
                        if (root.showAmount) fields.push("amount")
                        if (root.showMessage) fields.push("message")
                        if (root.showRecipientName) fields.push("recipient")
                        return fields
                    }
                    model: [
                        { text: qsTr("Amount"), value: "amount", objectName: "requestPaymentShowAmount" },
                        { text: qsTr("Message"), value: "message", objectName: "requestPaymentShowMessage" },
                        { text: qsTr("Recipient name"), value: "recipient", objectName: "requestPaymentShowRecipientName" }
                    ]
                    onActivated: function(value) {
                        if (value === "amount") root.showAmount = !root.showAmount
                        else if (value === "message") root.showMessage = !root.showMessage
                        else if (value === "recipient") root.showRecipientName = !root.showRecipientName
                    }
                }
            }
        }
    }

    Component {
        id: noteRecommendation

        CoreText {
            objectName: "requestPaymentNoteRecommendation"
            text: qsTr("Recommended")
            textStyle: Theme.text.caption
            color: Theme.color.neutral6
            wrap: false
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.l
        RowLayout {
            visible: root.modalView
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.space.m
            spacing: 10
            CoreText {
                objectName: "requestPaymentTitle"
                Layout.fillWidth: true
                text: qsTr("Payment request")
                textStyle: Theme.text.subtitle
                horizontalAlignment: Text.AlignLeft
                wrap: true
            }
            OverflowMenuButton {
                id: moreButton
                objectName: "paymentRequestMoreButton"
                circular: true
                size: 30
                iconSize: Theme.size.icon.xl
                iconColor: Theme.color.neutral7
                activeIconColor: Theme.color.neutral8
                surfaceLevel: 1
                checked: moreMenu.opened
                onClicked: moreMenu.opened ? moreMenu.close() : moreMenu.open()
                ContextMenu {
                    id: moreMenu
                    objectName: "paymentRequestMoreMenu"
                    x: moreButton.width - width
                    y: moreButton.height + 8
                    ContextMenuButton {
                        objectName: "requestPaymentCopyQRMenuButton"
                        visible: !root.paymentReceived
                        enabled: root.sharing
                        text: qsTr("Copy QR code")
                        onTriggered: root.copyQR()
                    }
                    ContextMenuButton {
                        objectName: "requestPaymentSaveQRMenuButton"
                        visible: !root.paymentReceived
                        enabled: root.sharing
                        text: qsTr("Save QR code…")
                        onTriggered: if (root.sharing) saveDialog.open()
                    }
                    ContextMenuDivider { visible: !root.paymentReceived }
                    ContextMenuButton {
                        objectName: "requestPaymentAgainMenuButton"
                        text: qsTr("Request again")
                        onTriggered: root.requestAgain(root.request.id)
                    }
                    ContextMenuDivider {}
                    ContextMenuButton {
                        objectName: "requestPaymentDeleteMenuButton"
                        text: qsTr("Delete payment request")
                        role: ContextMenuButton.Destructive
                        onTriggered: root.deleteRequest()
                    }
                }
            }
            CloseButton {
                objectName: "paymentRequestModalClose"
                Accessible.name: qsTr("Close payment request")
                onClicked: root.closeRequested()
            }
        }

        ColumnLayout {
            visible: root.modalView
            Layout.fillWidth: true
            Layout.maximumWidth: Infinity
            spacing: Theme.space.m
            TransactionSummary {
                objectName: "requestPaymentReceivedSummary"
                visible: root.paymentReceived
                Layout.fillWidth: true
                Layout.maximumWidth: Infinity
                Layout.bottomMargin: Theme.space.xs
                amount: receivedAmount.displayWithUnit
                iconComponent: ActivityIcon {
                    objectName: "requestPaymentReceivedIcon"
                    paymentRequest: true
                    solid: true
                    iconSize: 36
                }
            }
            Rectangle {
                id: qrPanel
                objectName: "requestPaymentQRPlaceholder"
                visible: !root.paymentReceived
                Layout.preferredWidth: 184
                Layout.preferredHeight: 184
                Layout.alignment: Qt.AlignHCenter
                radius: 10
                color: Theme.color.qrBackground
                QRImage {
                    id: qrImage
                    objectName: "requestPaymentQRImage"
                    anchors.fill: parent
                    anchors.margins: Theme.space.l
                    visible: root.sharing
                    opacity: root.sharing ? 1 : 0
                    code: root.sharing && root.request ? root.request.qrPayload : ""
                    backgroundColor: Theme.color.qrBackground
                    foregroundColor: Theme.color.qrForeground
                    Behavior on opacity { enabled: Theme.motion.animated; NumberAnimation { duration: Theme.motion.fade(Theme.motion.moderate) } }
                }
                MouseArea {
                    objectName: "requestPaymentQRContextArea"
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    enabled: root.sharing
                    onClicked: function(mouse) {
                        qrMenu.x = mouse.x
                        qrMenu.y = mouse.y
                        qrMenu.open()
                    }
                }
                ContextMenu {
                    id: qrMenu
                    objectName: "requestPaymentQRContextMenu"
                    ContextMenuButton {
                        objectName: "requestPaymentQRContextCopy"
                        text: qsTr("Copy QR code")
                        enabled: root.sharing
                        onTriggered: root.copyQR()
                    }
                    ContextMenuButton {
                        objectName: "requestPaymentQRContextSave"
                        text: qsTr("Save QR code…")
                        enabled: root.sharing
                        onTriggered: if (root.sharing) saveDialog.open()
                    }
                }
            }
            CoreText {
                objectName: "paymentRequestCreatedDate"
                visible: root.saved && !!root.request.createdIso
                Layout.fillWidth: true
                Layout.topMargin: root.sharing ? 4 : 0
                text: visible ? qsTr("Created %1").arg(new Date(root.request.createdIso).toLocaleString(Qt.locale(), Locale.ShortFormat)) : ""
                textStyle: Theme.text.caption
                color: Theme.color.neutral7
                horizontalAlignment: Text.AlignHCenter
                wrap: true
            }
            StatusPill {
                objectName: "paymentRequestStatus"
                visible: root.saved
                Layout.alignment: Qt.AlignHCenter
                showDot: false
                text: root.paymentReceived ? qsTr("Payment received") : qsTr("Awaiting payment")
                accentColor: root.paymentReceived ? Theme.color.success : Theme.color.paymentRequest
            }
            AddressLabel {
                objectName: "requestPaymentAddressText"
                Layout.fillWidth: true
                Layout.topMargin: Theme.space.m
                address: root.request ? root.request.address : ""
                textStyle: Theme.text.monoDescription
                interactive: !root.saved || root.sharing
                clipboard: root.clipboard
                surfaceLevel: 2
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space.m
            FormSection {
                objectName: "requestPaymentFieldsSection"
                Layout.fillWidth: true
                //: Heading for the amount, recipient name, and message included in a payment request.
                title: root.modalView ? "" : qsTr("Payment details")
                headerTrailingItem: root.modalView ? null : customizationControl
                surfaceLevel: root.modalView ? 1 : 0
                cornerRadius: root.modalView ? 12 : 16
                showGradientBorder: false
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    ValueRow {
                        visible: root.modalView
                        Layout.fillWidth: true
                        title: qsTr("Address type")
                        titleTextStyle: Theme.text.description
                        titleColor: root.paymentReceived ? Theme.color.disabledContent : Theme.color.neutral7
                        valueColor: root.paymentReceived ? Theme.color.disabledContent : Theme.color.neutral9
                        minimumRowHeight: 56
                        value: root.request ? root.wallet.receiveAddressTypeLabel(root.request.addressType.toLowerCase()) : ""
                        showDivider: true
                        dividerColor: Theme.color.surfaceDivider
                    }
                    PaymentRequestField {
                        id: amountInput
                        objectName: "requestPaymentAmountField"
                        visible: root.modalView || root.showAmount
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        label: root.modalView ? qsTr("Requested amount") : qsTr("Amount")
                        value: root.request && root.request.amount.satoshi > 0 ? displayAmount.display : ""
                        placeholderText: "0"
                        fieldObjectName: "requestPaymentAmountInput"
                        editable: !root.paymentReceived
                        showDivider: labelInput.visible || messageInput.visible
                        dividerColor: Theme.color.surfaceDivider
                        fieldTextStyle: Theme.text.monoDescription
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        maximumLength: 32
                        validator: RegularExpressionValidator { regularExpression: /^0*\d{0,16}(\.\d{0,8})?$/ }
                        onTextChanged: if (root.amountInSatoshis(text) >= 0) root.amountErrorText = ""
                        onEditingFinished: if (!root.saved) root.saveField("amount", this)
                        unitControl: AmountUnitButton {
                            id: unitButton
                            objectName: "requestPaymentAmountUnitToggle"
                            iconObjectName: "requestPaymentAmountUnitIcon"
                            unit: root.amountUnit
                            onClicked: root.toggleAmountUnit()
                        }
                    }
                    PaymentRequestField {
                        id: labelInput
                        objectName: "requestPaymentLabelRow"
                        visible: root.modalView || root.showRecipientName
                        Layout.fillWidth: true
                        label: qsTr("Pay to")
                        showDivider: messageInput.visible
                        dividerColor: Theme.color.surfaceDivider
                        value: root.request ? root.request.label : ""
                        placeholderText: qsTr("Your name or business")
                        fieldObjectName: "requestPaymentYourNameInput"
                        editable: !root.paymentReceived
                        onEditingFinished: if (!root.saved) root.saveField("label", this)
                    }
                    PaymentRequestField {
                        id: messageInput
                        objectName: "requestPaymentMessageRow"
                        visible: root.modalView || root.showMessage
                        Layout.fillWidth: true
                        label: qsTr("Message")
                        value: root.request ? root.request.message : ""
                        placeholderText: qsTr("Lunch split")
                        fieldObjectName: "requestPaymentMessageInput"
                        editable: !root.paymentReceived
                        onEditingFinished: if (!root.saved) root.saveField("message", this)
                    }
                    Item {
                        objectName: "requestPaymentEmptyRow"
                        visible: !root.modalView && !root.hasPublicFields
                        Layout.fillWidth: true
                        Layout.preferredHeight: 56
                        LinkButton {
                            objectName: "requestPaymentAddDetailsButton"
                            anchors.centerIn: parent
                            text: qsTr("Add details to your payment request")
                            onClicked: {
                                root.showAmount = true
                                root.showMessage = true
                            }
                        }
                    }
                }
            }
            RowLayout {
                objectName: "requestPaymentDetailsFooter"
                visible: !root.modalView && root.hasPublicFields
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space.xs
                Layout.rightMargin: Theme.space.xs
                spacing: Theme.space.s
                Icon {
                    Layout.preferredWidth: 13
                    Layout.preferredHeight: 13
                    source: "qrc:/icons/globe.svg"
                    size: 13
                    color: Theme.color.neutral7
                }
                CoreText {
                    Layout.fillWidth: true
                    //: Footer explaining that the payment details above are shared with the person paying.
                    text: qsTr("These details appear in the payment link and QR code.")
                    textStyle: Theme.text.caption
                    color: Theme.color.neutral7
                    horizontalAlignment: Text.AlignLeft
                    wrap: true
                }
            }
        }
        Separator {
            objectName: "requestPaymentDetailsSeparator"
            visible: !root.modalView
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            Layout.topMargin: Theme.space.m
            Layout.bottomMargin: Theme.space.m
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.space.m

            FormSection {
                objectName: "requestPaymentNoteSection"
                Layout.fillWidth: true
                title: root.modalView ? "" : qsTr("Private details")
                headerTrailingItem: root.modalView ? null : noteRecommendation
                surfaceLevel: root.modalView ? 1 : 0
                cornerRadius: root.modalView ? 12 : 16
                showGradientBorder: false

                PaymentRequestField {
                    id: noteInput
                    objectName: "requestPaymentNoteRow"
                    Layout.fillWidth: true
                    label: qsTr("Note to self")
                    value: root.request ? root.request.noteSelf : ""
                    placeholderText: qsTr("Lunch with Hal")
                    fieldObjectName: "requestPaymentNoteSelfInput"
                    onEditingFinished: if (!root.saved) root.saveField("noteSelf", this)
                }
            }

            RowLayout {
                objectName: "requestPaymentNoteFooter"
                visible: !root.modalView
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space.xs
                Layout.rightMargin: Theme.space.xs
                spacing: Theme.space.s

                Icon {
                    Layout.preferredWidth: 13
                    Layout.preferredHeight: 13
                    source: "qrc:/icons/lock.fill.svg"
                    size: 13
                    color: Theme.color.neutral6
                }
                CoreText {
                    Layout.fillWidth: true
                    text: qsTr("Your note stays in this wallet. Only you can see it.")
                    textStyle: Theme.text.caption
                    color: Theme.color.neutral6
                    horizontalAlignment: Text.AlignLeft
                    wrap: true
                }
            }
        }
        CoreText {
            objectName: "requestPaymentError"
            visible: root.errorText.length > 0
            Layout.fillWidth: true
            text: root.errorText
            color: Theme.color.dangerText
            textStyle: Theme.text.caption
            horizontalAlignment: Text.AlignLeft
            wrap: true
        }
        TextButton {
            visible: !root.modalView && !!root.wallet && !!root.wallet.receivingAddress
                && (root.wallet.receivingAddress.address === "" || root.wallet.receivingAddress.paymentReceived)
            Layout.alignment: Qt.AlignHCenter
            //: Retry generating the receiving address after a failure.
            text: qsTr("Try again")
            onClicked: root.retryAddressRequested()
        }
        ContinueButton {
            objectName: "requestPaymentGenerateButton"
            visible: !root.saved
            Layout.topMargin: root.modalView ? 0 : 16
            enabled: !!root.wallet && !!root.wallet.receivingAddress
                && root.wallet.receivingAddress.address !== "" && !root.wallet.receivingAddress.paymentReceived
            Layout.fillWidth: true
            text: qsTr("Create payment request")
            onClicked: root.createRequest()
        }
        ContinueButton {
            objectName: "requestPaymentUpdateButton"
            visible: root.saved && root.modified
            Layout.fillWidth: true
            enabled: root.modified && !!root.wallet
                && !root.wallet.receiveRequestReconciliationPending
            text: root.wallet && root.wallet.receiveRequestReconciliationPending
                ? qsTr("Checking payments…") : qsTr("Update payment request")
            onClicked: root.saveFields()
        }
        OutlineButton {
            objectName: "requestPaymentCopyButton"
            visible: root.saved && !root.modified && !root.paymentReceived
            enabled: root.sharing
            Layout.fillWidth: true
            text: qsTr("Copy payment request")
            onClicked: root.copyRequest()
        }

    }
    AppFileDialog {
        id: saveDialog
        objectName: "requestPaymentSaveQRDialog"
        fileMode: AppFileDialog.SaveFile
        nameFilters: [qsTr("PNG files (*.png)")]
        defaultSuffix: "png"
        onAccepted: root.saveQRToFile(selectedFile)
    }
}
