// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import "../../controls"

// A titled or untitled card of peer detail rows. A named type keeps the
// delegate outside inline components (Qt 6.2 compiler crash) without a Loader.
FormSection {
    id: table
    required property var rows
    property bool compact: false

    // A positioner keeps Repeater delegates out of the Qt 6.4 layout engine.
    Column {
        id: tableColumn
        Layout.fillWidth: true
        spacing: 0
        Repeater {
            // Updating values must not recreate selectable text fields.
            model: table.rows.length
            delegate: Component {
                FormRow {
                    id: rowDelegate
                    required property int index
                    readonly property var rowData: table.rows[index]
                    width: tableColumn.width
                    implicitWidth: 0
                    showDivider: rowDelegate.index < table.rows.length - 1
                    contentItem: RowLayout {
                        spacing: Theme.space.l
                        SelectableText {
                            objectName: "peerDetailKey_" + rowDelegate.index
                            Layout.preferredWidth: table.compact ? 112 : 150
                            Layout.alignment: Qt.AlignVCenter
                            activeFocusOnTab: true
                            text: rowDelegate.rowData.label
                            textStyle: Theme.text.description
                            color: Theme.color.neutral7
                        }
                        SelectableText {
                            objectName: "peerDetailValue_" + rowDelegate.index
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            Layout.alignment: Qt.AlignVCenter
                            activeFocusOnTab: true
                            text: rowDelegate.rowData.value
                            textStyle: rowDelegate.rowData.mono ? Theme.text.monoDescription : Theme.text.description
                            wrapMode: TextEdit.WrapAnywhere
                        }
                    }
                }
            }
        }
    }
}
