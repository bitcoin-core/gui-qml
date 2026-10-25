// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../../controls"

// One group of related tokens in a foundations section: a short title and
// description above the samples, so it reads the same at any width.
ColumnLayout {
    id: root

    default property alias content: body.data
    property string title: ""
    property string description: ""
    // Shared types shown here, for the showcase completeness test.
    property var shows: []

    spacing: Theme.space.xl

    ColumnLayout {
        Layout.fillWidth: true
        Layout.maximumWidth: 560
        spacing: Theme.space.xs

        CoreText {
            Layout.fillWidth: true
            text: root.title
            color: Theme.color.neutral9
            textStyle: Theme.text.subheading
            horizontalAlignment: Text.AlignLeft
        }

        CoreText {
            visible: root.description.length > 0
            Layout.fillWidth: true
            text: root.description
            color: Theme.color.neutral6
            textStyle: Theme.text.caption
            horizontalAlignment: Text.AlignLeft
        }
    }

    ColumnLayout {
        id: body
        Layout.fillWidth: true
        spacing: Theme.space.l
    }
}
