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
    signal toggleExpanded()
    signal pasteRequested(string field)
    signal edited()
    signal removeRequested()
    readonly property SendRecipientEditor editorItem: editorLoader.item as SendRecipientEditor
    objectName: "sendRecipientCard_" + index
    implicitWidth: 0
    padding: 12
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    background: Rectangle {
        color: Theme.color.neutral1
        radius: 10
    }
    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            visible: recipientCard.wallet.recipients.count > 1
            Layout.fillWidth: true; spacing: 10
            AbstractButton {
                objectName: "sendEditRecipient_" + recipientCard.index
                Layout.fillWidth: true; implicitHeight: 36
                //: Accessible action to expand or collapse one recipient in the stack.
                Accessible.name: qsTr("Edit recipient %1").arg(recipientCard.index + 1)
                onClicked: recipientCard.toggleExpanded()
                contentItem: RowLayout {
                    spacing: 10
                    Rectangle {
                        implicitWidth: 28; implicitHeight: 28; radius: 14
                        color: recipientCard.expanded ? Theme.color.orange : Theme.color.neutral3
                        CoreText { anchors.centerIn: parent; text: recipientCard.index + 1; font: Theme.text.subheading.font; color: recipientCard.expanded ? "#000000" : Theme.color.neutral9 }
                    }
                    CoreText {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        horizontalAlignment: Text.AlignLeft
                        wrap: false
                        elide: Text.ElideRight
                        font: Theme.text.subheading.font
                        text: recipientCard.recipientObject.label || recipientCard.recipientObject.paymentRequestLabel || qsTr("Recipient %1").arg(recipientCard.index + 1)
                    }
                    BitcoinAmountDisplayLabel { visible: !recipientCard.expanded; amount: recipientCard.recipientObject.amount.localizedDisplay; unit: recipientCard.recipientObject.amount.unitLabel; font: Theme.text.monoDescription.font }
                    Icon {
                        source: "image://images/caret-right"
                        rotation: recipientCard.expanded ? 90 : 0
                        size: 9
                        color: Theme.color.neutral6
                        Behavior on rotation { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
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
                canRemove: recipientCard.wallet.recipients.count > 1
                removeButtonObjectName: "sendRemoveRecipient_" + recipientCard.index
                onPasteRequested: function(field) { recipientCard.pasteRequested(field) }
                onEdited: recipientCard.edited()
                onRemoveRequested: recipientCard.removeRequested()
            }
        }
    }
}
