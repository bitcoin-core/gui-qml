// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../controls"

LabeledField {
    id: root

    property var address: null
    property string fieldObjectName: ""
    property alias text: addressInput.inputText
    property alias placeholderText: addressInput.placeholderText
    property alias readOnly: addressInput.readOnly
    readonly property alias field: addressInput.field
    property bool embedded: true
    property var fieldTextStyle: Theme.text.monoDescription

    signal textEdited(string text)
    signal editingFinished()

    BitcoinAddressInputField {
        id: addressInput
        Layout.fillWidth: true
        address: root.address
        inputObjectName: root.fieldObjectName.length > 0
            ? root.fieldObjectName
            : root.objectName.length > 0 ? root.objectName + "Field" : ""
        embedded: root.embedded
        textStyle: root.fieldTextStyle
        showLabel: false
        onTextChanged: root.textEdited(root.text)
        onEditingFinished: root.editingFinished()
    }
}
