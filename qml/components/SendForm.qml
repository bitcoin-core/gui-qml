// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

GridLayout {
    id: root
    required property bool available
    required property double estimatedFeeSatoshi
    required property string estimatedFeeRate
    required property bool feeEstimatePending
    required property int estimatedInputCount
    required property double sendTotalSatoshi
    signal useMaximumRequested(SendRecipient recipient)
    required property SendRecipientsListModel recipients
    required property FeeSelectionModel fees
    required property CoinsListModel coins
    required property bool preparing
    required property bool canPrepare
    required property string errorText
    required property int expandedRecipient
    required property bool clipboardOffered
    required property string clipboardText
    required property string importErrorText
    required property bool manualCoinSelection
    required property bool selectedInputsActive

    signal prepareRequested()
    signal resetRequested()
    signal toggleRecipientRequested(int index, bool expanded)
    signal removeRecipientRequested(int index)
    signal addRecipientRequested()
    signal paymentUriPasted(int field, string text, SendRecipient recipient)
    signal recipientEdited()
    signal feeEdited()
    signal showCoinsRequested()
    signal automaticSelectionRequested()

    function recipientAt(index: int): SendRecipientCard {
        return recipientRepeater.itemAt(index) as SendRecipientCard
    }

    readonly property int selectedCount: coins ? coins.selectedCoinsCount : 0
    readonly property bool feeAvailable: fees && !fees.estimatePending && estimatedFeeSatoshi >= 0

    Layout.fillWidth: true
    columns: width >= SizeClass.wideWidthMin ? 2 : 1
    columnSpacing: 24
    rowSpacing: 24

    FormSection {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        Layout.preferredWidth: root.columns === 2 ? root.width * 0.6 - 12 : root.width
        title: root.recipients && root.recipients.count > 1 ? qsTranslate("SendCreate", "Recipients") : qsTranslate("SendCreate", "Recipient")
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
                model: root.recipients
                delegate: SendRecipientCard {
                    id: recipientDelegate
                    recipients: root.recipients
                    available: root.available
                    selectedCoins: root.selectedCount > 0
                    fees: root.fees
                    onUseMaximumRequested: root.useMaximumRequested(recipientDelegate.recipientObject)
                    width: recipientColumn.width
                    expanded: root.expandedRecipient === recipientDelegate.index
                    clipboardRequest: root.clipboardOffered
                    clipboardText: root.clipboardText
                    onToggleExpanded: root.toggleRecipientRequested(recipientDelegate.index, recipientDelegate.expanded)
                    onPaymentUriPasted: function(field, text) {
                        root.paymentUriPasted(field, text, recipientDelegate.editorItem ? recipientDelegate.editorItem.recipient : null)
                    }
                    onEdited: root.recipientEdited()
                    onRemoveRequested: root.removeRecipientRequested(recipientDelegate.index)
                }
            }
        }
        CoreText {
            objectName: "sendPaymentRequestStatusText"
            visible: root.importErrorText.length > 0
            Layout.leftMargin: 16; Layout.rightMargin: 16
            Layout.fillWidth: true; text: root.importErrorText; horizontalAlignment: Text.AlignLeft; color: Theme.color.dangerText; textStyle: Theme.text.description
        }

        NeutralButton {
            objectName: "sendAddRecipientButton"
            Layout.fillWidth: true
            text: qsTranslate("SendCreate", "+ Add another")
            enabled: root.recipients && root.recipients.canAddRecipient
            onClicked: root.addRecipientRequested()
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        Layout.preferredWidth: root.columns === 2 ? root.width * 0.4 - 12 : root.width
        spacing: 24

        FormSection {
            objectName: "sendNetworkFeeSection"
            Layout.fillWidth: true
            title: qsTranslate("SendCreate", "Network fee")
            footerText: !root.recipients ? ""
                : root.fees && root.fees.estimatePending ? qsTranslate("SendCreate", "Estimating…")
                : root.estimatedFeeSatoshi >= 0 ? ""
                : root.recipients && root.recipients.allValid ? qsTranslate("SendCreate", "Fee estimate unavailable")
                : qsTranslate("SendCreate", "Enter a valid recipient address and amount to estimate the fee")
            SendFeeSelection {
                Layout.fillWidth: true
                fees: root.fees
                estimatedFeeSatoshi: root.estimatedFeeSatoshi
                estimatedFeeRate: root.estimatedFeeRate
                feeEstimatePending: root.feeEstimatePending
                onFeeChanged: root.feeEdited()
            }
        }

        FormSection {
            Layout.fillWidth: true
            title: qsTranslate("SendCreate", "Coin control")

            FormRow {
                Layout.fillWidth: true
                title: qsTranslate("SendCreate", "Selection")
                showDivider: root.manualCoinSelection || root.selectedInputsActive
                trailingItem: SegmentedPicker {
                    objectName: "sendCoinControlPicker"
                    implicitWidth: 190
                    model: [qsTranslate("SendCreate", "Automatic"), qsTranslate("SendCreate", "Manual")]
                    currentIndex: root.manualCoinSelection || root.selectedInputsActive ? 1 : 0
                    onSelected: function(index, option) {
                        if (root.coins)
                            root.coins.setManualInputSelection(index === 1)
                    }
                }
            }

            FormRow {
                visible: root.manualCoinSelection || root.selectedInputsActive
                Layout.fillWidth: true
                title: qsTranslate("SendCreate", "Inputs")
                description: root.selectedCount === 0 ? qsTranslate("SendCreate", "No inputs selected")
                    : root.selectedCount === 1 ? qsTranslate("SendCreate", "1 input selected") : qsTranslate("SendCreate", "%1 inputs selected").arg(root.selectedCount)
                showDivider: false
                trailingItem: NeutralButton {
                    objectName: "sendSelectInputsButton"
                    buttonSize: NeutralButton.Medium
                    text: qsTranslate("SendCreate", "Select coins")
                    onClicked: root.showCoinsRequested()
                }
            }

            CoreText {
                objectName: "sendInputsSelectedText"
                visible: false
                //: Status in the Send form showing how many transaction inputs are selected.
                text: root.manualCoinSelection && root.selectedCount === 0 ? qsTranslate("SendCreate", "No inputs selected")
                    : root.selectedInputsActive ? (root.selectedCount === 1 ? qsTranslate("SendCreate", "1 input selected") : qsTranslate("SendCreate", "%1 inputs selected").arg(root.selectedCount))
                    : qsTranslate("SendCreate", "%1 inputs selected automatically").arg(root.estimatedInputCount)
            }
            NeutralButton {
                objectName: "sendUseAutomaticInputsButton"
                buttonSize: NeutralButton.Large
                visible: false
                text: qsTranslate("SendCreate", "Use automatic selection")
                onClicked: root.automaticSelectionRequested()
            }
        }

        FormSection {
            Layout.fillWidth: true
            title: qsTranslate("SendCreate", "Summary")
            BitcoinAmount { id: summaryFee; satoshi: Math.max(0, root.estimatedFeeSatoshi); unit: ApplicationModels.optionsModel.displayUnit }
            BitcoinAmount { id: recipientsTotalAmount; satoshi: root.recipients ? root.recipients.totalAmountSatoshi : 0; unit: ApplicationModels.optionsModel.displayUnit }
            BitcoinAmount { id: summaryTotal; satoshi: root.sendTotalSatoshi; unit: ApplicationModels.optionsModel.displayUnit }
            ValueRow { objectName: "sendTotalFeesValue"; Layout.fillWidth: true; amountValue: true; animateUnitChanges: false; title: qsTranslate("SendCreate", "Total fees"); value: root.feeAvailable ? summaryFee.localizedDisplay : "—"; valueUnit: summaryFee.unitLabel }
            ValueRow { objectName: "sendTotalAmountValue"; Layout.fillWidth: true; amountValue: true; animateUnitChanges: false; title: qsTranslate("SendCreate", "Total to send"); showDivider: false; value: root.feeAvailable ? summaryTotal.localizedDisplay : recipientsTotalAmount.localizedDisplay; valueUnit: root.feeAvailable ? summaryTotal.unitLabel : recipientsTotalAmount.unitLabel; valueTextStyle: Theme.text.amountBody }
        }

        CoreText { objectName: "sendPrepareTransactionErrorText"; Layout.fillWidth: true; visible: root.errorText.length > 0; text: root.errorText; horizontalAlignment: Text.AlignLeft; textStyle: Theme.text.description; color: Theme.color.dangerText }
        PrimaryButton {
            objectName: "sendReviewButton"
            Layout.fillWidth: true
            text: qsTranslate("SendCreate", "Review transaction")
            enabled: root.canPrepare && !root.preparing
            onClicked: root.prepareRequested()
        }
    }
}
