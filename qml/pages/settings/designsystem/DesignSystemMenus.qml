// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../../controls"

DesignSystemSection {
    id: root

    property string menuGrouping: "month"
    property bool menuShowFees: true
    readonly property string longMessage: Array(9).join("This alert carries a long message to show how the popup handles it. "
        + "The popup stops 20 px from the window edges and the message scrolls, so the title and actions stay in view. "
        + "Keep alert messages to a sentence or two where you can; node errors are the exception. ")

    function openMenu(anchor) {
        exampleMenu.openBelow(anchor)
    }

    description: "Buttons that open the shared menu, alert and dialog popups. Menu, alert and dialog actions do nothing."

    DesignSystemSpecimen {
        objectName: "designSystemContextMenu"
        shows: ["ContextMenuButton", "ContextMenuDivider", "ContextMenuToggle"]
        Layout.fillWidth: true
        name: "ContextMenu"
        note: "With a title, a picker, a toggle, a divider, an icon item, a disabled item and a destructive item. Modal without a dim; openBelow(trigger) places it, Up/Down/Home/End move between items."

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
            label: "long message scrolls, Cancel, Normal"
            NeutralButton {
                objectName: "designSystemLongAlertButton"
                text: "Long message"
                onClicked: longAlert.open()
            }
        }
        DesignSystemState {
            label: "tone: Success, Neutral, Normal"
            NeutralButton {
                objectName: "designSystemSuccessAlertButton"
                text: "Success"
                onClicked: successAlert.open()
            }
        }
        DesignSystemState {
            label: "tone: Error with icon"
            NeutralButton {
                objectName: "designSystemErrorAlertButton"
                text: "Error"
                onClicked: errorAlert.open()
            }
        }
        DesignSystemState {
            label: "content field, errorText, disabled action"
            NeutralButton {
                objectName: "designSystemInputAlertButton"
                text: "With a field"
                onClicked: inputAlert.open()
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
            id: successAlert
            objectName: "designSystemSuccessAlert"
            parent: Overlay.overlay
            tone: AlertPopup.Success
            title: "Transaction updated"
            message: "Your replacement transaction was broadcast."

            AlertAction {
                text: "View transaction"
                role: AlertAction.Neutral
            }
            AlertAction {
                text: "Done"
            }
        }

        AlertPopup {
            id: errorAlert
            objectName: "designSystemErrorAlert"
            parent: Overlay.overlay
            tone: AlertPopup.Error
            iconSource: "image://images/error"
            title: "Error"
            message: "Unable to start HTTP server. See debug log for details."
            messageAlignment: Text.AlignLeft
        }

        AlertPopup {
            id: inputAlert
            objectName: "designSystemInputAlert"
            parent: Overlay.overlay
            title: "Open payment request"
            errorText: inputAlertField.text.length > 0 && inputAlertField.text.indexOf("bitcoin:") !== 0
                ? "Enter a bitcoin: URI." : ""
            initialFocusItem: inputAlertField.field
            implicitWidth: 420

            content: LabeledTextField {
                id: inputAlertField
                Layout.fillWidth: true
                label: "Payment request"
                placeholderText: "bitcoin:address?amount=…"
                fieldBackgroundColor: Theme.color.surface2
            }

            AlertAction {
                text: "Cancel"
                role: AlertAction.Cancel
            }
            AlertAction {
                text: "Apply"
                enabled: inputAlertField.text.indexOf("bitcoin:") === 0
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

    DesignSystemSpecimen {
        objectName: "designSystemDialogPopup"
        shows: ["DialogHeader"]
        Layout.fillWidth: true
        name: "DialogPopup"
        note: "The shell of every modal: centering, overlay, surface, transitions, height cap, focus in and out. Feature modals close on Escape and outside presses; sheets host multi-step flows."

        DesignSystemState {
            label: "feature modal, DialogHeader"
            NeutralButton {
                objectName: "designSystemFeatureModalButton"
                text: "Feature modal"
                onClicked: featureModal.open()
            }
        }
        DesignSystemState {
            label: "sheet: true"
            NeutralButton {
                objectName: "designSystemSheetButton"
                text: "Sheet"
                onClicked: sheetModal.open()
            }
        }

        DialogPopup {
            id: featureModal
            objectName: "designSystemFeatureModal"
            title: "Banned peers"
            implicitWidth: 560

            contentItem: ColumnLayout {
                spacing: Theme.space.xl

                DialogHeader {
                    Layout.fillWidth: true
                    title: featureModal.title
                    description: "A feature modal: a title row with the close button, then its content."
                    onCloseRequested: featureModal.close()
                }

                FormSection {
                    Layout.fillWidth: true
                    backgroundColor: Theme.color.surface2
                    ValueRow {
                        Layout.fillWidth: true
                        title: "Address"
                        value: "203.0.113.7:8333"
                    }
                    ValueRow {
                        Layout.fillWidth: true
                        title: "Banned until"
                        value: "Tomorrow"
                        showDivider: false
                    }
                }
            }
        }

        DialogPopup {
            id: sheetModal
            objectName: "designSystemSheet"
            title: "Add wallet"
            sheet: true
            closesOnPressOutside: false
            implicitWidth: 560
            implicitHeight: 420

            contentItem: ColumnLayout {
                spacing: Theme.space.xl

                DialogHeader {
                    Layout.fillWidth: true
                    title: sheetModal.title
                    description: "A sheet uses radius sheet and hosts a flow. It closes on Escape and the close button only."
                    onCloseRequested: sheetModal.close()
                }

                Item { Layout.fillHeight: true }

                PrimaryButton {
                    Layout.alignment: Qt.AlignRight
                    text: "Continue"
                }
            }
        }
    }
}
