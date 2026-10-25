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

    title: "Data display"
    description: "Amounts, addresses and QR codes."

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
}
