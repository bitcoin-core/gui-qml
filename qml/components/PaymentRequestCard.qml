// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Dialogs
import org.bitcoincore.qt 1.0
import "../controls"
import "../pages/wallet"

Pane {
    id: root
    objectName: "paymentRequestCard"
    property var wallet
    property var request
    property bool modalView: false
    property var clipboard: Clipboard
    readonly property int amountUnit: optionsModel.displayUnit
    property string errorText: ""
    readonly property bool saved: !!request && request.id !== ""
    readonly property bool paymentReceived: !!request && request.paymentReceived
    readonly property bool sharing: visible && saved && !paymentReceived
    readonly property bool hasFields: amountInput.text.trim() !== "" || labelInput.text.trim() !== ""
        || messageInput.text.trim() !== "" || noteInput.text.trim() !== ""
    readonly property bool modified: amountInput.modified || labelInput.modified || messageInput.modified || noteInput.modified
    signal created(string requestId)
    signal closeRequested()
    signal deleted(string requestId)

    padding: modalView ? (width < 480 ? 16 : 24) : 0
    implicitWidth: 560
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    background: Rectangle {
        visible: root.modalView
        radius: 16
        color: Theme.color.neutral1
        border.width: 1
        border.color: root.modalView ? Theme.color.neutral3 : Theme.color.neutral2
    }

    function resetFields() {
        errorText = ""
        amountInput.reset()
        labelInput.reset()
        messageInput.reset()
        noteInput.reset()
    }
    onRequestChanged: resetFields()
    Component.onCompleted: resetFields()

    function amountInSatoshis(text) {
        if (text === "") return 0
        if (!/^\d*(\.\d*)?$/.test(text) || text === ".") return -1
        const decimals = amountUnit === BitcoinAmount.SAT ? 0
            : amountUnit === BitcoinAmount.mBTC ? 5 : amountUnit === BitcoinAmount.uBTC ? 2 : 8
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
            errorText = amountUnit === BitcoinAmount.SAT
                ? qsTr("Enter a whole number of sats, up to 2,100,000,000,000,000.")
                : qsTr("Enter a valid amount, up to 21,000,000 BTC.")
            return false
        }
        if (name === "amount") request.amount.satoshi = value
        else request[name] = value
        field.modified = false
        errorText = ""
        return true
    }
    function saveFields() {
        if (saved) {
            const amount = paymentReceived ? request.amount.satoshi : amountInSatoshis(amountInput.text)
            if (amount < 0) { errorText = qsTr("Enter a valid amount."); return false }
            if (!wallet.updatePaymentRequest(request.id, amount,
                paymentReceived ? request.label : labelInput.text,
                paymentReceived ? request.message : messageInput.text, noteInput.text)) {
                errorText = qsTr("The changes could not be saved. Please try again.")
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
        if (!wallet || !request || saved || !hasFields || !saveFields()) return
        errorText = ""
        if (wallet.commitReceivingPaymentRequest()) created(request.id)
        else errorText = qsTr("The payment request could not be created. Please try again.")
    }
    function copyRequest() {
        if (!sharing || !request.qrPayload) return
        clipboard.setText(request.qrPayload)
    }

    function captureQR(callback) {
        if (!sharing || !request.qrPayload) return false
        if (qrImage.status !== Image.Ready) {
            errorText = qsTr("The QR code is not ready. Please try again.")
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
            if (!root.clipboard.setImage(result.image)) root.errorText = qsTr("The QR code could not be copied. Please try again.")
        })
    }
    function saveQRToFile(fileUrl) {
        return captureQR(function(result) {
            if (!result.saveToFile(decodeURIComponent(fileUrl.toString().replace(/^file:\/\//, ""))))
                root.errorText = qsTr("The QR code could not be saved. Please try again.")
        })
    }
    function deleteRequest() {
        if (!wallet || !saved) return false
        const requestId = request.id
        if (!wallet.removeReceiveRequest(requestId)) {
            errorText = qsTr("The payment request could not be deleted. Please try again.")
            return false
        }
        // Discard local edits so closing the modal cannot try to save a deleted request.
        resetFields()
        deleted(requestId)
        return true
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
        function onPaymentReceivedChanged() {
            if (!root.paymentReceived) return
            amountInput.reset()
            labelInput.reset()
            messageInput.reset()
            root.errorText = ""
            saveDialog.close()
            moreMenu.close()
            qrMenu.close()
        }
        function onIdChanged() { if (!root.saved) root.resetFields() }
    }

    contentItem: ColumnLayout {
        spacing: 16
        RowLayout {
            visible: root.modalView
            Layout.fillWidth: true
            Layout.bottomMargin: 8
            spacing: 10
            CoreText {
                objectName: "requestPaymentTitle"
                Layout.fillWidth: true
                text: qsTr("Payment request")
                font: Theme.text.title.font
                horizontalAlignment: Text.AlignLeft
                wrap: true
            }
            OverflowMenuButton {
                id: moreButton
                objectName: "paymentRequestMoreButton"
                circular: true
                size: 30
                iconSize: 24
                iconColor: Theme.color.neutral6
                activeIconColor: Theme.color.neutral8
                backgroundColor: Theme.color.neutral2
                hoverBackgroundColor: Theme.color.neutral3
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
                        text: qsTr("Save QR code")
                        onTriggered: if (root.sharing) saveDialog.open()
                    }
                    ContextMenuDivider { visible: !root.paymentReceived }
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
            spacing: 8
            TransactionSummary {
                objectName: "requestPaymentReceivedSummary"
                visible: root.paymentReceived
                Layout.fillWidth: true
                Layout.maximumWidth: Infinity
                Layout.bottomMargin: 4
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
                color: "white"
                QRImage {
                    id: qrImage
                    objectName: "requestPaymentQRImage"
                    anchors.fill: parent
                    anchors.margins: 16
                    visible: root.sharing
                    opacity: root.sharing ? 1 : 0
                    code: root.sharing && root.request ? root.request.qrPayload : ""
                    backgroundColor: "white"
                    foregroundColor: "black"
                    Behavior on opacity { NumberAnimation { duration: 180 } }
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
                        text: qsTr("Save QR code")
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
                text: visible ? qsTr("Created %1").arg(Qt.formatDateTime(new Date(root.request.createdIso), "MMM d, h:mm AP")) : ""
                font: Theme.text.caption.font
                color: Theme.color.neutral7
                horizontalAlignment: Text.AlignHCenter
                wrap: true
            }
            Rectangle {
                visible: root.saved
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: statusLabel.implicitWidth + 24
                implicitHeight: 30
                radius: 15
                color: Qt.rgba(statusLabel.color.r, statusLabel.color.g, statusLabel.color.b, 0.14)
                CoreText {
                    id: statusLabel
                    objectName: "paymentRequestStatus"
                    anchors.centerIn: parent
                    text: root.paymentReceived ? qsTr("Payment received") : qsTr("Awaiting payment")
                    color: root.paymentReceived ? Theme.color.green : Theme.color.lavender
                    font: Theme.text.captionStrong.font
                }
            }
            AddressLabel {
                objectName: "requestPaymentAddressText"
                Layout.fillWidth: true
                Layout.topMargin: 8
                address: root.request ? root.request.address : ""
                textStyle: Theme.text.monoDescription
                interactive: !root.paymentReceived
                clipboard: root.clipboard
                embedded: true
            }
        }

        RowLayout {
            visible: !root.modalView
            Layout.fillWidth: true
            spacing: 12
            CoreText {
                text: qsTr("Payment request")
                font: Theme.text.title.font
                horizontalAlignment: Text.AlignLeft
            }
            Rectangle {
                implicitWidth: optionalLabel.implicitWidth + 24
                implicitHeight: optionalLabel.implicitHeight + 12
                radius: height / 2
                color: Theme.color.neutral2
                CoreText {
                    id: optionalLabel
                    anchors.centerIn: parent
                    text: qsTr("Optional")
                    font: Theme.text.caption.font
                    color: Theme.color.neutral6
                }
            }
            Item { Layout.fillWidth: true }
        }
        CoreText {
            visible: !root.modalView
            Layout.fillWidth: true
            text: qsTr("Add payment details for this address")
            font: Theme.text.description.font
            color: Theme.color.neutral7
            horizontalAlignment: Text.AlignLeft
            wrap: true
        }
        RowLayout {
            visible: !root.modalView
            Layout.bottomMargin: -8
            spacing: 6
            Icon {
                Layout.preferredWidth: 13
                Layout.preferredHeight: 13
                source: "qrc:/icons/globe.svg"
                size: 13
                color: Theme.color.neutral6
            }
            CoreText {
                text: qsTr("Included in the request")
                font: Theme.text.caption.font
                color: Theme.color.neutral6
            }
        }

        Pane {
            objectName: "requestPaymentFieldsSection"
            Layout.fillWidth: true
            padding: 0
            background: Rectangle { color: root.modalView ? Theme.color.neutral2 : Theme.color.neutral1; radius: 12 }
            contentItem: ColumnLayout {
                spacing: 0
                ValueRow {
                    visible: root.modalView
                    Layout.fillWidth: true
                    title: qsTr("Address type")
                    titleTextStyle: Theme.text.description
                    titleColor: root.paymentReceived ? Theme.color.neutral4 : Theme.color.neutral7
                    valueColor: root.paymentReceived ? Theme.color.neutral4 : Theme.color.neutral9
                    minimumRowHeight: 56
                    value: root.request ? root.wallet.receiveAddressTypeLabel(root.request.addressType.toLowerCase()) : ""
                    showDivider: true
                }
                PaymentRequestField {
                    id: amountInput
                    objectName: "requestPaymentAmountField"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    label: root.modalView ? qsTr("Requested amount") : qsTr("Amount")
                    value: root.request && root.request.amount.satoshi > 0 ? displayAmount.display : ""
                    placeholderText: "0"
                    fieldObjectName: "requestPaymentAmountInput"
                    editable: !root.paymentReceived
                    showDivider: true
                    fieldTextStyle: Theme.text.monoDescription
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    maximumLength: 32
                    validator: RegularExpressionValidator { regularExpression: /^0*\d{0,16}(\.\d{0,8})?$/ }
                    onEditingFinished: if (!root.saved) root.saveField("amount", this)
                    unitControl: TextButton {
                        id: unitButton
                        objectName: "requestPaymentAmountUnitToggle"
                        text: root.amountUnit === BitcoinAmount.SAT ? qsTr("sats")
                            : root.amountUnit === BitcoinAmount.mBTC ? "mBTC"
                            : root.amountUnit === BitcoinAmount.uBTC ? qsTr("bits") : "BTC"
                        textSize: 14
                        textColor: !enabled ? Theme.color.neutral4
                            : hovered ? Theme.color.orange : Theme.color.neutral9
                        bgColor: Theme.color.neutral2
                        states: []
                        padding: 6
                        horizontalPadding: 8
                        contentItem: RowLayout {
                            spacing: 4
                            CoreText {
                                text: unitButton.text
                                font: Theme.text.caption.font
                                color: unitButton.textColor
                            }
                            Icon {
                                objectName: "requestPaymentAmountUnitIcon"
                                Layout.minimumWidth: 12
                                Layout.preferredWidth: 12
                                Layout.maximumWidth: 12
                                Layout.minimumHeight: 12
                                Layout.preferredHeight: 12
                                Layout.maximumHeight: 12
                                source: "qrc:/icons/arrow-up-down.svg"
                                size: 12
                                color: unitButton.textColor
                            }
                        }
                        Accessible.name: qsTr("Change amount unit")
                        onClicked: root.toggleAmountUnit()
                    }
                }
                PaymentRequestField {
                    id: labelInput
                    objectName: "requestPaymentLabelRow"
                    Layout.fillWidth: true
                    label: qsTr("Pay to")
                    showDivider: true
                    value: root.request ? root.request.label : ""
                    placeholderText: qsTr("Your name or business")
                    fieldObjectName: "requestPaymentYourNameInput"
                    editable: !root.paymentReceived
                    onEditingFinished: if (!root.saved) root.saveField("label", this)
                }
                PaymentRequestField {
                    id: messageInput
                    objectName: "requestPaymentMessageRow"
                    Layout.fillWidth: true
                    label: qsTr("For")
                    value: root.request ? root.request.message : ""
                    placeholderText: qsTr("Lunch split")
                    fieldObjectName: "requestPaymentMessageInput"
                    editable: !root.paymentReceived
                    onEditingFinished: if (!root.saved) root.saveField("message", this)
                }
            }
        }
        RowLayout {
            visible: !root.modalView
            Layout.bottomMargin: -8
            spacing: 6
            Icon {
                Layout.preferredWidth: 13
                Layout.preferredHeight: 13
                source: "qrc:/icons/lock.fill.svg"
                size: 13
                color: Theme.color.neutral6
            }
            CoreText {
                text: qsTr("Only visible to you")
                font: Theme.text.caption.font
                color: Theme.color.neutral6
            }
        }
        Pane {
            Layout.fillWidth: true
            padding: 0
            background: Rectangle { color: root.modalView ? Theme.color.neutral2 : Theme.color.neutral1; radius: 12 }
            contentItem: ColumnLayout {
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
        }
        CoreText {
            objectName: "requestPaymentError"
            visible: root.errorText.length > 0
            Layout.fillWidth: true
            text: root.errorText
            color: Theme.color.red
            font: Theme.text.caption.font
            horizontalAlignment: Text.AlignLeft
            wrap: true
        }
        ContinueButton {
            objectName: "requestPaymentGenerateButton"
            visible: !root.saved
            enabled: root.hasFields && !!root.wallet && !!root.wallet.receivingAddress
                && root.wallet.receivingAddress.address !== "" && !root.wallet.receivingAddress.paymentReceived
            Layout.fillWidth: true
            text: qsTr("Create payment request")
            onClicked: root.createRequest()
        }
        ContinueButton {
            objectName: "requestPaymentUpdateButton"
            visible: root.saved
            Layout.fillWidth: true
            enabled: root.modified
            text: qsTr("Update payment request")
            onClicked: if (root.saveFields()) root.closeRequested()
        }

    }
    FileDialog {
        id: saveDialog
        objectName: "requestPaymentSaveQRDialog"
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("PNG files (*.png)")]
        defaultSuffix: "png"
        onAccepted: root.saveQRToFile(selectedFile)
    }
}
