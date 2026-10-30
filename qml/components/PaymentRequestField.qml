// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../controls"

// A payment request detail: shows the saved value until edited.
TextFieldRow {
    id: root
    property alias label: root.title
    property bool editable: true
    property bool hidesAmount: false
    readonly property bool amountHidden: hidesAmount && optionsModel.hideAmounts
    echoMode: amountHidden && !field.activeFocus ? TextInput.NoEcho : TextInput.Normal

    Binding {
        target: root.field.Accessible
        property: "description"
        value: root.amountHidden && !root.field.activeFocus ? qsTr("Hidden amount") : root.description
    }

    BitcoinAmountDisplayLabelBase {
        parent: root.field
        anchors.fill: parent
        anchors.leftMargin: root.field.leftPadding
        anchors.rightMargin: root.field.rightPadding
        visible: root.amountHidden && !root.field.activeFocus
        hidesAmount: true
        textStyle: root.fieldTextStyle
        horizontalAlignment: root.textAlignment
        color: root.editable ? Theme.color.neutral9 : Theme.color.disabledContent
        Accessible.ignored: true
    }
    property alias unitControl: root.trailingAccessory

    tracksValue: true
    enabled: editable
    showDivider: false
    minimumRowHeight: 56
    titleColor: editable ? Theme.color.neutral7 : Theme.color.disabledContent
    fieldHeight: Theme.size.control
    fieldWidth: Math.max(80, Math.min(260, width * 0.53
        - (trailingAccessoryWidth > 0 ? trailingAccessoryWidth + Theme.space.xs : 0)))
    // FormRow's minimum; deriving it from the width-based field width is a
    // binding loop until a layout sets the width.
    implicitWidth: 320
    onEditableChanged: if (!editable) reset()
}
