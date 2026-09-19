// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../controls"

GridLayout {
    id: fieldsGrid
    required property var fields
    required property int wideColumns
    property bool lastRowDivider: false
    Layout.fillWidth: true
    columns: width >= 900 ? wideColumns : width >= 520 ? 2 : 1
    columnSpacing: 16
    rowSpacing: 0
    Repeater {
        model: fieldsGrid.fields
        delegate: FormRow {
            id: fieldRow
            required property var modelData
            required property int index
            readonly property string value: modelData.value
            objectName: "transaction" + modelData.key + "Row"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitWidth: 0
            minimumRowHeight: 54
            title: modelData.title
            titleColor: Theme.color.neutral7
            titleTextStyle: Theme.text.caption
            bodyItem: CoreText {
                objectName: fieldRow.objectName + "Value"
                Layout.fillWidth: true
                text: fieldRow.value
                font: Theme.text.description.font
                horizontalAlignment: Text.AlignLeft
                textFormat: Text.PlainText
                wrap: true
            }
            showDivider: fieldsGrid.lastRowDivider || index < fieldsGrid.fields.length - fieldsGrid.columns
        }
    }
}
