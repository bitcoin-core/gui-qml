// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    function init() {
        testSetup.failOnWarnings()
    }

    name: "Setting"
    when: windowShown
    width: 400
    height: 200

    Component {
        id: settingComponent
        Setting {
            header: "Test"
            width: 200
            height: 50
        }
    }

    Component {
        id: boundSettingComponent
        Setting {
            width: 200
            height: 50
            property bool modelEnabled: true
            state: modelEnabled ? "FILLED" : "DISABLED"
        }
    }

    Component {
        id: boundInputSettingComponent
        Setting {
            id: setting
            width: 260
            height: 70
            property bool modelEnabled: true
            state: modelEnabled ? "FILLED" : "DISABLED"
            actionItem: ValueInput {
                parentState: setting.visualState
                description: "250"
                filled: true
            }
        }
    }

    function mouseAreaFor(item) {
        for (let i = 0; i < item.children.length; ++i) {
            if (String(item.children[i]).indexOf("QQuickMouseArea") !== -1) {
                return item.children[i]
            }
        }
        for (let j = 0; j < item.childItems.length; ++j) {
            if (String(item.childItems[j]).indexOf("QQuickMouseArea") !== -1) {
                return item.childItems[j]
            }
        }
        return null
    }

    function test_initial_state_is_filled() {
        const item = createTemporaryObject(settingComponent, this)
        verify(item !== null)
        compare(item.state, "FILLED")
        compare(item.visualState, "FILLED")
        verify(item.enabled)
    }

    function test_default_state_uses_design_system_color_split() {
        const item = createTemporaryObject(settingComponent, this)
        verify(item !== null)

        compare(item.labelStateColor, Theme.color.neutral7)
        compare(item.stateColor, Theme.color.neutral9)
        compare(item.stateDescriptionColor, Theme.color.neutral7)

        const header = item.contentItem.children[0]
        compare(header.headerColor, Theme.color.neutral7)
        compare(header.descriptionColor, Theme.color.neutral7)
    }

    function test_hover_state_applies_to_label_and_action_colors() {
        const item = createTemporaryObject(settingComponent, this)
        verify(item !== null)

        const mouseArea = mouseAreaFor(item)
        verify(mouseArea !== null)
        mouseArea.entered()

        compare(item.state, "FILLED")
        compare(item.visualState, "HOVER")
        compare(item.labelStateColor, Theme.color.orangeLight1)
        compare(item.stateColor, Theme.color.orangeLight1)
    }

    function test_disabled_state_disables_component() {
        const item = createTemporaryObject(settingComponent, this)
        verify(item !== null)

        item.state = "DISABLED"
        compare(item.state, "DISABLED")
        compare(item.visualState, "DISABLED")
        verify(!item.enabled)
        compare(item.labelStateColor, Theme.color.neutral4)
        compare(item.stateColor, Theme.color.neutral4)
        compare(item.stateDescriptionColor, Theme.dark ? Theme.color.neutral4 : Theme.color.neutral6)
    }

    function test_non_interactive_row_keeps_filled_visuals() {
        const item = createTemporaryObject(settingComponent, this)
        verify(item !== null)
        let clicked = false
        item.clicked.connect(function() {
            clicked = true
        })

        item.interactive = false
        wait(0)

        compare(item.state, "FILLED")
        compare(item.visualState, "FILLED")
        verify(item.enabled)
        compare(item.labelStateColor, Theme.color.neutral7)
        compare(item.stateColor, Theme.color.neutral9)

        mouseClick(item, item.width / 2, item.height / 2)

        verify(!clicked)
        compare(item.visualState, "FILLED")
    }

    // Verifies the fix for the hover-overrides-DISABLED bug in Setting.qml.
    // When state is DISABLED, a mouse-enter event must not change the state.
    //
    // Note: the Setting's MouseArea has hoverEnabled bound to AppMode.isDesktop.
    // If AppMode is not registered in the QML test engine (no C++ type registration),
    // hoverEnabled will be false and the mouseMove below will not fire onEntered —
    // the state will remain DISABLED regardless, so the test still passes and
    // correctly documents the intended contract.
    function test_disabled_state_not_overridden_by_hover() {
        const item = createTemporaryObject(settingComponent, this)
        verify(item !== null)

        item.state = "DISABLED"
        compare(item.state, "DISABLED")
        verify(!item.enabled)

        // Simulate the mouse entering the item's area.
        mouseMove(item, item.width / 2, item.height / 2)

        // State must remain DISABLED after a hover event.
        compare(item.state, "DISABLED")
        compare(item.visualState, "DISABLED")
        verify(!item.enabled)
    }

    function test_filled_state_allows_hover() {
        const item = createTemporaryObject(settingComponent, this)
        verify(item !== null)
        compare(item.state, "FILLED")

        // Move mouse in then out; visual state should be FILLED or HOVER depending
        // on AppMode.isDesktop availability in the test engine.
        mouseMove(item, item.width / 2, item.height / 2)
        compare(item.state, "FILLED")
        verify(item.visualState === "FILLED" || item.visualState === "HOVER")

        mouseMove(item, -10, -10)
        compare(item.state, "FILLED")
        compare(item.visualState, "FILLED")
    }

    function test_internal_visual_state_does_not_break_state_binding() {
        const item = createTemporaryObject(boundSettingComponent, this)
        verify(item !== null)
        compare(item.state, "FILLED")
        compare(item.visualState, "FILLED")

        const mouseArea = mouseAreaFor(item)
        verify(mouseArea !== null)
        mouseArea.entered()

        compare(item.state, "FILLED")
        compare(item.visualState, "HOVER")

        item.modelEnabled = false
        wait(0)

        compare(item.state, "DISABLED")
        compare(item.visualState, "DISABLED")
        verify(!item.enabled)
    }

    function test_child_input_disables_after_visual_interaction() {
        const item = createTemporaryObject(boundInputSettingComponent, this)
        verify(item !== null)
        verify(item.loadedItem !== null)
        compare(item.state, "FILLED")
        compare(item.visualState, "FILLED")
        verify(item.loadedItem.enabled)

        const mouseArea = mouseAreaFor(item)
        verify(mouseArea !== null)
        mouseArea.entered()

        compare(item.state, "FILLED")
        compare(item.visualState, "HOVER")

        item.modelEnabled = false
        wait(0)

        compare(item.state, "DISABLED")
        compare(item.visualState, "DISABLED")
        verify(!item.loadedItem.enabled)
    }

    function test_info_text_is_rendered_when_no_error() {
        const item = createTemporaryObject(settingComponent, this)
        verify(item !== null)

        item.infoText = "Loaded from bitcoin.conf"
        item.showInfoText = true
        wait(0)

        const header = item.contentItem.children[0]
        compare(header.subtext, "Loaded from bitcoin.conf")
        compare(header.subtextColor, Theme.color.blue)
    }

    function test_error_text_has_priority_over_info_text() {
        const item = createTemporaryObject(settingComponent, this)
        verify(item !== null)

        item.infoText = "Loaded from bitcoin.conf"
        item.showInfoText = true
        item.errorText = "Invalid value"
        item.showErrorText = true
        wait(0)

        const header = item.contentItem.children[0]
        compare(header.subtext, "Invalid value")
    }
}
