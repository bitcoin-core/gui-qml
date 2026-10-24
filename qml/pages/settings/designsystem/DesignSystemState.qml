// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../../controls"

ColumnLayout {
    id: root

    default property alias content: sample.data
    property string label: ""

    spacing: 6

    ColumnLayout {
        id: sample
        Layout.fillWidth: true
        spacing: 0
    }

    CoreText {
        visible: root.label.length > 0
        Layout.fillWidth: true
        text: root.label
        color: Theme.color.neutral7
        font: Theme.text.monoCaption.font
        lineHeight: Theme.text.monoCaption.lineHeight
        lineHeightMode: Text.FixedHeight
        horizontalAlignment: Text.AlignLeft
        wrap: true
    }
}
