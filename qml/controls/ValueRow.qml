// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

FormRow {
    id: root

    property string value: ""
    property url valueIconSource: ""
    property int valueIconSize: 18
    property int valueMaximumWidth: 260
    property color valueColor: enabled ? Theme.color.neutral9 : Theme.color.neutral4
    property color valueIconColor: valueColor
    property bool amountValue: false
    property var valueTextStyle: amountValue ? Theme.text.amountDescription : Theme.text.description
    property string valueUnit: ""
    property bool animateUnitChanges: true

    trailingItem: RowLayout {
        spacing: Theme.space.s

        CoreText {
            objectName: !root.amountValue && root.objectName.length > 0 ? root.objectName + "Value" : ""
            visible: !root.amountValue
            Layout.maximumWidth: root.valueMaximumWidth
            text: root.value
            color: root.valueColor
            textStyle: root.valueTextStyle
            horizontalAlignment: Text.AlignRight
            wrap: false
            elide: Text.ElideMiddle
        }

        BitcoinAmountDisplayLabel {
            objectName: root.amountValue && root.objectName.length > 0 ? root.objectName + "Value" : ""
            visible: root.amountValue
            Layout.maximumWidth: root.valueMaximumWidth
            amount: root.value
            unit: root.value === "—" ? "" : root.valueUnit
            animateUnitChanges: root.animateUnitChanges
            color: root.valueColor
            textStyle: root.valueTextStyle
            horizontalAlignment: Text.AlignRight
            wrap: false
            elide: Text.ElideMiddle
        }

        Icon {
            visible: root.valueIconSource.toString().length > 0
            Layout.preferredWidth: visible ? root.valueIconSize : 0
            Layout.preferredHeight: visible ? root.valueIconSize : 0
            source: root.valueIconSource
            color: root.valueIconColor
            size: root.valueIconSize
        }
    }
}
