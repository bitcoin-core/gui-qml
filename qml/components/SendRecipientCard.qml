// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

Pane {
    id: recipientCard
    required property int index
    required property SendRecipient recipientObject
    required property WalletQmlModel wallet
    required property bool expanded
    property bool clipboardRequest: false
    property string clipboardText: ""
    signal toggleExpanded()
    signal paymentUriPasted(int field, string text)
    signal edited()
    signal removeRequested()
    readonly property SendRecipientEditor editorItem: editorLoader.item as SendRecipientEditor
    objectName: "sendRecipientCard_" + index
    implicitWidth: 0
    padding: 12
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    background: Rectangle {
        color: Theme.color.surface1
        radius: Theme.radius.card
    }
    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            visible: recipientCard.wallet.recipients.count > 1
            Layout.fillWidth: true; spacing: 10
            AbstractButton {
                id: recipientToggle
                objectName: "sendEditRecipient_" + recipientCard.index
                Layout.fillWidth: true; implicitHeight: 36
                focusPolicy: Qt.StrongFocus
                //: Accessible action to expand or collapse one recipient in the stack.
                Accessible.name: qsTr("Edit recipient %1").arg(recipientCard.index + 1)
                //: Accessible state of one recipient in the stack.
                Accessible.description: recipientCard.expanded ? qsTr("Expanded") : qsTr("Collapsed")
                onClicked: recipientCard.toggleExpanded()
                background: Item {
                    FocusBorder {
                        objectName: recipientToggle.objectName + "FocusBorder"
                        visible: recipientToggle.visualFocus
                        targetRadius: 6
                    }
                }
                contentItem: RowLayout {
                    spacing: 10
                    Rectangle {
                        implicitWidth: 28; implicitHeight: 28; radius: height / 2
                        color: recipientCard.expanded ? Theme.color.accent : Theme.color.surface3
                        CoreText { anchors.centerIn: parent; text: recipientCard.index + 1; textStyle: Theme.text.subheading; color: recipientCard.expanded ? Theme.color.accentForeground : Theme.color.neutral9 }
                    }
                    CoreText {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        horizontalAlignment: Text.AlignLeft
                        wrap: false
                        elide: Text.ElideRight
                        textStyle: Theme.text.subheading
                        text: recipientCard.recipientObject.label || recipientCard.recipientObject.paymentRequestLabel || qsTr("Recipient %1").arg(recipientCard.index + 1)
                    }
                    BitcoinAmountDisplayLabel { visible: !recipientCard.expanded; amount: recipientCard.recipientObject.amount.localizedDisplay; unit: recipientCard.recipientObject.amount.unitLabel; textStyle: Theme.text.amountDescription }
                    CaretRightIcon {
                        expanded: recipientCard.expanded
                        color: Theme.color.neutral6
                    }
                }
            }
        }
        Loader {
            id: editorLoader
            active: recipientCard.expanded || recipientCard.wallet.recipients.count === 1
            visible: active
            Layout.fillWidth: true
            sourceComponent: SendRecipientEditor {
                wallet: recipientCard.wallet; recipient: recipientCard.recipientObject
                clipboardRequest: recipientCard.clipboardRequest
                clipboardText: recipientCard.clipboardText
                enabled: recipientCard.wallet && recipientCard.wallet.available
                canRemove: recipientCard.wallet.recipients.count > 1
                removeButtonObjectName: "sendRemoveRecipient_" + recipientCard.index
                onPaymentUriPasted: function(field, text) { recipientCard.paymentUriPasted(field, text) }
                onEdited: recipientCard.edited()
                onRemoveRequested: recipientCard.removeRequested()
            }
        }
    }
}
