// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "ContextMenuPicker"
    when: windowShown
    width: 400
    height: 400

    Item {
        id: host
        width: parent.width
        height: parent.height
    }

    Component {
        id: objectsPickerComponent

        ColumnLayout {
            id: hostLayout
            width: 280
            height: implicitHeight
            property alias picker: _picker
            property string selectedValue: "date"
            property var activatedValue: null
            property int activatedCount: 0

            ContextMenuPicker {
                id: _picker
                title: "Sorting"
                currentValue: hostLayout.selectedValue
                selectionIconSource: Qt.resolvedUrl("../../qml/res/icons/check.png")
                model: [
                    { text: "Date", value: "date" },
                    { text: "Amount", value: "amount" },
                    { text: "Address", value: "address" }
                ]
                onActivated: function(value) {
                    hostLayout.activatedValue = value
                    hostLayout.activatedCount += 1
                }
            }
        }
    }

    Component {
        id: primitivesPickerComponent

        ColumnLayout {
            width: 280
            height: implicitHeight
            property alias picker: _picker
            property var activatedValue: null

            ContextMenuPicker {
                id: _picker
                selectionIconSource: Qt.resolvedUrl("../../qml/res/icons/check.png")
                model: ["All", "Sent", "Received"]
                onActivated: function(value) {
                    parent.activatedValue = value
                }
            }
        }
    }

    Component {
        id: noTitlePickerComponent

        ColumnLayout {
            width: 280
            height: implicitHeight
            property alias picker: _picker

            ContextMenuPicker {
                id: _picker
                selectionIconSource: Qt.resolvedUrl("../../qml/res/icons/check.png")
                model: [{ text: "A", value: 1 }, { text: "B", value: 2 }]
            }
        }
    }

    Component {
        id: subtitlePickerComponent

        ColumnLayout {
            width: 280
            height: implicitHeight
            property alias picker: _picker

            ContextMenuPicker {
                id: _picker
                selectionIconSource: Qt.resolvedUrl("../../qml/res/icons/check.png")
                subtitleRole: "subtitle"
                model: [
                    { text: "With subtitle", value: 1, subtitle: "Details" },
                    { text: "Without subtitle", value: 2, subtitle: "" }
                ]
            }
        }
    }

    function waitForRows(picker, count) {
        tryVerify(function() {
            return picker.itemAtIndex(count - 1) !== null
        })
    }

    function test_renders_object_model_rows() {
        const host_layout = createTemporaryObject(objectsPickerComponent, host)
        verify(host_layout !== null)
        const picker = host_layout.picker
        waitForRows(picker, 3)

        compare(picker.itemAtIndex(0).rowText, "Date")
        compare(picker.itemAtIndex(0).rowValue, "date")
    }

    function test_section_title_uses_bold_caption_typography() {
        const host_layout = createTemporaryObject(objectsPickerComponent, host)
        verify(host_layout !== null)
        const picker = host_layout.picker
        compare(picker.titleItem.font.pixelSize, Theme.text.captionStrong.pixelSize)
        compare(picker.titleItem.font.styleName, "Semi Bold")
        compare(picker.titleItem.lineHeight, Theme.text.captionStrong.lineHeight)
    }

    function test_activating_row_emits_without_replacing_currentValue_binding() {
        const host_layout = createTemporaryObject(objectsPickerComponent, host)
        verify(host_layout !== null)
        const picker = host_layout.picker
        waitForRows(picker, 3)

        picker.itemAtIndex(1).forceActiveFocus()
        verify(picker.itemAtIndex(1).activeFocus)
        keyClick(Qt.Key_Space)
        compare(picker.currentValue, "date")
        compare(host_layout.activatedValue, "amount")
        compare(host_layout.activatedCount, 1)

        host_layout.selectedValue = "address"
        compare(picker.currentValue, "address")
        compare(picker.itemAtIndex(2).selected, true)
    }

    function test_programmatic_change_updates_selection_without_activated() {
        const host_layout = createTemporaryObject(objectsPickerComponent, host)
        verify(host_layout !== null)
        const picker = host_layout.picker
        waitForRows(picker, 3)

        host_layout.selectedValue = "address"
        compare(picker.itemAtIndex(2).selected, true)
        compare(picker.itemAtIndex(0).selected, false)
        compare(host_layout.activatedCount, 0)
    }

    function test_unmatched_currentvalue_renders_zero_selected() {
        const host_layout = createTemporaryObject(objectsPickerComponent, host)
        verify(host_layout !== null)
        const picker = host_layout.picker
        waitForRows(picker, 3)

        host_layout.selectedValue = "doesnotexist"
        for (let i = 0; i < 3; ++i) {
            compare(picker.itemAtIndex(i).selected, false)
        }
    }

    function test_primitives_model_works() {
        const host_layout = createTemporaryObject(primitivesPickerComponent, host)
        verify(host_layout !== null)
        const picker = host_layout.picker
        waitForRows(picker, 3)

        compare(picker.itemAtIndex(0).rowText, "All")
        compare(picker.itemAtIndex(0).rowValue, "All")
        picker.itemAtIndex(2).forceActiveFocus()
        verify(picker.itemAtIndex(2).activeFocus)
        keyClick(Qt.Key_Space)
        compare(host_layout.activatedValue, "Received")
    }

    function test_empty_title_omits_header() {
        const host_layout = createTemporaryObject(noTitlePickerComponent, host)
        verify(host_layout !== null)
        const picker = host_layout.picker
        waitForRows(picker, 2)

        compare(picker.implicitHeight, 2 * picker.rowHeight)
    }

    function test_row_height_depends_on_actual_subtitle_content() {
        const host_layout = createTemporaryObject(subtitlePickerComponent, host)
        verify(host_layout !== null)
        const picker = host_layout.picker
        waitForRows(picker, 2)

        compare(picker.itemAtIndex(0).implicitHeight, picker.subtitleRowHeight)
        compare(picker.itemAtIndex(1).implicitHeight, picker.rowHeight)
    }
}
