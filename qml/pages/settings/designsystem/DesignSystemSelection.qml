// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

import "../../../controls"

DesignSystemSection {
    id: root

    property int segmentIndex: 0
    property string pickerValue: "day"
    property string menuPickerValue: "date"
    property var menuPickerValues: ["sent", "received"]
    property string choiceTile: "reduce"

    readonly property var pickerOptions: [
        { text: "Day", value: "day" },
        { text: "Week", value: "week" },
        { text: "Month", value: "month" }
    ]

    function toggledValues(values, value) {
        return values.indexOf(value) >= 0 ? values.filter(v => v !== value) : values.concat([value])
    }

    title: "Selection"
    description: "Controls that pick a value. Every sample keeps its value locally."

    DesignSystemSpecimen {
        objectName: "designSystemCheckBox"
        Layout.fillWidth: true
        name: "CheckBox"

        DesignSystemState {
            label: "checked: false"
            CheckBox { text: "I have backed up my wallet" }
        }
        DesignSystemState {
            label: "checked: true"
            CheckBox { text: "I have backed up my wallet"; checked: true }
        }
        DesignSystemState {
            label: "enabled: false"
            CheckBox { text: "Unavailable"; enabled: false }
        }
        DesignSystemState {
            label: "checked, enabled: false"
            CheckBox { text: "Unavailable"; checked: true; enabled: false }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemOptionSwitch"
        Layout.fillWidth: true
        name: "OptionSwitch"

        DesignSystemState {
            label: "checked: false"
            OptionSwitch { Accessible.name: "Example switch" }
        }
        DesignSystemState {
            label: "checked: true"
            OptionSwitch { checked: true; Accessible.name: "Example switch" }
        }
        DesignSystemState {
            label: "enabled: false"
            OptionSwitch { enabled: false; Accessible.name: "Example switch" }
        }
        DesignSystemState {
            label: "checked, enabled: false"
            OptionSwitch { checked: true; enabled: false; Accessible.name: "Example switch" }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemSegmentedPicker"
        Layout.fillWidth: true
        name: "SegmentedPicker"
        note: "Each segment is a ToggleButton, which has no other user."

        DesignSystemState {
            label: "implicitHeight: 45"
            SegmentedPicker {
                Accessible.name: "Example picker"
                implicitWidth: 300
                model: root.pickerOptions
                currentIndex: root.segmentIndex
                onSelected: function(index, option) {
                    root.segmentIndex = index
                }
            }
        }
        DesignSystemState {
            label: "implicitHeight: 36"
            SegmentedPicker {
                Accessible.name: "Example picker"
                implicitWidth: 220
                implicitHeight: 36
                model: ["Light", "Dark"]
                currentIndex: 1
            }
        }
        DesignSystemState {
            label: "enabled: false"
            SegmentedPicker {
                Accessible.name: "Example picker"
                implicitWidth: 220
                implicitHeight: 36
                enabled: false
                model: ["Light", "Dark"]
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemPopupPicker"
        Layout.fillWidth: true
        name: "PopupPicker"

        DesignSystemState {
            label: "rest"
            PopupPicker {
                model: root.pickerOptions
                currentValue: root.pickerValue
                menuTitle: "Group by"
                onActivated: function(value) {
                    root.pickerValue = value
                }
            }
        }
        DesignSystemState {
            label: "surfaceLevel: 1"
            PopupPicker {
                surfaceLevel: 1
                model: root.pickerOptions
                currentValue: root.pickerValue
                onActivated: function(value) {
                    root.pickerValue = value
                }
            }
        }
        DesignSystemState {
            label: "placeholderText"
            PopupPicker {
                model: root.pickerOptions
                placeholderText: "Choose a period"
            }
        }
        DesignSystemState {
            label: "enabled: false"
            PopupPicker {
                enabled: false
                model: root.pickerOptions
                currentValue: "week"
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemContextMenuPicker"
        Layout.fillWidth: true
        name: "ContextMenuPicker"
        note: "Shown inline here; menus host it as a section."

        DesignSystemState {
            width: 240
            label: "single selection"
            ContextMenuPicker {
                title: "Sort by"
                currentValue: root.menuPickerValue
                model: [
                    { text: "Date", value: "date" },
                    { text: "Amount", value: "amount" }
                ]
                onActivated: function(value) {
                    root.menuPickerValue = value
                }
            }
        }
        DesignSystemState {
            width: 240
            label: "multiSelect, subtitleRole"
            ContextMenuPicker {
                title: "Show"
                multiSelect: true
                selectedValues: root.menuPickerValues
                subtitleRole: "subtitle"
                model: [
                    { text: "Sent", value: "sent", subtitle: "Outgoing payments" },
                    { text: "Received", value: "received", subtitle: "Incoming payments" }
                ]
                onActivated: function(value) {
                    root.menuPickerValues = root.toggledValues(root.menuPickerValues, value)
                }
            }
        }
        DesignSystemState {
            width: 240
            label: "iconRole"
            ContextMenuPicker {
                currentValue: "receive"
                iconRole: "icon"
                model: [
                    { text: "Send", value: "send", icon: "image://images/activity-send.svg" },
                    { text: "Receive", value: "receive", icon: "image://images/activity-receive.svg" }
                ]
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemSlider"
        Layout.fillWidth: true
        name: "Slider"

        DesignSystemState {
            label: "rest"
            Slider { value: 40; Accessible.name: "Example slider" }
        }
        DesignSystemState {
            label: "discrete: true"
            Slider { from: 1; to: 8; value: 3; discrete: true; Accessible.name: "Example slider" }
        }
        DesignSystemState {
            label: "enabled: false"
            Slider { value: 40; enabled: false; Accessible.name: "Example slider" }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemRangeSlider"
        Layout.fillWidth: true
        name: "RangeSlider"

        DesignSystemState {
            label: "rest"
            RangeSlider { lowerValue: 20; upperValue: 70; Accessible.name: "Example range" }
        }
        DesignSystemState {
            label: "enabled: false"
            RangeSlider { lowerValue: 20; upperValue: 70; enabled: false; Accessible.name: "Example range" }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemOptionButton"
        Layout.fillWidth: true
        name: "OptionButton"
        note: "Choice tiles. The first pair is exclusive, as in the storage options."

        ButtonGroup {
            id: tileGroup
        }

        DesignSystemState {
            width: 380
            label: "surfaceSelectionStyle, checked"
            OptionButton {
                Layout.fillWidth: true
                ButtonGroup.group: tileGroup
                surfaceSelectionStyle: true
                text: "Reduce storage"
                description: "Uses about 2GB. For regular wallet use."
                recommended: true
                checked: root.choiceTile === "reduce"
                onClicked: root.choiceTile = "reduce"
            }
        }
        DesignSystemState {
            width: 380
            label: "surfaceSelectionStyle"
            OptionButton {
                Layout.fillWidth: true
                ButtonGroup.group: tileGroup
                surfaceSelectionStyle: true
                text: "Store all data"
                description: "Uses about 600GB. Support the network."
                checked: root.choiceTile === "full"
                onClicked: root.choiceTile = "full"
            }
        }
        DesignSystemState {
            width: 380
            label: "outlined"
            OptionButton {
                Layout.fillWidth: true
                text: "Default location"
                description: "Stored in the standard data directory."
                customDir: "/Users/example/Bitcoin"
            }
        }
        DesignSystemState {
            width: 380
            label: "errorText, enabled: false"
            OptionButton {
                Layout.fillWidth: true
                surfaceSelectionStyle: true
                enabled: false
                text: "Store all data"
                description: "Uses about 600GB. Support the network."
                errorText: "Not enough free space."
            }
        }
    }

    DesignSystemSpecimen {
        objectName: "designSystemWalletCreationTypeRow"
        Layout.fillWidth: true
        name: "WalletCreationTypeRow"

        DesignSystemState {
            width: 380
            label: "rest"
            WalletCreationTypeRow {
                Layout.fillWidth: true
                title: "Regular"
                description: "Fully managed in this application."
                iconSource: "image://images/key-filled"
            }
        }
        DesignSystemState {
            width: 380
            label: "enabled: false"
            WalletCreationTypeRow {
                Layout.fillWidth: true
                enabled: false
                title: "View only"
                description: "Watch an extended public key."
                iconSource: "image://images/visible"
            }
        }
    }
}
