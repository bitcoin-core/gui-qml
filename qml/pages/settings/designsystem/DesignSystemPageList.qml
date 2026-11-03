// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../../../controls"

FormSection {
    id: pageList
    property var pages: []
    signal opened(var page)

    Layout.fillWidth: true

    // A positioner keeps the Repeater out of the Qt 6.4 layout engine.
    Column {
        Layout.fillWidth: true
        // Qt 6.4 needs a width before the positioner can measure its rows.
        width: pageList.width

        Repeater {
            model: pageList.pages

            delegate: ListRow {
                required property var modelData
                required property int index

                objectName: "designSystemOpen" + modelData.id
                width: parent.width
                title: modelData.name
                description: modelData.description
                showDivider: index < pageList.pages.length - 1
                showsDisclosureIndicator: true
                onClicked: pageList.opened(modelData)
            }
        }
    }
}
