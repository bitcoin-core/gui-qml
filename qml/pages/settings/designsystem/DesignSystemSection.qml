// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../../controls"

ColumnLayout {
    id: root

    default property alias content: contentColumn.data
    property string title: ""
    property string description: ""

    spacing: 8

    ColumnLayout {
        Layout.fillWidth: true
        Layout.leftMargin: 4
        Layout.rightMargin: 4
        spacing: Theme.space.xs

        CoreText {
            Layout.fillWidth: true
            text: root.title
            color: Theme.color.neutral9
            textStyle: Theme.text.title
            horizontalAlignment: Text.AlignLeft
            wrap: false
            elide: Text.ElideRight
        }

        CoreText {
            visible: root.description.length > 0
            Layout.fillWidth: true
            text: root.description
            color: Theme.color.neutral7
            textStyle: Theme.text.description
            horizontalAlignment: Text.AlignLeft
            wrap: true
        }
    }

    ColumnLayout {
        id: contentColumn
        Layout.fillWidth: true
        Layout.topMargin: 8
        spacing: 24
    }
}
