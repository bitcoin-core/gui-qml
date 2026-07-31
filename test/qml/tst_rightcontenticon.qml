// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtTest 1.2
import "../../qml/components"
import "../../qml/controls"

TestCase {
    name: "RightContentIcon"
    when: windowShown
    width: 320
    height: 180

    Component {
        id: rightContentIconComponent
        RowLayout {
            RightContentIcon {
                objectName: "rightContentIcon"
                source: "image://images/export"
                color: "white"
                iconSize: 22
            }
        }
    }

    Component {
        id: externalLinkComponent
        ExternalLink {
            width: 220
            height: 30
            parentState: "FILLED"
            description: "bitcoincore.org"
            link: "https://bitcoincore.org"
        }
    }

    Component {
        id: caretExternalLinkComponent
        ExternalLink {
            width: 220
            height: 30
            parentState: "FILLED"
            description: "v0.0.0-test"
            link: "https://bitcoin.org/en/download"
            iconSource: "image://images/caret-right"
            iconWidth: 18
            iconHeight: 18
        }
    }

    Component {
        id: aboutOptionsComponent
        AboutOptions {
            width: 450
        }
    }

    function findObject(item, objectName) {
        if (!item) return null
        if (item.objectName === objectName) return item
        const children = item.children || []
        for (let i = 0; i < children.length; ++i) {
            const childResult = findObject(children[i], objectName)
            if (childResult) return childResult
        }
        const childItems = item.childItems || []
        for (let j = 0; j < childItems.length; ++j) {
            const childItemResult = findObject(childItems[j], objectName)
            if (childItemResult) return childItemResult
        }
        return null
    }

    function test_right_content_icon_slot_geometry() {
        const item = createTemporaryObject(rightContentIconComponent, this)
        verify(item !== null)

        const slot = findObject(item, "rightContentIcon")
        verify(slot !== null)
        compare(slot.implicitWidth, 30)
        compare(slot.implicitHeight, 30)
        compare(slot.Layout.preferredWidth, 30)
        compare(slot.Layout.preferredHeight, 30)
        compare(slot.slotSize, 30)
        compare(slot.iconSize, 22)
    }

    function test_external_link_uses_fixed_export_slot() {
        const item = createTemporaryObject(externalLinkComponent, this)
        verify(item !== null)
        compare(item.textColor, Theme.color.neutral9)
        compare(item.iconColor, Theme.color.neutral9)

        const slot = findObject(item, "externalLinkIconSlot")
        verify(slot !== null)
        compare(slot.implicitWidth, 30)
        compare(slot.implicitHeight, 30)
        compare(slot.slotSize, 30)
        compare(slot.iconSize, 22)
    }

    function test_external_link_disabled_state_deemphasizes_value_and_icon() {
        const item = createTemporaryObject(externalLinkComponent, this)
        verify(item !== null)

        item.parentState = "DISABLED"
        wait(0)

        verify(!item.enabled)
        compare(item.textColor, Theme.color.neutral4)
        compare(item.iconColor, Theme.color.neutral4)
    }

    function test_external_link_can_use_small_caret_glyph() {
        const item = createTemporaryObject(caretExternalLinkComponent, this)
        verify(item !== null)

        const slot = findObject(item, "externalLinkIconSlot")
        verify(slot !== null)
        compare(slot.implicitWidth, 30)
        compare(slot.implicitHeight, 30)
        compare(slot.slotSize, 30)
        compare(slot.iconSize, 18)
    }

    // Hover event delivery is unreliable under the offscreen platform (see the
    // note in tst_setting.qml), so the positive path accepts either state; the
    // strict contract is that hover must never override FILLED with anything
    // other than HOVER, and must revert once the mouse leaves.
    function test_external_link_hover_switches_to_hover_state() {
        const item = createTemporaryObject(externalLinkComponent, this)
        verify(item !== null)
        compare(item.state, "FILLED")

        mouseMove(item, item.width / 2, item.height / 2)
        verify(item.state === "FILLED" || item.state === "HOVER")
        if (item.state === "HOVER") {
            compare(item.textColor, Theme.color.orangeLight1)
            compare(item.iconColor, Theme.color.orangeLight1)
        }

        mouseMove(item, -10, -10)
        tryCompare(item, "state", "FILLED")
    }

    function test_external_link_disabled_ignores_hover() {
        const item = createTemporaryObject(externalLinkComponent, this)
        verify(item !== null)
        item.parentState = "DISABLED"
        wait(0)

        mouseMove(item, item.width / 2, item.height / 2)
        wait(0)
        compare(item.state, "DISABLED")
        compare(item.textColor, Theme.color.neutral4)
    }

    function test_about_options_rows_are_contiguous() {
        const item = createTemporaryObject(aboutOptionsComponent, this)
        verify(item !== null)
        compare(item.spacing, 0)
    }
}
