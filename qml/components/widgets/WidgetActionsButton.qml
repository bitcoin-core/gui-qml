// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../../controls"

OverflowMenuButton {
    id: root
    required property var dashboard
    objectName: "widgetActionsButton"
    checked: actionsMenu.opened
    Accessible.name: qsTr("Widget options")
    onClicked: actionsMenu.opened ? actionsMenu.close() : actionsMenu.open()
    onVisibleChanged: if (!visible) actionsMenu.close()

    Connections {
        target: root.dashboard
        function onEditingChanged() { if (!root.dashboard.editing) actionsMenu.close() }
    }

    ContextMenu {
        id: actionsMenu
        objectName: "widgetActionsMenu"
        modal: true
        dim: false
        margins: 8
        x: root.width - width
        y: root.height + 6

        ContextMenuButton {
            objectName: "addWidgetButton"
            text: qsTr("Add Widget…")
            onTriggered: root.dashboard.openPicker()
        }
        ContextMenuDivider {}
        ContextMenuButton {
            objectName: "editWidgetsButton"
            text: root.dashboard.editing ? qsTr("Save") : qsTr("Edit Widgets")
            onTriggered: {
                if (root.dashboard.editing) root.dashboard.finishEditing()
                else root.dashboard.editing = true
            }
        }
    }
}
