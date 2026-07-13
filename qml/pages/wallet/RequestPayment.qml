// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtCore
import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../../controls"
import "../../components"
import "../../components" as Components

Page {
    id: root
    objectName: "requestPaymentPage"
    background: null

    property WalletQmlModel wallet: walletController.selectedWallet
    property PaymentRequest request: wallet ? wallet.currentPaymentRequest : null
    property string requestError: ""
    property string selectedReceiveAddressType: ""
    property var availableAddressTypes: root.wallet ? root.wallet.availableReceiveAddressTypes() : []
    readonly property bool hasAddress: root.requestValue("address") !== ""
    readonly property bool hasAddressType: receiveOptionsPopup.showAddressType && root.hasAddress && root.requestValue("addressType") !== ""
    readonly property bool showAddressTypeSelector: receiveOptionsPopup.showAddressType && root.request !== null && root.requestIsEditing() && !root.hasAddress
    readonly property bool hasSavedRequest: root.requestValue("id") !== ""

    signal addressHistoryRequested()

    function requestValue(name) {
        if (!root.request || root.request[name] === undefined || root.request[name] === null) {
            return ""
        }
        return root.request[name]
    }

    function requestIsEditing() {
        return !root.request || root.request.isEditing === undefined ? true : root.request.isEditing
    }

    function amountInputPattern(unit) {
        if (unit === BitcoinAmount.SAT) return /^(0|[1-9]\d{0,15})$/
        if (unit === BitcoinAmount.uBTC) return /^(0|[1-9]\d{0,13})(\.\d{0,2})?$/
        if (unit === BitcoinAmount.mBTC) return /^(0|[1-9]\d{0,10})(\.\d{0,5})?$/
        return /^(0|[1-9]\d{0,7})(\.\d{0,8})?$/
    }

    function amountInputPlaceholder(unit) {
        if (unit === BitcoinAmount.SAT) return "0"
        if (unit === BitcoinAmount.uBTC) return "0.00"
        if (unit === BitcoinAmount.mBTC) return "0.00000"
        return "0.00000000"
    }

    function amountInputMaximumLength(unit) {
        if (unit === BitcoinAmount.SAT) return 16
        if (unit === BitcoinAmount.uBTC) return 17
        if (unit === BitcoinAmount.mBTC) return 17
        return 17
    }

    function flippedDisplayUnit(unit) {
        return unit === BitcoinAmount.SAT ? BitcoinAmount.BTC : BitcoinAmount.SAT
    }

    function resetSelectedReceiveAddressType() {
        if (root.request && !root.hasAddress && root.request.addressType !== undefined && root.request.addressType !== "") {
            root.selectedReceiveAddressType = root.request.addressType
            return
        }
        if (!root.wallet) {
            root.selectedReceiveAddressType = "bech32"
            return
        }
        root.selectedReceiveAddressType = root.wallet.defaultReceiveAddressType()
            || "bech32"
    }

    function addressTypeIndex(addressType) {
        for (let i = 0; i < root.availableAddressTypes.length; ++i) {
            if (root.availableAddressTypes[i].id === addressType) {
                return i
            }
        }
        return root.availableAddressTypes.length > 0 ? 0 : -1
    }

    function commitCurrentRequest() {
        if (!root.request || !root.wallet) return false
        root.requestError = ""

        if (!root.hasAddress && root.request.addressType !== undefined && root.selectedReceiveAddressType !== "") {
            root.request.addressType = root.selectedReceiveAddressType
        }

        if (root.wallet.commitPaymentRequest()) {
            root.request.isEditing = false
            return true
        }

        if (root.request.needsUnlock) {
            commitPassphrasePopup.errorText = ""
            commitPassphrasePopup.open()
            return false
        }

        root.requestError = root.hasAddress
            ? qsTr("The payment request could not be created.")
            : qsTr("The new payment address could not be created.")
        return false
    }

    Component.onCompleted: resetSelectedReceiveAddressType()
    onWalletChanged: {
        root.requestError = ""
        resetSelectedReceiveAddressType()
    }

    Binding {
        target: root.request ? root.request.amount : null
        property: "unit"
        value: optionsModel.displayUnit
    }

    Settings {
        id: receiveSettings
        property alias receiveShowName: receiveOptionsPopup.showName
        property alias receiveShowMessage: receiveOptionsPopup.showMessage
        property alias receiveShowNoteSelf: receiveOptionsPopup.showNoteSelf
        property alias receiveShowAddressType: receiveOptionsPopup.showAddressType
    }

    function useCurrentRequestAsTemplate() {
        if (!root.wallet || !root.hasSavedRequest) return
        root.wallet.usePaymentRequestAsTemplate(root.requestValue("id"))
        root.requestError = ""
        root.resetSelectedReceiveAddressType()
    }

    function deleteCurrentRequest() {
        if (!root.wallet || !root.hasSavedRequest) return
        if (root.wallet.removeReceiveRequest(root.requestValue("id"))) {
            root.request.clear()
            root.requestError = ""
            root.resetSelectedReceiveAddressType()
            walletController.requestClosePaymentRequestDetail()
        }
    }

    Item {
        id: requestHistoryCount
        objectName: "requestHistoryCount"
        visible: false
        property int count: root.wallet && root.wallet.receiveRequests !== undefined ? root.wallet.receiveRequests.count : 0
    }

    ScrollView {
        clip: true
        width: parent.width
        height: parent.height
        contentWidth: width

        ColumnLayout {
            width: 520
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 0
            enabled: walletController.initialized

            Item {
                id: titleRow
                Layout.fillWidth: true
                Layout.preferredHeight: titleText.implicitHeight
                Layout.topMargin: 36
                Layout.bottomMargin: 36

                CoreText {
                    id: titleText
                    objectName: "requestPaymentTitle"
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.hasSavedRequest
                        ? qsTr("Payment request #%1").arg(root.requestValue("id"))
                        : qsTr("Request a payment")
                    font: Theme.text.subtitle.font
                    lineHeight: Theme.text.subtitle.lineHeight
                    lineHeightMode: Text.FixedHeight
                    color: Theme.color.neutral9
                }

                IconButton {
                    id: receiveOptionsButton
                    objectName: "receiveOptionsButton"
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    checked: receiveOptionsPopup.opened
                    iconSource: "image://images/ellipsis"
                    Accessible.name: qsTr("Receive options")
                    onClicked: {
                        if (receiveOptionsPopup.opened) {
                            receiveOptionsPopup.close()
                        } else {
                            receiveOptionsPopup.open()
                        }
                    }
                }

                Components.ReceiveOptionsPopup {
                    id: receiveOptionsPopup
                    x: receiveOptionsButton.x - width + receiveOptionsButton.width
                    y: receiveOptionsButton.y + receiveOptionsButton.height
                    showRequestActions: root.hasSavedRequest
                    onViewAddressHistory: root.addressHistoryRequested()
                    onUseAsTemplate: root.useCurrentRequestAsTemplate()
                    onDeleteFromHistory: root.deleteCurrentRequest()
                }
            }

            BitcoinAmountInputField {
                id: amountInput
                Layout.fillWidth: true
                inputObjectName: "requestPaymentAmountInput"
                accessibleName: qsTr("Payment amount")
                amount: root.request ? root.request.amount : null
                errorText: root.request ? root.request.amountError : ""
                enabled: root.requestIsEditing()
                onInputTextChanged: {
                    root.requestError = ""
                }
                onTextEdited: {
                    root.requestError = ""
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: root.requestError.length > 0

                Icon {
                    source: "image://images/alert-filled"
                    size: 22
                    color: Theme.color.red
                }

                CoreText {
                    text: root.requestError
                    font.pixelSize: 15
                    color: Theme.color.red
                    horizontalAlignment: Text.AlignLeft
                    Layout.fillWidth: true
                }
            }

            Separator {
                Layout.fillWidth: true
            }

            LabeledTextInput {
                id: nameInput
                objectName: "requestPaymentLabelInput"
                inputObjectName: "requestPaymentYourNameInput"
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showName
                labelText: qsTr("Name")
                placeholderText: qsTr("Enter name...")
                enabled: root.requestIsEditing()
                text: root.requestValue("label")
                onTextEdited: {
                    root.requestError = ""
                    if (root.request) {
                        root.request.label = nameInput.text
                    }
                }
            }

            Separator {
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showName && (receiveOptionsPopup.showMessage || receiveOptionsPopup.showNoteSelf || root.showAddressTypeSelector || root.hasAddressType || root.hasAddress)
            }

            LabeledTextInput {
                id: messageInput
                objectName: "requestPaymentMessageInput"
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showMessage
                labelText: qsTr("Message")
                placeholderText: qsTr("Enter message...")
                enabled: root.requestIsEditing()
                text: root.requestValue("message")
                onTextEdited: {
                    root.requestError = ""
                    if (root.request) {
                        root.request.message = messageInput.text
                    }
                }
            }

            Separator {
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showMessage && (receiveOptionsPopup.showNoteSelf || root.showAddressTypeSelector || root.hasAddressType || root.hasAddress)
            }

            LabeledTextInput {
                id: noteSelfInput
                objectName: "requestPaymentNoteSelfInput"
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showNoteSelf
                labelText: qsTr("Note to self")
                placeholderText: qsTr("Enter private note...")
                enabled: root.requestIsEditing()
                text: root.requestValue("noteSelf")
                onTextEdited: {
                    root.requestError = ""
                    if (root.request && root.request.noteSelf !== undefined) {
                        root.request.noteSelf = noteSelfInput.text
                    }
                }
            }

            Separator {
                Layout.fillWidth: true
                visible: receiveOptionsPopup.showNoteSelf && (root.showAddressTypeSelector || root.hasAddressType || root.hasAddress)
            }

            ColumnLayout {
                id: addressFormatRow
                Layout.fillWidth: true
                visible: root.showAddressTypeSelector
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56

                    CoreText {
                        id: addressTypePickerLabel
                        Layout.preferredWidth: 150
                        Layout.minimumWidth: 150
                        Layout.maximumWidth: 150
                        Layout.rightMargin: 10
                        horizontalAlignment: Text.AlignLeft
                        text: qsTr("Address type")
                        font: Theme.text.body.font
                        lineHeight: Theme.text.body.lineHeight
                        lineHeightMode: Text.FixedHeight
                        wrapMode: Text.NoWrap
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    DropdownButton {
                        id: addressTypePicker
                        objectName: "receiveAddressTypePicker"
                        property int selectedIndex: root.addressTypeIndex(root.selectedReceiveAddressType)
                        property string selectedLabel: selectedIndex >= 0 && root.availableAddressTypes.length > 0
                            ? root.availableAddressTypes[selectedIndex].label : ""

                        enabled: root.request !== null && root.requestIsEditing() && root.availableAddressTypes.length > 0
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        Layout.preferredWidth: Math.min(addressTypePicker.implicitWidth, 280)
                        Layout.maximumWidth: 280

                        text: selectedLabel
                        textAlignment: Text.AlignRight
                        labelTextStyle: Theme.text.body
                        caretSize: 24
                        opened: addressTypePopup.visible
                        onClicked: addressTypePopup.opened ? addressTypePopup.close() : addressTypePopup.open()
                    }
                }

                ContextMenu {
                    id: addressTypePopup
                    modal: true
                    dim: false
                    x: Math.max(0, addressTypePicker.x + addressTypePicker.width - width)
                    y: addressTypePicker.y + addressTypePicker.height + 2

                    ContextMenuPicker {
                        objectName: "receiveAddressTypeList"
                        model: root.availableAddressTypes
                        textRole: "label"
                        valueRole: "id"
                        subtitleRole: "description"
                        currentValue: root.selectedReceiveAddressType
                        onActivated: function(value) {
                            root.selectedReceiveAddressType = value
                            addressTypePopup.close()
                        }
                    }
                }
            }

            Item {
                Layout.fillWidth: true
                visible: root.hasAddressType
                Layout.preferredHeight: 56

                CoreText {
                    id: addressTypeLabel
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 128
                    horizontalAlignment: Text.AlignLeft
                    text: qsTr("Type")
                    font: Theme.text.body.font
                    lineHeight: Theme.text.body.lineHeight
                    lineHeightMode: Text.FixedHeight
                }

                CoreText {
                    anchors.left: addressTypeLabel.right
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignLeft
                    text: root.requestValue("addressType")
                    font: Theme.text.body.font
                    lineHeight: Theme.text.body.lineHeight
                    lineHeightMode: Text.FixedHeight
                    color: Theme.color.neutral9
                }
            }

            Separator {
                Layout.fillWidth: true
                visible: root.hasAddressType && root.hasAddress
            }

            Item {
                Layout.fillWidth: true
                visible: root.hasAddress
                Layout.topMargin: root.hasAddressType ? 0 : 10
                implicitHeight: addressLabel.height + copyLabel.height
                height: addressLabel.height + copyLabel.height

                CoreText {
                    id: addressLabel
                    anchors.left: parent.left
                    anchors.top: parent.top
                    horizontalAlignment: Text.AlignLeft
                    width: 128
                    text: qsTr("Address")
                    font: Theme.text.body.font
                    lineHeight: Theme.text.body.lineHeight
                    lineHeightMode: Text.FixedHeight
                }

                CoreText {
                    id: copyLabel
                    anchors.left: parent.left
                    anchors.top: addressLabel.bottom
                    horizontalAlignment: Text.AlignLeft
                    width: 128
                    text: qsTr("Copy")
                    font: Theme.text.body.font
                    lineHeight: Theme.text.body.lineHeight
                    lineHeightMode: Text.FixedHeight
                    color: Theme.color.orange
                }

                CoreText {
                    id: addressValue
                    objectName: "requestPaymentAddressText"
                    anchors.left: addressLabel.right
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    horizontalAlignment: Text.AlignLeft
                    font: Theme.text.monoBody.font
                    lineHeight: Theme.text.monoBody.lineHeight
                    lineHeightMode: Text.FixedHeight
                    wrapMode: Text.WordWrap
                    text: root.request ? root.request.addressFormatted : ""
                }

                MouseArea {
                    anchors.left: parent.left
                    anchors.top: addressLabel.bottom
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.request) {
                            Clipboard.setText(root.request.address)
                            copiedToast.show(copyLabel, (copyLabel.paintedWidth - copyLabel.width) / 2)
                        }
                    }
                }
            }

            ContinueButton {
                id: generateButton
                objectName: "requestPaymentGenerateButton"
                Layout.fillWidth: true
                Layout.topMargin: 36
                text: {
                    if (!root.request || root.requestIsEditing()) {
                        return root.hasSavedRequest
                            ? qsTr("Update payment request")
                            : qsTr("Generate payment request")
                    }
                    return qsTr("New request")
                }
                onClicked: {
                    if (!root.request) return
                    if (root.requestIsEditing()) {
                        root.commitCurrentRequest()
                    } else {
                        root.request.clear()
                        root.requestError = ""
                        root.resetSelectedReceiveAddressType()
                    }
                }

                Item {
                    objectName: "requestPaymentCreateButton"
                    anchors.fill: parent
                    enabled: false
                }
            }

            OutlineButton {
                objectName: "requestPaymentCancelButton"
                Layout.fillWidth: true
                Layout.topMargin: 10
                visible: root.request ? root.requestIsEditing() && root.hasSavedRequest : false
                text: qsTr("Cancel")
                onClicked: {
                    if (root.request && root.wallet && root.wallet.loadPaymentRequest(root.request.id)) {
                        root.request.isEditing = false
                    }
                }
            }

            RowLayout {
                id: generatedActions
                objectName: "requestPaymentGeneratedActions"
                Layout.fillWidth: true
                Layout.topMargin: 10
                visible: root.request ? !root.requestIsEditing() : false
                spacing: 10

                Button {
                    id: editButton
                    objectName: "requestPaymentEditButton"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    hoverEnabled: AppMode.isDesktop
                    implicitHeight: 46
                    Accessible.name: qsTr("Edit payment request")

                    contentItem: RowLayout {
                        spacing: 6
                        Item { Layout.fillWidth: true }
                        Icon {
                            source: "qrc:/icons/edit"
                            color: Theme.color.neutral9
                            size: 24
                        }
                        CoreText {
                            text: qsTr("Edit")
                            bold: true
                            font.pixelSize: 18
                            color: Theme.color.neutral9
                        }
                        Item { Layout.fillWidth: true }
                    }

                    background: Rectangle {
                        implicitHeight: 46
                        color: Theme.color.background
                        radius: 5
                        border.width: 1
                        border.color: editButton.pressed ? Theme.color.orangeLight2 : editButton.hovered ? Theme.color.neutral9 : Theme.color.neutral6
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                    }

                    onClicked: {
                        if (root.request) root.request.edit()
                    }
                }

                Button {
                    id: copyButton
                    objectName: "requestPaymentCopyButton"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    hoverEnabled: AppMode.isDesktop
                    implicitHeight: 46
                    Accessible.name: qsTr("Copy payment request")

                    contentItem: RowLayout {
                        spacing: 6
                        Item { Layout.fillWidth: true }
                        Icon {
                            source: "qrc:/icons/copy"
                            color: Theme.color.neutral9
                            size: 24
                        }
                        CoreText {
                            text: qsTr("Copy")
                            bold: true
                            font.pixelSize: 18
                            color: Theme.color.neutral9
                        }
                        Item { Layout.fillWidth: true }
                    }

                    background: Rectangle {
                        implicitHeight: 46
                        color: Theme.color.background
                        radius: 5
                        border.width: 1
                        border.color: copyButton.pressed ? Theme.color.orangeLight2 : copyButton.hovered ? Theme.color.neutral9 : Theme.color.neutral6
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                    }

                    onClicked: {
                        if (root.request) {
                            Clipboard.setText(root.request.qrPayload)
                            copiedToast.show(copyButton)
                        }
                    }
                }

                Button {
                    id: qrButton
                    objectName: "requestPaymentQRButton"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 0
                    hoverEnabled: AppMode.isDesktop
                    implicitHeight: 46
                    Accessible.name: qsTr("Show QR code")

                    contentItem: RowLayout {
                        spacing: 6
                        Item { Layout.fillWidth: true }
                        Icon {
                            source: "qrc:/icons/qr-code"
                            color: Theme.color.neutral9
                            size: 24
                        }
                        CoreText {
                            text: qsTr("QR Code")
                            bold: true
                            font.pixelSize: 18
                            color: Theme.color.neutral9
                        }
                        Item { Layout.fillWidth: true }
                    }

                    background: Rectangle {
                        implicitHeight: 46
                        color: Theme.color.background
                        radius: 5
                        border.width: 1
                        border.color: qrButton.pressed ? Theme.color.orangeLight2 : qrButton.hovered ? Theme.color.neutral9 : Theme.color.neutral6
                        Behavior on border.color { ColorAnimation { duration: 150 } }
                    }

                    onClicked: qrPopup.open()
                }

            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 20
            }

            Connections {
                target: root.request && root.request.isEditing !== undefined ? root.request : null
                function onIsEditingChanged() {
                    if (root.request) {
                        amountInput.syncFromAmount(true)
                        nameInput.text = root.requestValue("label")
                        messageInput.text = root.requestValue("message")
                        noteSelfInput.text = root.requestValue("noteSelf")
                    }
                }
            }

            Connections {
                target: walletController
                function onSelectedWalletChanged() {
                    if (root.request) {
                        root.request.clear()
                    }
                    root.requestError = ""
                    root.resetSelectedReceiveAddressType()
                }
            }
        }
    }

    ToastPopup {
        id: copiedToast
        objectName: "requestPaymentCopiedToast"
        popupAnchor: copyButton
        popupOffset: 4
        text: qsTr("Copied")
        backgroundColor: Theme.color.green
        borderColor: Theme.color.green
        textColor: Theme.color.white
        iconSource: "image://images/check"
        iconColor: Theme.color.white
    }

    WalletPassphrasePopup {
        id: commitPassphrasePopup
        parent: Overlay.overlay
        width: Math.min(420, root.width - 40)
        popupObjectName: "requestPaymentPassphrasePopup"
        passphraseFieldObjectName: "requestPaymentPassphraseField"
        errorTextObjectName: "requestPaymentPassphraseErrorText"
        cancelButtonObjectName: "requestPaymentPassphraseCancelButton"
        confirmButtonObjectName: "requestPaymentPassphraseConfirmButton"
        titleText: qsTr("Enter wallet password")
        descriptionText: qsTr("Enter your wallet password to create a new address.")
        confirmText: qsTr("Unlock and create")
        busyConfirmText: qsTr("Unlocking...")
        onSubmitted: passphrase => {
            commitPassphrasePopup.busy = true
            if (root.wallet && root.wallet.commitPaymentRequestWithPassphrase(passphrase)) {
                root.request.isEditing = false
                commitPassphrasePopup.busy = false
                commitPassphrasePopup.close()
                return
            }
            commitPassphrasePopup.busy = false
            commitPassphrasePopup.errorText = root.requestValue("unlockError") !== ""
                ? root.requestValue("unlockError")
                : qsTr("The payment request could not be created.")
        }
    }

    QRCodePopup {
        id: qrPopup
        objectName: "requestPaymentQRPopup"
        code: root.request ? root.request.qrPayload : ""
        label: root.requestValue("label")
        onCopyRequested: {
            if (root.request) {
                Clipboard.setText(root.request.qrPayload)
                copiedToast.show(copyButton)
            }
            qrPopup.close()
        }
    }
}
