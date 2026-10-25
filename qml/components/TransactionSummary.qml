// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../controls"

// Display-only summary, also usable before a transaction has been broadcast.
ColumnLayout {
    id: root
    property string amount: ""
    property string subtitle: ""
    property color amountColor: Theme.color.neutral9
    property Component iconComponent
    property Component statusComponent
    readonly property int unitSeparator: amount.lastIndexOf(" ")
    readonly property string amountValue: unitSeparator < 0 ? amount : amount.substring(0, unitSeparator)
    readonly property string amountUnit: unitSeparator < 0 ? "" : amount.substring(unitSeparator + 1)

    spacing: Theme.space.ml
    Loader {
        visible: !!root.iconComponent
        sourceComponent: root.iconComponent
        Layout.preferredWidth: 64
        Layout.preferredHeight: 64
        Layout.alignment: Qt.AlignHCenter
    }
    Item {
        Layout.fillWidth: true
        implicitHeight: amountRow.implicitHeight

        RowLayout {
            id: amountRow
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(parent.width, amountText.implicitWidth
                + (amountUnit.visible ? spacing + amountUnit.implicitWidth : 0))
            spacing: Theme.space.ml

            BitcoinAmountDisplayLabel {
                id: amountText
                objectName: "transactionSummaryAmount"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignBaseline
                // Amounts being reviewed or confirmed never roll through other digits.
                animateUnitChanges: false
                text: root.amountValue
                color: root.amountColor
                textStyle: Theme.text.amountLead
                font.pixelSize: 48
                font.letterSpacing: -1.5
                // The role's fixed line height is for its own 24 px size; at 48 px
                // it puts the baseline the unit aligns to below the digits.
                lineHeightMode: Text.ProportionalHeight
                lineHeight: 1
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 22
                wrap: false
            }
            BitcoinAmountDisplayLabel {
                id: amountUnit
                objectName: "transactionSummaryUnit"
                visible: root.amountUnit.length > 0
                animateUnitChanges: false
                Layout.alignment: Qt.AlignBaseline
                text: root.amountUnit
                color: Theme.color.neutral7
                textStyle: root.width < 500 ? Theme.text.body : Theme.text.bodyLarge
                wrap: false
            }
        }
    }
    CoreText {
        visible: root.subtitle.length > 0
        Layout.fillWidth: true
        text: root.subtitle
        color: Theme.color.neutral7
        textStyle: Theme.text.description
    }
    Loader {
        visible: !!root.statusComponent
        sourceComponent: root.statusComponent
        Layout.alignment: Qt.AlignHCenter
    }
}
