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

    Connections {
        target: walletController
        function onSelectedWalletChanged() {
            root.pop()
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
        background: null

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

                RowLayout {
                    id: selectAndAddRecipients
                    Layout.fillWidth: true
                    Layout.topMargin: 10
                    Layout.bottomMargin: 10
                    visible: settings.multipleRecipientsEnabled

                    CoreText {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignLeft
                        id: selectAndAddRecipientsLabel
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
                    placeholderText: qsTr("Enter ...")
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
