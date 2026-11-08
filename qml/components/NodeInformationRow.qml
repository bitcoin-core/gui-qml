// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../controls"

ColumnLayout {
    id: informationRow
    required property int rowCount
    required property var modelData
    required property int index
    readonly property int valueLineCount: informationValue.lineCount
    readonly property int valueWrapMode: informationValue.wrapMode

    spacing: 0

    KeyValueRow {
        Layout.fillWidth: true
        keyWidth: 150
        key: CoreText {
            text: informationRow.modelData.label
            color: Theme.color.neutral7
            font.pixelSize: 13
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignTop
            wrap: false
        }
        value: CoreText {
            id: informationValue
            objectName: "nodeInformationValue_" + informationRow.index
            text: informationRow.modelData.value
            color: Theme.color.neutral9
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignTop
        }
    }

    Separator {
        Layout.fillWidth: true
        Layout.topMargin: 10
        Layout.bottomMargin: 10
        visible: informationRow.index < informationRow.rowCount - 1
    }
}
