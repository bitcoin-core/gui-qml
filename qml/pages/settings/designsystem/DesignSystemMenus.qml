// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../../controls"
import "../../../components"

DesignSystemSection {
    id: root

    property string menuGrouping: "month"
    property bool menuShowFees: true
    readonly property string longMessage: "This alert carries a long message to show how the popup grows with its text. "
        + "Alerts do not scroll, so very long messages push the actions down and can reach the edge of a small window. "
        + "Keep alert messages to a sentence or two, and move longer explanations onto a page or a feature modal. "
        + "This paragraph repeats the point so the message wraps over many lines at the default width."

    function openMenu(anchor) {
        const position = anchor.mapToItem(Overlay.overlay, 0, anchor.height + 2)
        exampleMenu.x = position.x
        exampleMenu.y = position.y
        exampleMenu.open()
    }

    title: "Menus and popups"
    description: "Buttons that open the shared menu and alert popups. Menu and alert actions do nothing."

    DesignSystemSpecimen {
        objectName: "designSystemContextMenu"
        Layout.fillWidth: true
        name: "ContextMenu"
        note: "With a title, a picker, a toggle, a divider, an icon item, a disabled item and a destructive item."

        DesignSystemState {
            label: "NeutralButton opener"
            NeutralButton {
                id: menuButton
                objectName: "designSystemContextMenuButton"
                text: "Open menu"
                onClicked: root.openMenu(menuButton)
            }
        }
        DesignSystemState {
            label: "OverflowMenuButton opener"
            OverflowMenuButton {
                id: overflowButton
                checkable: false
                checked: exampleMenu.opened
                onClicked: root.openMenu(overflowButton)
            }
        }

        ContextMenu {
            id: exampleMenu
            objectName: "designSystemExampleMenu"
            parent: Overlay.overlay
            title: "Group by"

            ContextMenuPicker {
                currentValue: root.menuGrouping
                model: [
                    { text: "Month", value: "month" },
                    { text: "Day", value: "day" }
                ]
                onActivated: function(value) {
                    root.menuGrouping = value
                }
            }

            ContextMenuDivider {}

            ContextMenuToggle {
                text: "Show fees"
                checked: root.menuShowFees
                onToggled: root.menuShowFees = checked
            }

            ContextMenuDivider {}

            ContextMenuButton {
                text: "Export"
                iconSource: "image://images/export"
            }

            ContextMenuButton {
                text: "Unavailable"
                enabled: false
            }

            ContextMenuButton {
                text: "Delete"
                role: ContextMenuButton.Destructive
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemAlertPopup"
        Layout.fillWidth: true
        name: "AlertPopup"

        DesignSystemState {
            label: "default action"
            NeutralButton {
                objectName: "designSystemNeutralAlertButton"
                text: "Neutral alert"
                onClicked: neutralAlert.open()
            }
        }
        DesignSystemState {
            label: "Cancel, Destructive"
            NeutralButton {
                objectName: "designSystemDestructiveAlertButton"
                text: "Destructive alert"
                onClicked: destructiveAlert.open()
            }
        }
        DesignSystemState {
            label: "long message, Cancel, Normal"
            NeutralButton {
                objectName: "designSystemLongAlertButton"
                text: "Long message"
                onClicked: longAlert.open()
            }
        }

        AlertPopup {
            id: neutralAlert
            objectName: "designSystemNeutralAlert"
            parent: Overlay.overlay
            title: "Transaction sent"
            message: "Your transaction was broadcast to the network."
        }

        AlertPopup {
            id: destructiveAlert
            objectName: "designSystemDestructiveAlert"
            parent: Overlay.overlay
            title: "Delete payment request?"
            message: "This removes the request from your activity."

            AlertAction {
                text: "Cancel"
                role: AlertAction.Cancel
            }
            AlertAction {
                text: "Delete"
                role: AlertAction.Destructive
            }
        }

        AlertPopup {
            id: longAlert
            objectName: "designSystemLongAlert"
            parent: Overlay.overlay
            title: "Confirm paste?"
            message: root.longMessage

            AlertAction {
                text: "Cancel"
                role: AlertAction.Cancel
            }
            AlertAction {
                text: "Paste"
            }
        }
    }
}
