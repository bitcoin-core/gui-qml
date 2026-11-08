// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "../../components"

Rectangle {
    id: table
    required property var rows
    property bool compact: false
    implicitHeight: tableColumn.implicitHeight
    color: Theme.color.neutral1
    radius: 12
    border.width: 0
    clip: true
    // A positioner keeps Repeater delegates out of the Qt 6.4 layout engine.
    Column {
        id: tableColumn
        width: parent.width
        spacing: 0
        Repeater {
            // Updating values must not recreate selectable text fields.
            model: table.rows.length
            delegate: Component {
                ColumnLayout {
                    id: rowDelegate
                    required property int index
                    readonly property var rowData: table.rows[index]
                    width: tableColumn.width
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: table.compact ? 14 : 20
                        Layout.rightMargin: table.compact ? 14 : 20
                        Layout.minimumHeight: table.compact ? 50 : 48
                        spacing: 16
                        SelectablePeerText {
                            objectName: "peerDetailKey_" + rowDelegate.index
                            Layout.preferredWidth: table.compact ? 112 : 150
                            text: rowDelegate.rowData.label
                            font: Theme.text.description.font
                            Layout.preferredHeight: Theme.text.description.lineHeight
                            color: Theme.color.neutral6
                            horizontalAlignment: Text.AlignLeft
                        }
                        SelectablePeerText {
                            objectName: "peerDetailValue_" + rowDelegate.index
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: rowDelegate.rowData.value
                            font: rowDelegate.rowData.mono ? Theme.text.monoDescription.font : Theme.text.description.font
                            Layout.preferredHeight: rowDelegate.rowData.mono ? Theme.text.monoDescription.lineHeight : Theme.text.description.lineHeight
                            color: Theme.color.neutral8
                            horizontalAlignment: Text.AlignLeft
                        }
                    }
                    Separator {
                        Layout.fillWidth: true
                        Layout.leftMargin: table.compact ? 14 : 20
                        Layout.rightMargin: table.compact ? 14 : 20
                        visible: rowDelegate.index < table.rows.length - 1
                        color: Theme.color.neutral2
                    }
                }
            }
        }
    }
    component SelectablePeerText: TextInput {
        readOnly: true
        selectByMouse: true
        activeFocusOnTab: true
        clip: true
        verticalAlignment: TextInput.AlignVCenter
        selectionColor: Theme.color.orange
        selectedTextColor: Theme.color.white

        HoverHandler { cursorShape: Qt.IBeamCursor }
    }
}
