// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "PopupPicker"
    when: windowShown
    width: 500
    height: 300

    Item {
        id: host
        width: parent.width
        height: parent.height
    }

    Component {
        id: adaptivePickerComponent

        Item {
            property alias picker: picker
            property string selectedValue: "embedded"

            PopupPicker {
                id: picker
                objectName: "adaptivePopupPicker"
                currentValue: parent.selectedValue
                selectionIconSource: ""
                model: [
                    { text: "Roboto Mono", value: "embedded" },
                    { text: "System Monospace", value: "best_system" }
                ]
            }
        }
    }

    function test_closed_chip_tracks_current_label_width() {
        const pickerHost = createTemporaryObject(adaptivePickerComponent, host)
        verify(pickerHost !== null)

        const picker = pickerHost.picker
        tryCompare(picker, "currentText", "Roboto Mono")
        const shortWidth = picker.implicitWidth
        verify(shortWidth > 0)
        verify(shortWidth < picker.minimumMenuWidth)

        pickerHost.selectedValue = "best_system"
        tryCompare(picker, "currentText", "System Monospace")
        tryVerify(function() { return picker.implicitWidth > shortWidth })

        pickerHost.selectedValue = "embedded"
        tryCompare(picker, "currentText", "Roboto Mono")
        tryCompare(picker, "implicitWidth", shortWidth)
    }

    function test_menu_width_is_independent_from_closed_chip_width() {
        const pickerHost = createTemporaryObject(adaptivePickerComponent, host)
        verify(pickerHost !== null)

        const picker = pickerHost.picker
        const menu = findChild(pickerHost, "adaptivePopupPickerMenu")
        verify(menu !== null)
        verify(picker.implicitWidth < picker.minimumMenuWidth)

        picker.open()
        tryCompare(menu, "opened", true)
        verify(menu.width >= picker.minimumMenuWidth)
        picker.close()
    }
}
