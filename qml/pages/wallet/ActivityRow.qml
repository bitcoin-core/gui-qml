// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../../controls"
import "../../components"

Item {
    id: root
    required property string activityId
    required property string txid
    required property string requestId
    required property string label
    required property string address
    required property string amount
    required property double netAmountSat
    required property string dateTimeLabel
    required property int activityType
    required property int status
    required property int depth
    required property int blocksToMaturity
    required property bool statusKnown
    required property bool isInactive
    required property bool isPendingRequest
    required property string replacedByTxid
    required property bool hasPaymentRequest
    required property var actions

    property bool firstInSection: true
    property bool lastInSection: true
    property bool isCompact: false
    readonly property bool compact: width < 560
    readonly property bool hasChildren: actions.length > 1
    readonly property bool showChildren: hasChildren && !isCompact
    // Group headings are presentation only; they never become a stored note
    // or the label passed to transaction details.
    readonly property string displayLabel: {
        if (label.trim().length > 0) return label
        switch (activityType) {
        case TransactionActivityModel.Multiple: return qsTr("Multiple actions")
        case TransactionActivityModel.Consolidation: return qsTr("Consolidation")
        case TransactionActivityModel.Split: return qsTr("Split")
        case TransactionActivityModel.InternalTransfer: return qsTr("Sent to yourself")
        default: return ""
        }
    }
    readonly property bool hasLabel: displayLabel.length > 0
    readonly property bool hasAmount: !isPendingRequest || netAmountSat > 0
    readonly property bool incoming: isPendingRequest || activityType === TransactionActivityModel.Receive
        || activityType === TransactionActivityModel.Mined || netAmountSat > 0
    readonly property real rowPadding: compact ? 12 : 20
    readonly property real rowGap: compact ? 12 : 16
    readonly property real amountWidth: compact ? 0 : Math.min(210, width * 0.32)
    readonly property color amountColor: isInactive ? Theme.color.neutral6
        : isPendingRequest ? Theme.color.neutral7
        : netAmountSat > 0 ? Theme.color.green : Theme.color.neutral9
    readonly property string metadata: {
        const parts = [dateTimeLabel]
        if (isPendingRequest) {
            parts.push(qsTr("Payment request"), qsTr("Awaiting payment"))
        } else if (isInactive) {
            parts.push(replacedByTxid.length > 0 ? qsTr("Replaced")
                : status === Transaction.Abandoned ? qsTr("Cancelled")
                : status === Transaction.Conflicted ? qsTr("Conflicted") : qsTr("Not accepted"))
        } else if (!statusKnown) {
            parts.push(qsTr("Updating status…"))
        } else {
            if (depth === 0) parts.push(qsTr("Unconfirmed"))
            else if (depth < 6) parts.push(depth === 1 ? qsTr("1 confirmation") : qsTr("%1 confirmations").arg(depth))
            if (status === Transaction.Immature) parts.push(qsTr("Matures in %1 blocks").arg(blocksToMaturity))
        }
        if (isCompact && hasChildren) parts.push(qsTr("%1 recipients").arg(actions.length))
        return parts.join(" · ")
    }

    signal activated(string txid, string requestId, bool isRequest)
    signal contextMenuRequested(real x, real y)

    MouseArea {
        anchors.fill: parent
        z: 1
        acceptedButtons: Qt.RightButton
        onClicked: function(mouse) { root.contextMenuRequested(mouse.x, mouse.y) }
    }

    objectName: isPendingRequest ? "activityRequest_" + requestId : "activityItem_" + txid
    implicitHeight: parentContent.height + childrenColumn.height + (showChildren ? 12 : 0)
    height: implicitHeight

    Rectangle {
        id: rowSurface
        objectName: "activityRowBackground"
        anchors.fill: parent
        color: parentButton.hovered || parentButton.down ? Theme.color.neutral2 : Theme.color.neutral1
        radius: 16
        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 16
            color: parent.color
            visible: !root.firstInSection
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 16
            color: parent.color
            visible: !root.lastInSection
        }
    }

    AbstractButton {
        id: parentButton
        objectName: "activityRowOpenButton"
        anchors.fill: parent
        padding: 0
        hoverEnabled: AppMode.isDesktop
        focusPolicy: Qt.StrongFocus
        Accessible.role: Accessible.ListItem
        Accessible.name: root.hasLabel ? root.displayLabel : root.address || qsTr("Transaction")
        Accessible.description: [root.hasAmount ? root.amount : "", root.address, root.metadata,
            root.hasChildren ? qsTr("%1 actions").arg(root.actions.length) : ""].filter(function(value) { return value.length > 0 }).join(". ")
        onClicked: root.activated(root.txid, root.requestId, root.isPendingRequest)
        HoverHandler { cursorShape: Qt.PointingHandCursor }
        background: Item {
            FocusBorder {
                visible: parentButton.visualFocus
                borderRadius: 16
                topMargin: 2
                bottomMargin: 2
                leftMargin: 2
                rightMargin: 2
            }
        }
    }

    Item {
        id: parentContent
        parent: parentButton
        readonly property real topPadding: root.isCompact ? 6 : root.hasChildren ? 18 : 8
        readonly property real bottomPadding: root.isCompact ? 6 : 8
        width: parent.width
        height: Math.max(root.isCompact ? 0 : root.hasChildren ? 48 : 68, parentLayout.implicitHeight) + topPadding + bottomPadding

        RowLayout {
            id: parentLayout
            anchors.fill: parent
            anchors.leftMargin: root.rowPadding
            anchors.rightMargin: root.rowPadding
            anchors.topMargin: parentContent.topPadding
            anchors.bottomMargin: parentContent.bottomPadding
            spacing: root.rowGap
            ActivityIcon {
                objectName: "activityRowIcon"
                iconSize: root.isCompact ? 18 : 24
                Layout.preferredWidth: root.isCompact ? 32 : 44
                Layout.preferredHeight: root.isCompact ? 32 : 44
                Layout.alignment: Qt.AlignVCenter
                activityType: root.activityType
                paymentRequest: root.isPendingRequest
                incoming: root.incoming
                pending: root.isPendingRequest || (root.statusKnown && root.depth === 0)
                inactive: root.isInactive
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 3
                Item {
                    id: titleLine
                    visible: root.hasLabel
                    Layout.fillWidth: true
                    implicitHeight: Math.max(parentLabel.implicitHeight, 18)
                    CoreText {
                        id: parentLabel
                        objectName: "activityRowLabel"
                        width: Math.min(implicitWidth, Math.max(0, parent.width - (requestBadge.visible ? 26 : 0)))
                        text: root.displayLabel
                        font: Theme.text.subheading.font
                        lineHeight: Theme.text.description.lineHeight
                        lineHeightMode: Text.FixedHeight
                        color: root.isInactive ? Theme.color.neutral6 : Theme.color.neutral9
                        horizontalAlignment: Text.AlignLeft
                        wrap: false
                        elide: Text.ElideRight
                    }
                    ActivityRequestBadge {
                        id: requestBadge
                        parent: root.hasLabel ? titleLine : addressLine
                        objectName: "activityRowRequestBadge"
                        visible: root.hasPaymentRequest && !root.isPendingRequest
                        inactive: root.isInactive || !root.statusKnown
                        surfaceColor: rowSurface.color
                        x: (root.hasLabel ? parentLabel.width : addressText.width) + 8
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Item {
                    id: addressLine
                    visible: !root.hasChildren && root.address.length > 0 && (!root.isCompact || !root.hasLabel)
                    Layout.fillWidth: true
                    implicitHeight: addressText.implicitHeight
                    ActivityAddress {
                        id: addressText
                        objectName: "activityRowAddress"
                        width: Math.min(displayWidth, Math.max(0, parent.width - (!root.hasLabel && requestBadge.visible ? 26 : 0)))
                        address: root.address
                        compact: root.compact
                        primaryColor: root.isInactive ? Theme.color.neutral6 : Theme.color.neutral9
                        secondaryColor: root.isInactive ? Theme.color.neutral6 : Theme.color.neutral7
                    }
                }
                CoreText {
                    objectName: "activityRowMetadata"
                    Layout.fillWidth: true
                    text: root.metadata
                    font: Theme.text.caption.font
                    lineHeight: Theme.text.caption.lineHeight
                    lineHeightMode: Text.FixedHeight
                    color: Theme.color.neutral7
                    horizontalAlignment: Text.AlignLeft
                }
                AmountText {
                    objectName: "activityRowCompactAmount"
                    visible: root.compact && root.hasAmount
                    Layout.fillWidth: true
                    text: root.amount
                    color: root.amountColor
                    horizontalAlignment: Text.AlignLeft
                }
            }
            AmountText {
                objectName: "activityRowAmount"
                visible: !root.compact && root.hasAmount
                Layout.preferredWidth: root.amountWidth
                text: root.amount
                color: root.amountColor
            }
            Item {
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
                CaretRightIcon {
                    anchors.centerIn: parent
                    color: Theme.color.neutral6
                    size: 14
                }
            }
        }
    }

    Column {
        id: childrenColumn
        parent: parentButton
        objectName: "activityRowChildren"
        anchors.top: parentContent.bottom
        width: parent.width
        Repeater {
            model: root.showChildren ? root.actions : []
            delegate: Item {
                id: childRow
                required property var modelData
                objectName: "activityAction_" + modelData.actionId
                width: childrenColumn.width
                height: childLayout.implicitHeight + 8
                readonly property string actionLabel: modelData.label.trim().length > 0 ? modelData.label : ""
                Accessible.role: Accessible.ListItem
                Accessible.name: actionLabel || modelData.address || qsTr("Wallet contribution")
                Accessible.description: modelData.address + ". " + modelData.amount
                RowLayout {
                    id: childLayout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: root.rowPadding + 34 // Right edge of the 24px image inside the parent's 44px circle.
                    anchors.rightMargin: root.rowPadding
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Item {
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16
                        Icon {
                            objectName: "activityActionIcon"
                            readonly property bool internal: childRow.modelData.direction === TransactionActivityModel.InternalAction
                            readonly property bool incoming: childRow.modelData.direction === TransactionActivityModel.ReceiveAction
                            source: incoming || internal ? "qrc:/icons/activity-receive.svg" : "qrc:/icons/activity-send.svg"
                            color: root.isInactive ? Theme.color.neutral6 : internal ? Theme.color.purple
                                : incoming ? Theme.color.green : Theme.color.orange
                            rotation: internal ? -90 : 0
                            size: 24
                            // Offset the asset's transparent padding to align the glyph with the child inset.
                            x: incoming || internal ? -7 : -6.23
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 0
                        Item {
                            id: childTitleLine
                            visible: childRow.actionLabel.length > 0
                            Layout.fillWidth: true
                            implicitHeight: Math.max(childLabel.implicitHeight, 18)
                            CoreText {
                                id: childLabel
                                objectName: "activityActionLabel"
                                width: Math.min(implicitWidth, Math.max(0, parent.width - (childBadge.visible ? 26 : 0)))
                                text: childRow.actionLabel
                                font.family: Theme.text.caption.family
                                font.styleName: Theme.text.caption.styleName
                                font.pixelSize: 14
                                lineHeight: 20
                                lineHeightMode: Text.FixedHeight
                                color: root.isInactive ? Theme.color.neutral6 : Theme.color.neutral9
                                horizontalAlignment: Text.AlignLeft
                                wrap: false
                                elide: Text.ElideRight
                            }
                            ActivityRequestBadge {
                                id: childBadge
                                objectName: "activityActionRequestBadge"
                                parent: childRow.actionLabel.length > 0 ? childTitleLine : childAddressLine
                                visible: childRow.modelData.hasPaymentRequest
                                inactive: root.isInactive || !root.statusKnown
                                surfaceColor: rowSurface.color
                                x: (childRow.actionLabel.length > 0 ? childLabel.width : childAddressText.width) + 8
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Item {
                            id: childAddressLine
                            Layout.fillWidth: true
                            visible: childRow.modelData.address.length > 0
                            implicitHeight: childAddressText.implicitHeight
                            ActivityAddress {
                                id: childAddressText
                                objectName: "activityActionAddress"
                                width: Math.min(displayWidth, Math.max(0, parent.width - (childRow.actionLabel.length === 0 && childBadge.visible ? 26 : 0)))
                                address: childRow.modelData.address
                                compact: root.compact
                                primaryColor: root.isInactive ? Theme.color.neutral6 : Theme.color.neutral9
                                secondaryColor: root.isInactive ? Theme.color.neutral6 : Theme.color.neutral7
                            }
                        }
                        CoreText {
                            objectName: "activityActionDirection"
                            Layout.fillWidth: true
                            text: childRow.modelData.direction === TransactionActivityModel.ReceiveAction ? qsTr("Received")
                                : childRow.modelData.direction === TransactionActivityModel.InternalAction ? qsTr("Internal") : qsTr("Sent")
                            font.family: Theme.text.caption.family
                            font.styleName: Theme.text.caption.styleName
                            font.pixelSize: 12
                            lineHeight: 17
                            lineHeightMode: Text.FixedHeight
                            color: Theme.color.neutral6
                            horizontalAlignment: Text.AlignLeft
                        }
                        AmountText {
                            visible: root.compact
                            Layout.fillWidth: true
                            text: childRow.modelData.amount
                            color: Theme.color.neutral7
                            horizontalAlignment: Text.AlignLeft
                            font.pixelSize: 14
                            lineHeight: 20
                        }
                    }
                    AmountText {
                        objectName: "activityActionAmount"
                        visible: !root.compact
                        Layout.preferredWidth: root.amountWidth
                        text: childRow.modelData.amount
                        color: Theme.color.neutral7
                        font.pixelSize: 14
                        lineHeight: 20
                    }
                    // Keep amounts aligned with the parent despite the tighter child spacing.
                    Item {
                        Layout.preferredWidth: 24 + root.rowGap - childLayout.spacing
                        Layout.preferredHeight: 1
                    }
                }
            }
        }
    }
    Separator {
        objectName: "activityTransactionDivider"
        visible: !root.lastInSection
        anchors.bottom: parent.bottom
        width: parent.width
        color: Theme.color.neutral2
    }

    component ActivityAddress: AddressLabel {
        property bool compact: false
        interactive: false
        truncated: true
        leadingCharacterCount: compact ? 4 : 8
        trailingCharacterCount: compact ? 4 : 8
        textStyle: Theme.text.monoCaption
        leftPadding: 0
        rightPadding: 0
        topPadding: 0
        bottomPadding: 0
        Accessible.ignored: true // The transaction/action already exposes the full address.
    }

    component AmountText: CoreText {
        font.family: optionsModel.moneyFont.family
        font.weight: optionsModel.moneyFont.weight
        font.pixelSize: Theme.text.description.pixelSize
        lineHeight: Theme.text.description.lineHeight
        lineHeightMode: Text.FixedHeight
        horizontalAlignment: Text.AlignRight
        wrap: false
        elide: Text.ElideRight
    }
}
