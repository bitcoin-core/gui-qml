// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

DialogPopup {
    id: root
    objectName: "paymentRequestReviewPopup"

    property string source: ""
    property string address: ""
    property bool hasAmount: false
    property alias amountSatoshi: reviewAmount.satoshi
    // A request without an amount leaves the form's amount in place, so it is shown.
    property alias keptAmountSatoshi: keptAmount.satoshi
    // Use maximum follows fee estimates, so no fixed amount is shown for it.
    property bool keepsMaximum: false
    property string requestLabel: ""
    property string requestMessage: ""
    property bool replacesValues: false
    // Read by the caller in onClosed: Escape leaves it false.
    property bool accepted: false

    readonly property real valueWidth: root.availableWidth * 0.65

    //: Title of the dialog that shows an incoming payment request before it fills in the Send form.
    title: qsTr("Review payment request")
    implicitWidth: 520
    // A stray click outside must not discard the request.
    closesOnPressOutside: false

    onAboutToShow: accepted = false

    BitcoinAmount {
        id: reviewAmount
        unit: optionsModel.displayUnit
    }

    BitcoinAmount {
        id: keptAmount
        unit: optionsModel.displayUnit
    }

    contentItem: ColumnLayout {
        spacing: 0

        DialogHeader {
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.space.xxl
            title: root.title
            closeButtonObjectName: "paymentRequestReviewCloseButton"
            onCloseRequested: root.close()
        }

        ScrollView {
            id: detailsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: detailsSection.implicitHeight
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            FormSection {
                id: detailsSection
                objectName: "paymentRequestReviewSection"
                width: detailsScroll.availableWidth
                backgroundColor: Theme.color.surface2

                ValueRow {
                    Layout.fillWidth: true
                    visible: root.source.length > 0
                    //: Row label in the payment request review. The value is the file or link the request came from.
                    title: qsTr("From")
                    value: root.source
                    valueObjectName: "paymentRequestReviewSource"
                    valueMaximumWidth: root.valueWidth
                    valueWrap: true
                }

                FormRow {
                    Layout.fillWidth: true
                    //: Row label in the payment request review. The value is the bitcoin address the payment goes to.
                    title: qsTr("Address")
                    trailingItem: AddressLabel {
                        objectName: "paymentRequestReviewAddress"
                        Layout.preferredWidth: root.valueWidth
                        address: root.address
                        surfaceLevel: 2
                        textStyle: Theme.text.monoCaption
                        textAlignment: Text.AlignRight
                        leftPadding: 0
                        rightPadding: 0
                    }
                }

                ValueRow {
                    Layout.fillWidth: true
                    visible: root.hasAmount || root.keepsMaximum || root.keptAmountSatoshi !== 0
                    //: Row label in the payment request review. The value is the amount the request asks for.
                    title: qsTr("Amount")
                    value: root.hasAmount
                        ? reviewAmount.displayWithUnit
                        //: Amount in the payment request review when the request has none and the Send form uses the maximum amount.
                        : root.keepsMaximum
                        ? qsTr("Not specified, keeps the maximum amount")
                        //: Amount in the payment request review when the request has none. %1 is the amount already in the Send form, which stays.
                        : qsTr("Not specified, keeps %1").arg(keptAmount.displayWithUnit)
                    valueTextStyle: root.hasAmount ? Theme.text.amountDescription : Theme.text.description
                    valueObjectName: "paymentRequestReviewAmount"
                    valueMaximumWidth: root.valueWidth
                    valueWrap: true
                }

                ValueRow {
                    Layout.fillWidth: true
                    visible: root.requestLabel.length > 0
                    //: Row label in the payment request review. The value is the label the request suggests for the recipient.
                    title: qsTr("Label")
                    value: root.requestLabel
                    valueObjectName: "paymentRequestReviewLabel"
                    valueMaximumWidth: root.valueWidth
                    valueWrap: true
                }

                ValueRow {
                    Layout.fillWidth: true
                    visible: root.requestMessage.length > 0
                    //: Row label in the payment request review. The value is the message the request carries.
                    title: qsTr("Message")
                    value: root.requestMessage
                    valueObjectName: "paymentRequestReviewMessage"
                    valueMaximumWidth: root.valueWidth
                    valueWrap: true
                    showDivider: false
                }
            }
        }

        ToastBanner {
            objectName: "paymentRequestReviewWarning"
            Layout.fillWidth: true
            Layout.topMargin: Theme.space.l
            shown: root.replacesValues
            tone: ToastBanner.Warning
            //: Warning in the payment request review when the Send form already has values that Apply would overwrite.
            text: qsTr("Applying this replaces values already entered in the form.")
            textObjectName: "paymentRequestReviewWarningText"
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space.xxl
            columns: root.compact ? 1 : 2
            columnSpacing: Theme.space.m
            rowSpacing: Theme.space.m

            NeutralButton {
                objectName: "paymentRequestReviewDiscardButton"
                buttonSize: NeutralButton.Large
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                //: Button in the payment request review that closes it and leaves the Send form unchanged.
                text: qsTr("Discard")
                onClicked: root.close()
            }

            PrimaryButton {
                objectName: "paymentRequestReviewApplyButton"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                //: Button in the payment request review that fills in the Send form with the request.
                text: qsTr("Apply")
                onClicked: {
                    root.accepted = true
                    root.close()
                }
            }
        }
    }
}
