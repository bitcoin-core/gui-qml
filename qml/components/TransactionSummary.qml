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

    spacing: 12
    Loader {
        visible: !!root.iconComponent
        sourceComponent: root.iconComponent
        Layout.preferredWidth: 64
        Layout.preferredHeight: 64
        Layout.alignment: Qt.AlignHCenter
    }
    RowLayout {
        Layout.fillWidth: false
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: root.width
        spacing: 12
        CoreText {
            objectName: "transactionSummaryAmount"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignBaseline
            text: root.amountValue
            color: root.amountColor
            font.family: Theme.text.monoLead.font.family
            font.styleName: Theme.text.monoLead.font.styleName
            font.pixelSize: 48
            font.letterSpacing: -1.5
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 22
            wrap: false
        }
        CoreText {
            visible: root.amountUnit.length > 0
            Layout.alignment: Qt.AlignBaseline
            text: root.amountUnit
            color: Theme.color.neutral7
            font.pixelSize: root.width < 500 ? 18 : 22
            wrap: false
        }
    }
    CoreText {
        visible: root.subtitle.length > 0
        Layout.fillWidth: true
        text: root.subtitle
        color: Theme.color.neutral7
        font: Theme.text.description.font
    }
    Loader {
        visible: !!root.statusComponent
        sourceComponent: root.statusComponent
        Layout.alignment: Qt.AlignHCenter
    }
}
