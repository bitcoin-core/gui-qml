// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../../controls"
import "../../../components"

DesignSystemSection {
    id: root

    readonly property string exampleAddress: "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq"

    description: "Amounts, addresses, QR codes, transaction parts and log output."

    DesignSystemSpecimen {
        objectName: "designSystemBitcoinAmountDisplayLabel"
        Layout.fillWidth: true
        name: "BitcoinAmountDisplayLabel"

        DesignSystemState {
            label: "amountBody, BTC"
            BitcoinAmountDisplayLabel {
                amount: "0.00150000"
                unit: "BTC"
                textStyle: Theme.text.amountBody
            }
        }
        DesignSystemState {
            label: "default (amountDescription), sats"
            BitcoinAmountDisplayLabel {
                amount: "150,000"
                unit: "sats"
                color: Theme.color.neutral7
            }
        }
        DesignSystemState {
            label: "amountLead"
            BitcoinAmountDisplayLabel {
                amount: "1.25000000"
                unit: "BTC"
                textStyle: Theme.text.amountLead
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemAddressLabel"
        Layout.fillWidth: true
        name: "AddressLabel"
        note: "Interactive labels copy the address to the clipboard when clicked."

        DesignSystemState {
            width: 400
            label: "rest"
            AddressLabel {
                Layout.fillWidth: true
                address: root.exampleAddress
            }
        }
        DesignSystemState {
            width: 400
            label: "surfaceLevel: 2, monoCaption"
            AddressLabel {
                Layout.fillWidth: true
                address: root.exampleAddress
                surfaceLevel: 2
                textStyle: Theme.text.monoCaption
            }
        }
        DesignSystemState {
            width: 240
            label: "truncated: true"
            AddressLabel {
                Layout.fillWidth: true
                address: root.exampleAddress
                truncated: true
                textStyle: Theme.text.monoCaption
            }
        }
        DesignSystemState {
            width: 240
            label: "interactive: false"
            AddressLabel {
                Layout.fillWidth: true
                address: root.exampleAddress
                interactive: false
                truncated: true
                textStyle: Theme.text.monoCaption
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemSelectableText"
        Layout.fillWidth: true
        name: "SelectableText"
        note: "Read-only text the user can select and copy, such as peer details, log lines and raw transactions."

        DesignSystemState {
            width: 400
            label: "monoCaption, WrapAnywhere"
            SelectableText {
                Layout.fillWidth: true
                textStyle: Theme.text.monoCaption
                wrapMode: TextEdit.WrapAnywhere
                text: "0200000001e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855000000006a47"
            }
        }
        DesignSystemState {
            width: 400
            label: "placeholderText"
            SelectableText {
                Layout.fillWidth: true
                placeholderText: "Unavailable"
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemQRImage"
        Layout.fillWidth: true
        name: "QRImage"

        DesignSystemState {
            label: "code"
            QRImage {
                Layout.preferredWidth: 160
                Layout.preferredHeight: 160
                code: "bitcoin:" + root.exampleAddress
                backgroundColor: Theme.color.qrBackground
                foregroundColor: Theme.color.qrForeground
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemTransactionSummary"
        Layout.fillWidth: true
        name: "TransactionSummary"
        note: "The amount and subtitle at the top of transaction details and the send review."

        DesignSystemState {
            width: 400
            label: "amount, subtitle"
            TransactionSummary {
                Layout.fillWidth: true
                amount: "0.00150000 BTC"
                subtitle: "Received · 10/23/26 10:13 PM"
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemTransactionFlowNode"
        Layout.fillWidth: true
        name: "TransactionFlowNode"
        note: "One input or output card of the transaction flow; a request button links an output to its payment request."

        DesignSystemState {
            width: 300
            label: "input, wallet-owned"
            TransactionFlowNode {
                Layout.fillWidth: true
                input: true
                accentColor: Theme.color.txOutgoing
                entry: ({ ownership: "wallet", address: root.exampleAddress, amountKnown: true, amountSat: 250000 })
            }
        }
        DesignSystemState {
            width: 300
            label: "output with a payment request"
            TransactionFlowNode {
                Layout.fillWidth: true
                accentColor: Theme.color.txIncoming
                entry: ({ address: root.exampleAddress, amountKnown: true, amountSat: 150000,
                          paymentRequests: [{ requestId: 7 }] })
            }
        }
        DesignSystemState {
            width: 300
            label: "kind: fee"
            TransactionFlowNode {
                Layout.fillWidth: true
                accentColor: Theme.color.neutral6
                entry: ({ kind: "fee", amountKnown: true, amountSat: 1410 })
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemTransactionDetailsGrid"
        Layout.fillWidth: true
        name: "TransactionDetailsGrid"
        note: "Label and value cells in up to wideColumns columns; mono fields use monoDescription."

        DesignSystemState {
            width: parent.width
            label: "wideColumns: 3"
            TransactionDetailsGrid {
                wideColumns: 3
                fields: [
                    { key: "Size", title: "Size", value: "225 bytes", mono: true },
                    { key: "VirtualSize", title: "Virtual size", value: "144 vB", mono: true },
                    { key: "Rbf", title: "Replace by fee", value: "Signaled" }
                ]
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemDebugLogTitlesHeader"
        Layout.fillWidth: true
        name: "DebugLogTitlesHeader"
        note: "The debug log table: a titles header over rows that alternate their fill and mark warnings and errors."

        DesignSystemState {
            width: parent.width
            label: "header, rows: plain, alternate, isWarning, isError"
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                DebugLogTitlesHeader {
                    Layout.fillWidth: true
                }
                DebugLogItemRow {
                    objectName: "designSystemDebugLogItemRow"
                    Layout.fillWidth: true
                    timestamp: "10:13:02"
                    message: "UpdateTip: new best=00000000000000000002a7c4 height=870000"
                }
                DebugLogItemRow {
                    Layout.fillWidth: true
                    alternate: true
                    timestamp: "10:13:05"
                    message: "New outbound peer connected"
                }
                DebugLogItemRow {
                    Layout.fillWidth: true
                    isWarning: true
                    timestamp: "10:13:09"
                    message: "Low disk space"
                }
                DebugLogItemRow {
                    Layout.fillWidth: true
                    alternate: true
                    isError: true
                    timestamp: "10:13:12"
                    message: "Failed to open block file"
                }
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemMonospaceOutputView"
        Layout.fillWidth: true
        name: "MonospaceOutputView"
        note: "Selectable monospace output with optional side columns, as in the RPC console."

        DesignSystemState {
            width: parent.width
            label: "contentRole, leftColumnRole, categoryRole"
            MonospaceOutputView {
                Layout.fillWidth: true
                Layout.preferredHeight: 140
                accessibleName: "Sample console output"
                listModel: ListModel {
                    ListElement { timestamp: "10:13"; content: "getblockcount"; category: 0 }
                    ListElement { timestamp: "10:13"; content: "870000"; category: 1 }
                    ListElement { timestamp: "10:14"; content: "Method not found (code -32601)"; category: 2 }
                }
                contentRole: "content"
                leftColumnRole: "timestamp"
                categoryRole: "category"
                errorContentColor: Theme.color.dangerText
            }
        }
    }
}
