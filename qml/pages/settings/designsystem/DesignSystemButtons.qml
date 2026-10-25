// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import org.bitcoincore.qt 1.0

import "../../../controls"

DesignSystemSection {
    id: root

    property bool filterActive: false
    property int amountUnit: BitcoinAmount.BTC

    description: "Each button in the states its properties support. Hover, press and Tab to see the interactive states."

    DesignSystemSpecimen {
        objectName: "designSystemContinueButton"
        Layout.fillWidth: true
        name: "ContinueButton"

        DesignSystemState {
            label: "rest"
            ContinueButton { Layout.preferredWidth: 180; text: "Continue" }
        }
        DesignSystemState {
            label: "iconSource"
            ContinueButton { Layout.preferredWidth: 180; text: "Receive"; iconSource: "image://images/plus" }
        }
        DesignSystemState {
            label: "busy: true"
            ContinueButton { Layout.preferredWidth: 180; text: "Sending"; busy: true }
        }
        DesignSystemState {
            label: "enabled: false"
            ContinueButton { Layout.preferredWidth: 180; text: "Continue"; enabled: false }
        }
        DesignSystemState {
            label: "destructive: true"
            ContinueButton {
                Layout.preferredWidth: 180
                text: "Delete"
                destructive: true
            }
        }
        DesignSystemState {
            label: "destructive, enabled: false"
            ContinueButton {
                Layout.preferredWidth: 180
                text: "Delete"
                enabled: false
                destructive: true
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemNeutralButton"
        Layout.fillWidth: true
        name: "NeutralButton"

        DesignSystemState {
            label: "Medium"
            NeutralButton { text: "Export" }
        }
        DesignSystemState {
            label: "Medium, iconSource"
            NeutralButton { text: "Copy"; iconSource: "image://images/copy" }
        }
        DesignSystemState {
            label: "Medium, enabled: false"
            NeutralButton { text: "Export"; enabled: false }
        }
        DesignSystemState {
            label: "surfaceLevel: 0"
            NeutralButton { text: "Export"; surfaceLevel: 0 }
        }
        DesignSystemState {
            label: "Large"
            NeutralButton { text: "Cancel"; buttonSize: NeutralButton.Large }
        }
        DesignSystemState {
            label: "Large, enabled: false"
            NeutralButton { text: "Cancel"; buttonSize: NeutralButton.Large; enabled: false }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemLinkButton"
        Layout.fillWidth: true
        name: "LinkButton"
        note: "The one text action. Darkens on hover and press (accentTextHover, accentTextPressed)."

        DesignSystemState {
            label: "rest"
            LinkButton { text: "Learn more" }
        }
        DesignSystemState {
            label: "iconSource"
            LinkButton { text: "Open in browser"; iconSource: "image://images/export" }
        }
        DesignSystemState {
            label: "textStyle: menuItemStrong"
            LinkButton { text: "Apply"; textStyle: Theme.text.menuItemStrong }
        }
        DesignSystemState {
            label: "textStyle: buttonStrong"
            LinkButton { text: "Try again"; textStyle: Theme.text.buttonStrong }
        }
        DesignSystemState {
            label: "enabled: false"
            LinkButton { text: "Learn more"; enabled: false }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemIconButton"
        shows: ["Icon"]
        Layout.fillWidth: true
        name: "IconButton"
        note: "iconSize is an icon token (24 bare, 20 toolbar), independent of the target size."

        DesignSystemState {
            label: "rest"
            IconButton { iconSource: "image://images/gear"; accessibleName: "Settings" }
        }
        DesignSystemState {
            label: "checked: true"
            IconButton { iconSource: "image://images/gear"; checkable: true; checked: true; accessibleName: "Settings" }
        }
        DesignSystemState {
            label: "enabled: false"
            IconButton { iconSource: "image://images/gear"; enabled: false; accessibleName: "Settings" }
        }
        DesignSystemState {
            label: "toolbar: true"
            IconButton { objectName: "designSystemToolbarIconButton"; toolbar: true; iconSource: "image://images/flip-vertical"; accessibleName: "Sort" }
        }
        DesignSystemState {
            label: "toolbar, surfaceLevel: 1"
            IconButton { toolbar: true; surfaceLevel: 1; iconSource: "qrc:/icons/group-by.svg"; accessibleName: "Group" }
        }
        DesignSystemState {
            label: "toolbar, checked: true"
            IconButton { toolbar: true; checkable: true; checked: true; iconSource: "qrc:/icons/checklist.svg"; accessibleName: "Select" }
        }
        DesignSystemState {
            label: "toolbar, enabled: false"
            IconButton { toolbar: true; enabled: false; iconSource: "image://images/flip-vertical"; accessibleName: "Sort" }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemCloseButton"
        Layout.fillWidth: true
        name: "CloseButton"
        note: "Medium (28 target, 12 artwork) closes every modal and banner. Large (40/14) is only for a full-window flow's navigation bar."

        DesignSystemState {
            label: "Medium"
            CloseButton {}
        }
        DesignSystemState {
            label: "Large"
            CloseButton { buttonSize: CloseButton.Large }
        }
        DesignSystemState {
            label: "enabled: false"
            CloseButton { enabled: false }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemCopyButton"
        Layout.fillWidth: true
        name: "CopyButton"
        note: "Click to see the copied state. These samples do not write to the clipboard."

        DesignSystemState {
            label: "IconAndText"
            CopyButton {}
        }
        DesignSystemState {
            label: "IconOnly"
            CopyButton { displayMode: CopyButton.IconOnly }
        }
        DesignSystemState {
            label: "TextOnly"
            CopyButton { displayMode: CopyButton.TextOnly }
        }
        DesignSystemState {
            label: "enabled: false"
            CopyButton { enabled: false }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemOverflowMenuButton"
        Layout.fillWidth: true
        name: "OverflowMenuButton"

        DesignSystemState {
            label: "rest"
            OverflowMenuButton {}
        }
        DesignSystemState {
            label: "surfaceLevel: 1"
            OverflowMenuButton { surfaceLevel: 1 }
        }
        DesignSystemState {
            label: "circular: true"
            OverflowMenuButton { circular: true }
        }
        DesignSystemState {
            label: "checked: true"
            OverflowMenuButton { checkable: true; checked: true }
        }
        DesignSystemState {
            label: "enabled: false"
            OverflowMenuButton { enabled: false }
        }
        DesignSystemState {
            label: "size: controlSmall"
            OverflowMenuButton { size: Theme.size.controlSmall }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemFilterButton"
        Layout.fillWidth: true
        name: "FilterButton"

        DesignSystemState {
            label: "active: " + root.filterActive
            FilterButton {
                active: root.filterActive
                onClicked: root.filterActive = !root.filterActive
            }
        }
        DesignSystemState {
            label: "active: true"
            FilterButton { active: true }
        }
        DesignSystemState {
            label: "count: 3"
            FilterButton { active: true; count: 3 }
        }
        DesignSystemState {
            label: "enabled: false"
            FilterButton { enabled: false }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemDropdownButton"
        Layout.fillWidth: true
        name: "DropdownButton"

        DesignSystemState {
            label: "rest"
            DropdownButton { text: "All types" }
        }
        DesignSystemState {
            label: "subtitle"
            DropdownButton { text: "Fee"; subtitle: "~10 min" }
        }
        DesignSystemState {
            label: "opened: true"
            DropdownButton { text: "All types"; opened: true }
        }
        DesignSystemState {
            label: "active: true"
            DropdownButton { text: "Sent"; active: true }
        }
        DesignSystemState {
            label: "surfaceLevel: 1"
            DropdownButton { text: "All types"; surfaceLevel: 1 }
        }
        DesignSystemState {
            label: "enabled: false"
            DropdownButton { text: "All types"; enabled: false }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemAmountUnitButton"
        Layout.fillWidth: true
        name: "AmountUnitButton"
        note: "The first sample flips a local unit; it does not change the display unit setting."

        DesignSystemState {
            label: "unit: " + (root.amountUnit === BitcoinAmount.SAT ? "SAT" : "BTC")
            AmountUnitButton {
                unit: root.amountUnit
                onClicked: root.amountUnit = root.amountUnit === BitcoinAmount.SAT ? BitcoinAmount.BTC : BitcoinAmount.SAT
            }
        }
        DesignSystemState {
            label: "unit: SAT"
            AmountUnitButton { unit: BitcoinAmount.SAT }
        }
        DesignSystemState {
            label: "enabled: false"
            AmountUnitButton { enabled: false }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemNavButton"
        Layout.fillWidth: true
        name: "NavButton"

        DesignSystemState {
            label: "iconSource, text"
            NavButton { text: "Back"; iconSource: "image://images/caret-left" }
        }
        DesignSystemState {
            label: "text"
            NavButton { text: "Done" }
        }
        DesignSystemState {
            label: "iconSource"
            NavButton { iconSource: "image://images/gear"; Accessible.name: "Settings" }
        }
        DesignSystemState {
            label: "busy: true"
            NavButton { text: "Done"; busy: true }
        }
        DesignSystemState {
            label: "enabled: false"
            NavButton { text: "Done"; enabled: false }
        }
    }
}
