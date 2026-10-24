// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../../../controls"
import "../../../components"

DesignSystemSection {
    id: root

    readonly property int fieldWidth: 320
    readonly property string exampleAddress: "bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq"
    property string exampleBlockStorageLimit: "2"
    property string exampleProxyAddress: "127.0.0.1:9050"

    property BitcoinAddress sampleAddress: BitcoinAddress {}
    // Set from script: Qt 6.5 and older reject a literal for a qint64 property.
    property BitcoinAmount sampleAmount: BitcoinAmount {}

    Component.onCompleted: {
        sampleAddress.setAddress(exampleAddress)
        sampleAmount.satoshi = 150000
    }

    title: "Fields"
    description: "Text entry controls with placeholder, value, error and disabled states where the control has them."

    DesignSystemSpecimen {
        objectName: "designSystemLabeledTextField"
        Layout.fillWidth: true
        name: "LabeledTextField"

        DesignSystemState {
            width: root.fieldWidth
            label: "placeholderText"
            LabeledTextField { Layout.fillWidth: true; label: "Wallet name"; placeholderText: "My wallet" }
        }
        DesignSystemState {
            width: root.fieldWidth
            label: "text, supportingText"
            LabeledTextField { Layout.fillWidth: true; label: "Wallet name"; text: "Savings"; supportingText: "Shown in the wallet list." }
        }
        DesignSystemState {
            width: root.fieldWidth
            label: "errorText"
            LabeledTextField { Layout.fillWidth: true; label: "Wallet name"; text: "Savings"; errorText: "A wallet with this name already exists." }
        }
        DesignSystemState {
            width: root.fieldWidth
            label: "enabled: false"
            LabeledTextField { Layout.fillWidth: true; label: "Wallet name"; text: "Savings"; enabled: false }
        }
        DesignSystemState {
            width: root.fieldWidth
            label: "fieldSurface: true"
            LabeledTextField { Layout.fillWidth: true; label: "Label"; placeholderText: "Optional"; fieldSurface: true }
        }
        DesignSystemState {
            width: root.fieldWidth
            label: "fieldSurface, errorText"
            LabeledTextField { Layout.fillWidth: true; label: "Label"; text: "Rent"; fieldSurface: true; errorText: "Too long." }
        }
        DesignSystemState {
            width: root.fieldWidth
            label: "showCopyButton, actionText"
            LabeledTextField {
                Layout.fillWidth: true
                label: "Message"
                text: "Hello"
                showCopyButton: true
                actionText: "Clear"
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemPasswordTextField"
        Layout.fillWidth: true
        name: "PasswordTextField"

        DesignSystemState {
            width: root.fieldWidth
            label: "placeholderText"
            PasswordTextField { Layout.fillWidth: true; label: "Password"; placeholderText: "Enter password" }
        }
        DesignSystemState {
            width: root.fieldWidth
            label: "text"
            PasswordTextField { Layout.fillWidth: true; label: "Password"; text: "correct horse" }
        }
        DesignSystemState {
            width: root.fieldWidth
            label: "errorText"
            PasswordTextField { Layout.fillWidth: true; label: "Password"; text: "wrong"; errorText: "Incorrect password." }
        }
        DesignSystemState {
            width: root.fieldWidth
            label: "enabled: false"
            PasswordTextField { Layout.fillWidth: true; label: "Password"; text: "correct horse"; enabled: false }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemLabeledTextArea"
        Layout.fillWidth: true
        name: "LabeledTextArea"

        DesignSystemState {
            width: root.fieldWidth
            label: "fieldSurface, placeholderText"
            LabeledTextArea { Layout.fillWidth: true; fieldSurface: true; label: "Note to self"; placeholderText: "Add a note" }
        }
        DesignSystemState {
            width: root.fieldWidth
            label: "text, supportingText"
            LabeledTextArea { Layout.fillWidth: true; fieldSurface: true; label: "Note to self"; text: "Rent for March"; supportingText: "Recommended" }
        }
        DesignSystemState {
            width: root.fieldWidth
            label: "errorText"
            LabeledTextArea { Layout.fillWidth: true; fieldSurface: true; label: "Extended public key (xpub)"; text: "xpub123"; errorText: "Invalid extended public key." }
        }
        DesignSystemState {
            width: root.fieldWidth
            label: "enabled: false"
            LabeledTextArea { Layout.fillWidth: true; fieldSurface: true; label: "Note to self"; text: "Rent for March"; enabled: false }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemLabeledTextView"
        Layout.fillWidth: true
        name: "LabeledTextView"

        DesignSystemState {
            width: root.fieldWidth
            label: "placeholderText"
            LabeledTextView { Layout.fillWidth: true; label: "Message"; placeholderText: "Enter a message to sign"; fieldHeight: 96 }
        }
        DesignSystemState {
            width: root.fieldWidth
            label: "text, readOnly"
            LabeledTextView { Layout.fillWidth: true; label: "Signature"; text: "H1b2c3d4e5f6g7h8i9j0"; readOnly: true; fieldHeight: 96 }
        }
        DesignSystemState {
            width: root.fieldWidth
            label: "errorText"
            LabeledTextView { Layout.fillWidth: true; label: "Message"; text: "Hello"; errorText: "Message could not be signed."; fieldHeight: 96 }
        }
        DesignSystemState {
            width: root.fieldWidth
            label: "enabled: false"
            LabeledTextView { Layout.fillWidth: true; label: "Message"; text: "Hello"; enabled: false; fieldHeight: 96 }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemSearchBar"
        Layout.fillWidth: true
        name: "SearchBar"

        DesignSystemState {
            label: "placeholderText"
            SearchBar { placeholderText: "Search transactions" }
        }
        DesignSystemState {
            label: "text"
            SearchBar { placeholderText: "Search transactions"; text: "coffee" }
        }
        DesignSystemState {
            label: "showNavigationButtons"
            SearchBar { placeholderText: "Find in log"; text: "error"; showNavigationButtons: true }
        }
        DesignSystemState {
            label: "enabled: false"
            SearchBar { placeholderText: "Search transactions"; enabled: false }
        }
    }

    FormSection {
        objectName: "designSystemTextFieldRowSection"
        Layout.fillWidth: true
        title: "TextFieldRow"
        description: "Compact trailing editors for short values in a FormSection."

        TextFieldRow {
            objectName: "designSystemBlockStorageRow"
            fieldObjectName: "designSystemBlockStorageField"
            Layout.fillWidth: true
            title: "Block storage limit (GB)"
            fieldWidth: 72
            text: root.exampleBlockStorageLimit
            validator: IntValidator {
                bottom: 1
            }
            onTextEdited: function(text) {
                root.exampleBlockStorageLimit = text
            }
        }

        TextFieldRow {
            objectName: "designSystemProxyLocationRow"
            fieldObjectName: "designSystemProxyLocationField"
            Layout.fillWidth: true
            title: "Proxy location"
            fieldWidth: 200
            text: root.exampleProxyAddress
            onTextEdited: function(text) {
                root.exampleProxyAddress = text
            }
        }

        TextFieldRow {
            Layout.fillWidth: true
            title: "Placeholder"
            placeholderText: "127.0.0.1:9050"
        }

        TextFieldRow {
            Layout.fillWidth: true
            title: "Error"
            description: "errorText comes from FormRow."
            errorText: "Enter a host and port."
            text: "localhost"
        }

        TextFieldRow {
            Layout.fillWidth: true
            title: "Read only"
            readOnly: true
            text: "8333"
        }

        TextFieldRow {
            Layout.fillWidth: true
            title: "Disabled"
            showDivider: false
            enabled: false
            text: "8333"
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemBitcoinAddressInputField"
        Layout.fillWidth: true
        name: "BitcoinAddressInputField"

        DesignSystemState {
            width: 400
            label: "placeholderText"
            BitcoinAddressInputField { Layout.fillWidth: true }
        }
        DesignSystemState {
            width: 400
            label: "address"
            BitcoinAddressInputField { Layout.fillWidth: true; address: root.sampleAddress }
        }
        DesignSystemState {
            width: 400
            label: "embedded: true"
            BitcoinAddressInputField { Layout.fillWidth: true; embedded: true; showLabel: false }
        }
        DesignSystemState {
            width: 400
            label: "errorText"
            BitcoinAddressInputField { Layout.fillWidth: true; errorText: "Invalid address." }
        }
        DesignSystemState {
            width: 400
            label: "enabled: false"
            BitcoinAddressInputField { Layout.fillWidth: true; address: root.sampleAddress; enabled: false }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemBitcoinAmountInputField"
        Layout.fillWidth: true
        name: "BitcoinAmountInputField"
        note: "The unit toggle writes the display unit setting, so the sample with an amount is disabled."

        DesignSystemState {
            width: 400
            label: "placeholderText"
            BitcoinAmountInputField { Layout.fillWidth: true }
        }
        DesignSystemState {
            width: 400
            label: "errorText"
            BitcoinAmountInputField { Layout.fillWidth: true; errorText: "Amount exceeds your balance." }
        }
        DesignSystemState {
            width: 400
            label: "amount, enabled: false"
            BitcoinAmountInputField { Layout.fillWidth: true; amount: root.sampleAmount; enabled: false }
        }
    }
}
