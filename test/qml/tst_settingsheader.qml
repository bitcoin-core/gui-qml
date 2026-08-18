// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2

import "../../qml/controls"

TestCase {
    id: testCase
    name: "SettingsHeader"
    when: windowShown
    visible: true
    width: 400
    height: 80

    // The header starts inside an invisible container, then the container is
    // revealed. This mirrors the real failure: SettingsHeader is built as a
    // Page header before its parent chain is effectively visible.
    Component {
        id: deferredContainer
        Item {
            visible: false
            width: 400
            height: 80
            SettingsHeader {
                id: hdr
                width: 400
                title: "Test"
                rightItem: Item { implicitWidth: 30; implicitHeight: 30 }
            }
        }
    }

    Component {
        id: labeledBackHeader
        SettingsHeader {
            width: 400
            title: "About"
            backButtonObjectName: "settingsAboutBack"
            backButtonText: "Back"
        }
    }

    Component {
        id: compactBackHeader
        SettingsHeader {
            width: 400
            title: "Display"
            backButtonObjectName: "compactSettingsBack"
        }
    }

    // Regression: the right section must show its actions once visible. The
    // previous binding also read contentItem.visible, the child's *effective*
    // visibility, which includes the section's own Pane. Built while the parent
    // is not yet effectively visible, that self-reference latches to false and
    // never recovers when the parent is shown, dropping the right-side actions
    // (e.g. the debug.log refresh/export buttons).
    function test_rightSectionRecoversWhenParentShown() {
        const container = createTemporaryObject(deferredContainer, testCase)
        verify(container !== null)
        const rightSection = findChild(container, "settingsHeaderRightSection")
        verify(rightSection !== null)

        container.visible = true
        tryVerify(function() { return rightSection.visible })
    }

    function test_backButtonCanShowLabel() {
        const header = createTemporaryObject(labeledBackHeader, testCase)
        verify(header !== null)
        const backButton = findChild(header, "settingsAboutBack")
        verify(backButton !== null)

        compare(backButton.text, "Back")
        compare(backButton.iconSource.toString(), "image://images/caret-left")
        tryVerify(function() { return backButton.width > 40 })
    }

    function test_compactBackIconKeepsSizeAcrossThemeChanges() {
        const originalDark = Theme.dark
        const header = createTemporaryObject(compactBackHeader, testCase)
        verify(header !== null)
        const icon = findChild(header, "settingsHeaderBackIcon")
        verify(icon !== null)

        compare(icon.width, 24)
        compare(icon.height, 24)

        Theme.dark = !originalDark
        tryCompare(icon, "width", 24)
        tryCompare(icon, "height", 24)
        Theme.dark = originalDark
    }
}
