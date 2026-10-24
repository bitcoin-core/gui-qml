// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"
import "../../qml/pages/settings"

TestCase {
    id: testCase
    name: "DesignSystem"
    when: windowShown
    visible: true
    width: 900
    height: 900

    readonly property var sectionNames: [
        "designSystemPageControlsSection",
        "designSystemColorsSection",
        "designSystemTypographySection",
        "designSystemButtonsSection",
        "designSystemSelectionSection",
        "designSystemFieldsSection",
        "designSystemRowsSection",
        "designSystemNavigationSection",
        "designSystemFeedbackSection",
        "designSystemMenusSection",
        "designSystemDataDisplaySection"
    ]

    readonly property var specimenNames: [
        "designSystemColorRoles", "designSystemColorLists", "designSystemTextRoles",
        "designSystemContinueButton", "designSystemNeutralButton", "designSystemOutlineButton",
        "designSystemTextButton", "designSystemLinkButton", "designSystemIconButton", "designSystemCloseButton",
        "designSystemCopyButton", "designSystemOverflowMenuButton", "designSystemFilterButton",
        "designSystemDropdownButton", "designSystemAmountUnitButton", "designSystemNavButton",
        "designSystemCheckBox", "designSystemOptionSwitch", "designSystemSegmentedPicker", "designSystemPopupPicker",
        "designSystemContextMenuPicker", "designSystemSlider", "designSystemRangeSlider", "designSystemOptionButton",
        "designSystemWalletCreationTypeRow",
        "designSystemLabeledTextField", "designSystemPasswordTextField", "designSystemLabeledTextArea",
        "designSystemLabeledTextView", "designSystemSearchBar", "designSystemTextFieldRowSection",
        "designSystemBitcoinAddressInputField", "designSystemBitcoinAmountInputField",
        "designSystemFormSection", "designSystemFormRowSection", "designSystemSwitchRowSection",
        "designSystemListRowSection", "designSystemValueSection", "designSystemKeyValueRow",
        "designSystemNavigationBar2", "designSystemSettingsHeader", "designSystemPageHeading", "designSystemHeader",
        "designSystemNavigationTab",
        "designSystemInfoBanner", "designSystemSettingsRestartNotice", "designSystemToastBanner",
        "designSystemStatusPill", "designSystemConfirmationPill", "designSystemTooltip",
        "designSystemSkeleton", "designSystemSpinningIndicator",
        "designSystemContextMenu", "designSystemAlertPopup",
        "designSystemBitcoinAmountDisplayLabel", "designSystemAddressLabel", "designSystemQRImage"
    ]

    Component {
        id: designSystemPageComponent

        SettingsDesignSystem {
            width: testCase.width
            height: testCase.height
        }
    }

    function roleNames(object) {
        const names = []
        for (const name in object) {
            if (name !== "objectName" && typeof object[name] !== "function") names.push(name)
        }
        return names
    }

    function createPage() {
        // failOnWarning() exists from Qt 6.3; older Qt only loads the page.
        if (typeof failOnWarning === "function") failOnWarning(/qrc:\/qml\//)
        const page = createTemporaryObject(designSystemPageComponent, testCase)
        verify(page !== null)
        waitForRendering(page)
        return page
    }

    function test_everySectionAndSpecimenLoads() {
        const page = createPage()
        for (const name of sectionNames) verify(findChild(page, name) !== null, name)
        for (const name of specimenNames) verify(findChild(page, name) !== null, name)
    }

    function test_foundationsListEveryThemeRole() {
        const page = createPage()
        for (const name of roleNames(Theme.color)) verify(findChild(page, "designSystemColor_" + name) !== null, name)
        let textRoleCount = 0
        for (const name of roleNames(Theme.text)) {
            if (Theme.text[name].pixelSize === undefined) continue
            verify(findChild(page, "designSystemText_" + name) !== null, name)
            ++textRoleCount
        }
        verify(textRoleCount > 0)
        verify(findChild(page, "designSystemColor_lavender") !== null)
        verify(findChild(page, "designSystemColor_confirmationColors") !== null)
        verify(findChild(page, "designSystemText_captionStrong") !== null)
    }

    function test_menuAndAlertsOpen() {
        const page = createPage()

        const menuButton = findChild(page, "designSystemContextMenuButton")
        const menu = findChild(page, "designSystemExampleMenu")
        verify(menuButton !== null)
        verify(menu !== null)
        menuButton.clicked()
        tryCompare(menu, "opened", true)
        menu.close()
        tryCompare(menu, "visible", false)

        const alerts = [
            ["designSystemNeutralAlertButton", "designSystemNeutralAlert"],
            ["designSystemDestructiveAlertButton", "designSystemDestructiveAlert"],
            ["designSystemLongAlertButton", "designSystemLongAlert"]
        ]
        for (const [buttonName, alertName] of alerts) {
            const alert = findChild(page, alertName)
            verify(alert !== null, alertName)
            findChild(page, buttonName).clicked()
            tryCompare(alert, "opened", true)
            alert.close()
            tryCompare(alert, "visible", false)
        }
    }

    function isFocusIndicator(item) {
        // FocusBorder (buttons, rows) or FieldBorderRings (field editing).
        return item.targetRadius !== undefined && item.borderRadius !== undefined
            || item.ringColor !== undefined && item.cornerRadius !== undefined
    }

    function visibleFocusIndicators(item, result) {
        if (!item.visible || item.opacity === 0) return result
        if (isFocusIndicator(item)) result.push(item)
        for (let i = 0; i < item.children.length; ++i) visibleFocusIndicators(item.children[i], result)
        return result
    }

    function sceneRect(item) {
        const topLeft = item.mapToItem(null, 0, 0)
        return Qt.rect(topLeft.x, topLeft.y, item.width, item.height)
    }

    function contains(outer, inner) {
        return inner.x >= outer.x - 0.5 && inner.y >= outer.y - 0.5
            && inner.x + inner.width <= outer.x + outer.width + 0.5
            && inner.y + inner.height <= outer.y + outer.height + 0.5
    }

    function clippedBy(ring) {
        const rect = sceneRect(ring)
        for (let ancestor = ring.parent; ancestor; ancestor = ancestor.parent) {
            if (!ancestor.clip) continue
            const bounds = sceneRect(ancestor)
            // A vertical scroller hides rows that are off screen; only check its sides.
            const scrolls = ancestor.contentY !== undefined
                || (ancestor.contentItem && ancestor.contentItem.contentY !== undefined)
            if (scrolls) {
                bounds.y = rect.y
                bounds.height = rect.height
            }
            if (!contains(bounds, rect)) return ancestor
        }
        return null
    }

    function insideHostedFieldSpecimen(item) {
        // These bare inputs are always hosted in a LabeledField, which draws
        // the field halo; the showcase shows them on their own.
        const hosted = ["designSystemBitcoinAddressInputField", "designSystemBitcoinAmountInputField"]
        for (let ancestor = item; ancestor; ancestor = ancestor.parent) {
            if (ancestor.embedded === true) return false
            if (hosted.indexOf(ancestor.objectName) !== -1) return true
        }
        return false
    }

    function describe(item) {
        let name = String(item)
        for (let ancestor = item; ancestor; ancestor = ancestor.parent) {
            if (ancestor.objectName && ancestor.objectName.length > 0) return name + " in " + ancestor.objectName
        }
        return name
    }

    function test_tabWalksTheShowcaseWithVisibleFocus() {
        const page = createPage()
        const first = page.nextItemInFocusChain(true)
        verify(first !== null)
        first.forceActiveFocus(Qt.TabFocusReason)

        const visited = []
        let current = page.Window.activeFocusItem
        for (let step = 0; step < 500; ++step) {
            verify(current !== null)
            if (visited.indexOf(current) !== -1) break
            visited.push(current)

            // Menu items show focus by highlighting the row, as native menus do.
            const menuHighlight = current.Accessible.role === Accessible.MenuItem && current._highlighted === true
            const hostedField = insideHostedFieldSpecimen(current)
            const focusRect = sceneRect(current)
            const center = Qt.point(focusRect.x + focusRect.width / 2, focusRect.y + focusRect.height / 2)
            const ring = visibleFocusIndicators(page, []).find(function(indicator) {
                for (let ancestor = indicator.parent; ancestor; ancestor = ancestor.parent) {
                    if (ancestor === current) return true
                }
                const rect = sceneRect(indicator)
                return center.x >= rect.x && center.x <= rect.x + rect.width
                    && center.y >= rect.y && center.y <= rect.y + rect.height
            })
            verify(menuHighlight || hostedField || ring !== undefined, "no visible focus indicator on " + describe(current))
            const clip = ring !== undefined ? clippedBy(ring) : null
            verify(clip === null, "focus indicator on " + describe(current) + " clipped by " + describe(clip || page))

            keyClick(Qt.Key_Tab)
            const next = page.Window.activeFocusItem
            verify(next !== current, "Tab stays on " + describe(current))
            current = next
        }
        compare(current, visited[0], "Tab cycles back to " + describe(current) + " before reaching the first stop")
        verify(visited.length > 50, "only " + visited.length + " tab stops")
    }

    function test_existingFormExamplesStayAvailable() {
        const page = createPage()
        verify(findChild(page, "settingsPageContentLayout") !== null)
        verify(findChild(page, "designSystemThemeRow") !== null)
        verify(findChild(page, "designSystemLanguagePicker") !== null)
        verify(findChild(page, "designSystemWebsiteRow") !== null)
        verify(findChild(page, "designSystemVersionRow") !== null)
        verify(findChild(page, "designSystemBlockStorageField") !== null)
        verify(findChild(page, "designSystemProxyLocationField") !== null)
        verify(findChild(page, "designSystemDataDirectoryValue") !== null)
    }
}
