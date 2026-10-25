// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../../controls"

ColumnLayout {
    id: root

    default property alias content: samples.data
    property string name: ""
    property string note: ""
    // Shared types shown here besides the one the specimen is named after.
    property var shows: []
    property alias sampleSpacing: samples.spacing

    spacing: 8

    CoreText {
        Layout.fillWidth: true
        text: root.name
        color: Theme.color.neutral9
        textStyle: Theme.text.subheading
        horizontalAlignment: Text.AlignLeft
        wrap: false
        elide: Text.ElideRight
    }

    CoreText {
        visible: root.note.length > 0
        Layout.fillWidth: true
        text: root.note
        color: Theme.color.neutral7
        textStyle: Theme.text.caption
        horizontalAlignment: Text.AlignLeft
        wrap: true
    }

    // A positioner keeps samples, and any Repeater among them, out of the Qt 6.4 layout engine.
    Flow {
        id: samples
        Layout.fillWidth: true
        spacing: 16
    }
}
