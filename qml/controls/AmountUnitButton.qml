// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0

NeutralButton {
    id: root

    property int unit: BitcoinAmount.BTC
    property alias labelObjectName: label.objectName
    property alias iconObjectName: unitIcon.objectName

    text: unit === BitcoinAmount.SAT ? qsTr("sats")
        : unit === BitcoinAmount.MilliBTC ? "mBTC"
        : unit === BitcoinAmount.MicroBTC ? qsTr("bits") : "BTC"
    implicitHeight: 32
    textStyle: Theme.text.caption
    padding: Theme.space.s
    leftPadding: Theme.space.m
    rightPadding: Theme.space.m
    Accessible.name: qsTr("Change amount unit")

    contentItem: RowLayout {
        spacing: Theme.space.xs
        CoreText {
            id: label
            text: root.text
            textStyle: root.textStyle
            color: root.textColor
        }
        Icon {
            id: unitIcon
            Layout.minimumWidth: 12
            Layout.preferredWidth: 12
            Layout.maximumWidth: 12
            Layout.minimumHeight: 12
            Layout.preferredHeight: 12
            Layout.maximumHeight: 12
            source: "qrc:/icons/arrow-up-down.svg"
            size: Theme.size.icon.xs
            color: root.textColor
        }
    }
}
