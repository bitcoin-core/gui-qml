// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

import "../controls"

Popup {
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

    readonly property int contentMargin: 20

    modal: true
    dim: true
    // A stray click outside must not discard the request.
    closePolicy: Popup.CloseOnEscape
    focus: true
    padding: 0
    anchors.centerIn: parent
    width: parent ? Math.min(parent.width - 40, 400) : 400
    height: Math.min(implicitHeight, parent ? parent.height - 80 : 560)
    implicitHeight: Math.min(columnLayout.implicitHeight, 560)

    onAboutToShow: accepted = false

    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.5)
    }

    background: Rectangle {
        color: Theme.color.neutral1
        radius: 10
        border.color: Theme.color.neutral2
        border.width: 1
    }

    BitcoinAmount {
        id: reviewAmount
        unit: optionsModel.displayUnit
    }

    BitcoinAmount {
        id: keptAmount
        unit: optionsModel.displayUnit
    }

    ColumnLayout {
        id: columnLayout
        anchors.fill: parent
        spacing: 0

        CoreText {
            Layout.fillWidth: true
            Layout.preferredHeight: 64
            text: qsTr("Review payment request")
            font: Theme.text.subtitle.font
            color: Theme.color.neutral9
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        Separator {
            Layout.fillWidth: true
            color: Theme.color.neutral2
        }

        ScrollView {
            id: fieldsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: root.contentMargin
            contentWidth: availableWidth
            clip: true

        Column {
            width: fieldsScroll.availableWidth
            spacing: 10

            KeyValueRow {
                keyWidth: 80
                visible: root.source.length > 0
                key: CoreText {
                    text: qsTr("From")
                    color: Theme.color.neutral7
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignLeft
                }
                value: CoreText {
                    objectName: "paymentRequestReviewSource"
                    text: root.source
                    color: Theme.color.neutral9
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignLeft
                }
            }

            KeyValueRow {
                keyWidth: 80
                key: CoreText {
                    text: qsTr("Address")
                    color: Theme.color.neutral7
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignTop
                }
                value: CoreText {
                    objectName: "paymentRequestReviewAddress"
                    text: root.address
                    color: Theme.color.neutral9
                    font: Theme.text.monoCaption.font
                    wrapMode: Text.WrapAnywhere
                    horizontalAlignment: Text.AlignLeft
                }
            }

            KeyValueRow {
                keyWidth: 80
                visible: root.hasAmount || root.keepsMaximum || root.keptAmountSatoshi !== 0
                key: CoreText {
                    text: qsTr("Amount")
                    color: Theme.color.neutral7
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignLeft
                }
                value: CoreText {
                    objectName: "paymentRequestReviewAmount"
                    text: root.hasAmount
                        ? reviewAmount.displayWithUnit
                        : root.keepsMaximum
                        ? qsTr("Not specified, keeps the maximum amount")
                        : qsTr("Not specified, keeps %1").arg(keptAmount.displayWithUnit)
                    color: Theme.color.neutral9
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignLeft
                }
            }

            KeyValueRow {
                keyWidth: 80
                visible: root.requestLabel.length > 0
                key: CoreText {
                    text: qsTr("Label")
                    color: Theme.color.neutral7
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignTop
                }
                value: CoreText {
                    objectName: "paymentRequestReviewLabel"
                    text: root.requestLabel
                    color: Theme.color.neutral9
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignLeft
                }
            }

            KeyValueRow {
                keyWidth: 80
                visible: root.requestMessage.length > 0
                key: CoreText {
                    text: qsTr("Message")
                    color: Theme.color.neutral7
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignTop
                }
                value: CoreText {
                    objectName: "paymentRequestReviewMessage"
                    text: root.requestMessage
                    color: Theme.color.neutral9
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignLeft
                }
            }
        }
        }

        ToastBanner {
            objectName: "paymentRequestReviewWarning"
            Layout.fillWidth: true
            Layout.leftMargin: root.contentMargin
            Layout.rightMargin: root.contentMargin
            Layout.bottomMargin: root.contentMargin
            visible: root.replacesValues
            tintColor: Theme.color.orange
            iconSource: "image://images/alert-filled"
            text: qsTr("Applying this replaces values already entered in the form.")
            textObjectName: "paymentRequestReviewWarningText"
        }

        Row {
            Layout.fillWidth: true
            Layout.leftMargin: root.contentMargin
            Layout.rightMargin: root.contentMargin
            Layout.bottomMargin: root.contentMargin
            spacing: 10

            readonly property real buttonWidth: (width - spacing) / 2

            OutlineButton {
                objectName: "paymentRequestReviewDiscardButton"
                width: parent.buttonWidth
                text: qsTr("Discard")
                onClicked: root.close()
            }

            ContinueButton {
                objectName: "paymentRequestReviewApplyButton"
                width: parent.buttonWidth
                text: qsTr("Apply")
                onClicked: {
                    root.accepted = true
                    root.close()
                }
            }
        }
    }
}
