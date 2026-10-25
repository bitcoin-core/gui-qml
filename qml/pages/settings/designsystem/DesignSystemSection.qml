// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../../controls"

ColumnLayout {
    id: root

    default property alias content: contentColumn.data
    property string description: ""
    property alias contentSpacing: contentColumn.spacing

    spacing: Theme.space.page

    CoreText {
        visible: root.description.length > 0
        Layout.fillWidth: true
        Layout.leftMargin: Theme.space.xs
        Layout.rightMargin: Theme.space.xs
        Layout.maximumWidth: 620
        text: root.description
        color: Theme.color.neutral7
        textStyle: Theme.text.body
        horizontalAlignment: Text.AlignLeft
        wrap: true
    }

    ColumnLayout {
        id: contentColumn
        Layout.fillWidth: true
        spacing: 24
    }
}
