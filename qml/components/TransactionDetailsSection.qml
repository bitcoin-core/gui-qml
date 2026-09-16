// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0
import QtQuick.Layouts 1.15
import "../controls"

ColumnLayout {
    id: root
    property var details: ({})
    property bool showTransactionId: true
    property bool showBlock: true
    property bool showRawTransaction: true
    readonly property var flow: details.flow || ({})
    readonly property bool coinbase: !!flow.coinbase
    readonly property string unavailable: qsTr("Unavailable")
    readonly property var overviewFields: [
        {key: "Timestamp", title: qsTr("Timestamp"), value: details.timestamp !== undefined && details.timestamp !== null
            ? Qt.formatDateTime(new Date(Number(details.timestamp) * 1000), "yyyy-MM-dd hh:mm:ss") : unavailable},
        {key: "Fee", title: qsTr("Network fee"), value: coinbase ? qsTr("Not applicable")
            : details.feeKnown ? qsTr("%1 sats").arg(Number(details.feeSat).toLocaleString(Qt.locale(), 'f', 0)) : unavailable},
        {key: "FeeRate", title: qsTr("Fee rate"), value: coinbase ? qsTr("Not applicable")
            : details.feeRateSatPerVb !== undefined && details.feeRateSatPerVb !== null
                ? qsTr("%1 sat/vB").arg(Number(details.feeRateSatPerVb).toLocaleString(Qt.locale(), 'f', 2)) : unavailable},
        {key: "Block", title: qsTr("Block"), value: details.statusKnown === false ? unavailable
            : details.blockHeight !== undefined && details.blockHeight !== null && details.depth > 0 ? String(details.blockHeight)
            : details.depth > 0 ? unavailable : qsTr("Not mined"), hidden: !root.showBlock}
    ].filter(function(field) { return !field.hidden })
    readonly property var detailFields: [
        {key: "Size", title: qsTr("Size"), value: details.size > 0 ? qsTr("%1 bytes").arg(details.size) : unavailable},
        {key: "VirtualSize", title: qsTr("Virtual size"), value: details.virtualSize > 0 ? qsTr("%1 vB").arg(details.virtualSize) : unavailable},
        {key: "Weight", title: qsTr("Weight units"), value: details.weight > 0 ? qsTr("%1 WU").arg(details.weight) : unavailable},
        {key: "Version", title: qsTr("Version"), value: details.version !== undefined && details.version !== null ? String(details.version) : unavailable},
        {key: "LockTime", title: qsTr("Locktime"), value: details.lockTime !== undefined && details.lockTime !== null ? String(details.lockTime) : unavailable},
        {key: "Rbf", title: qsTr("Replace by fee"), value: coinbase ? qsTr("Not applicable")
            : details.signalsRbf === undefined || details.signalsRbf === null ? unavailable
            : details.signalsRbf ? qsTr("Signaled") : qsTr("Not signaled")}
    ]

    spacing: 28

    FormSection {
        objectName: "transactionOverviewSection"
        Layout.fillWidth: true
        title: qsTr("Overview")
        sectionSpacing: 12

        FormRow {
            id: idRow
            objectName: "transactionIdRow"
            visible: root.showTransactionId && !!root.details.txid
            Layout.fillWidth: true
            implicitWidth: 0
            minimumRowHeight: 56
            title: qsTr("Transaction ID")
            titleColor: Theme.color.neutral7
            titleTextStyle: Theme.text.caption
            bodyItem: AddressLabel {
                objectName: "transactionIdValue"
                Layout.alignment: Qt.AlignLeft
                Layout.preferredWidth: Math.min(naturalWidth, idRow.availableWidth)
                address: root.details.txid || ""
                textStyle: Theme.text.monoCaption
                embedded: true
                truncateWhenNeeded: true
                leftPadding: 0
                rightPadding: 0
                Accessible.name: showCopiedStatus ? qsTr("Copied") : qsTr("Copy transaction ID")
            }
        }
        FieldsGrid {
            objectName: "transactionOverviewGrid"
            fields: root.overviewFields
            wideColumns: 4
        }
    }

    FormSection {
        objectName: "transactionTechnicalDetailsSection"
        Layout.fillWidth: true
        title: qsTr("Details")
        sectionSpacing: 12
        FieldsGrid {
            objectName: "transactionDetailsGrid"
            fields: root.detailFields
            wideColumns: 3
            lastRowDivider: root.showRawTransaction
        }
        FormRow {
            objectName: "transactionRawRow"
            visible: root.showRawTransaction
            Layout.fillWidth: true
            implicitWidth: 0
            title: qsTr("Raw transaction")
            titleColor: Theme.color.neutral7
            titleTextStyle: Theme.text.caption
            showDivider: false
            trailingItem: CopyButton {
                objectName: "transactionRawCopyButton"
                enabled: !!root.details.rawTransaction
                onCopyRequested: Clipboard.setText(root.details.rawTransaction || "")
            }
            bodyItem: ScrollView {
                id: rawScroll
                objectName: "transactionRawScroll"
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(180, Math.max(24, rawText.contentHeight))
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                TextArea {
                    id: rawText
                    objectName: "transactionRawText"
                    width: rawScroll.availableWidth
                    text: root.visible ? root.details.rawTransaction || "" : ""
                    placeholderText: root.unavailable
                    readOnly: true
                    selectByMouse: true
                    textFormat: TextEdit.PlainText
                    wrapMode: TextEdit.WrapAnywhere
                    font: Theme.text.monoCaption.font
                    color: Theme.color.neutral9
                    placeholderTextColor: Theme.color.neutral6
                    padding: 0
                    background: null
                    Accessible.name: qsTr("Raw transaction")
                }
            }
        }
    }

    component FieldsGrid: GridLayout {
        id: fieldsGrid
        required property var fields
        required property int wideColumns
        property bool lastRowDivider: false
        Layout.fillWidth: true
        columns: width >= 900 ? wideColumns : width >= 520 ? 2 : 1
        columnSpacing: 16
        rowSpacing: 0
        Repeater {
            model: fieldsGrid.fields
            delegate: FormRow {
                id: fieldRow
                required property var modelData
                required property int index
                readonly property string value: modelData.value
                objectName: "transaction" + modelData.key + "Row"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                implicitWidth: 0
                minimumRowHeight: 54
                title: modelData.title
                titleColor: Theme.color.neutral7
                titleTextStyle: Theme.text.caption
                bodyItem: CoreText {
                    objectName: fieldRow.objectName + "Value"
                    Layout.fillWidth: true
                    text: fieldRow.value
                    font: Theme.text.description.font
                    horizontalAlignment: Text.AlignLeft
                    textFormat: Text.PlainText
                    wrap: true
                }
                showDivider: fieldsGrid.lastRowDivider || index < fieldsGrid.fields.length - fieldsGrid.columns
            }
        }
    }
}
