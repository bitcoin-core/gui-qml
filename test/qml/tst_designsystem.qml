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
        "designSystemMotionSection",
        "designSystemLayoutSection",
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
        "designSystemColorBase", "designSystemColorRoles", "designSystemSurfaces", "designSystemColorLists",
        "designSystemColorAliases", "designSystemTypeSet", "designSystemTextRoles",
        "designSystemMotionDurations", "designSystemMotionEasing", "designSystemMotionTransitions",
        "designSystemMotionHelpers",
        "designSystemSpaceScale", "designSystemRadii", "designSystemSizes", "designSystemIconSizes",
        "designSystemFocusRing", "designSystemBreakpoints",
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
        "designSystemNavigationBar2", "designSystemSettingsHeader", "designSystemPageHeading",
        "designSystemNavigationTab",
        "designSystemInfoBanner", "designSystemSettingsRestartNotice", "designSystemToastBanner",
        "designSystemStatusPill", "designSystemNetworkIndicator", "designSystemConfirmationPill", "designSystemTooltip",
        "designSystemSkeleton", "designSystemSpinningIndicator",
        "designSystemContextMenu", "designSystemAlertPopup", "designSystemDialogPopup",
        "designSystemBitcoinAmountDisplayLabel", "designSystemAddressLabel", "designSystemQRImage"
    ]

    // Roles a screen reader treats as plain containers, and the roles a
    // clickable control may have.
    readonly property var noSemanticRoles: [Accessible.NoRole, Accessible.Client, Accessible.Pane, Accessible.Grouping,
        Accessible.Border, Accessible.Whitespace, Accessible.Separator]
    readonly property var buttonRoles: [Accessible.Button, Accessible.CheckBox, Accessible.RadioButton,
        Accessible.PageTab, Accessible.ListItem, Accessible.MenuItem, Accessible.Link, Accessible.ButtonMenu,
        Accessible.ButtonDropDown, Accessible.ComboBox]

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
        for (const name of roleNames(Theme.color)) {
            const type = typeof Theme.color[name]
            if (type === "string" || type === "boolean") continue
            verify(findChild(page, "designSystemColor_" + name) !== null, name)
        }
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
        verify(findChild(page, "designSystemText_amountDescription") !== null)
    }

    function test_motionSectionListsTokensAndSwitchesModeForThePage() {
        const startMode = Theme.motion.mode
        const page = createPage()
        for (const name of ["fast", "normal", "moderate", "exit", "slow", "navigation", "tooltipDelay"]) {
            verify(findChild(page, "designSystemMotion_" + name) !== null, name)
        }
        for (const name of ["enter", "exit", "move", "loop"]) {
            verify(findChild(page, "designSystemMotionEasing_" + name) !== null, name)
        }
        const picker = findChild(page, "designSystemMotionModePicker")
        compare(picker.currentText, startMode)
        picker.selected(1, "reduced")
        compare(Theme.motion.mode, "reduced")
        compare(picker.currentText, "reduced")
        findChild(page, "designSystemMotionReplayButton").clicked()
        // Settings keeps the page alive when another section is shown.
        page.visible = false
        compare(Theme.motion.mode, startMode)
        page.visible = true
        picker.selected(1, "reduced")
        page.destroy()
        wait(0)
        compare(Theme.motion.mode, startMode)
    }

    function test_layoutSectionListsTokensAndPreviewsWidths() {
        const page = createPage()
        for (const name of roleNames(Theme.space)) verify(findChild(page, "designSystemSpace_" + name) !== null, name)
        for (const name of roleNames(Theme.radius)) verify(findChild(page, "designSystemRadius_" + name) !== null, name)
        for (const name of roleNames(Theme.size)) {
            if (name === "icon") continue
            verify(findChild(page, "designSystemSize_" + name) !== null, name)
        }
        for (const name of roleNames(Theme.size.icon)) {
            verify(findChild(page, "designSystemIconSize_" + name) !== null, name)
        }
        for (const name of roleNames(Theme.focusRing)) {
            if (name === "width") continue
            verify(findChild(page, "designSystemFocusRing_" + name) !== null, name)
        }

        const split = findChild(page, "designSystemLayoutSplitView")
        const picker = findChild(page, "designSystemPageWidthPicker")
        verify(split !== null)
        verify(picker !== null)
        compare(picker.currentText, "Window")
        compare(page.contentHorizontalPadding, Theme.space.pagePadding(page.width))
        compare(split.isCompact, false)

        picker.selected(1, picker.model[1])
        compare(page.previewWidth, 390)
        compare(picker.currentText, "390")
        compare(page.contentHorizontalPadding, Theme.space.pageCompact + (page.width - 390) / 2)
        tryCompare(split, "isCompact", true)
        compare(page.contentLayout.width, 390 - Theme.space.pageCompact * 2)

        picker.selected(0, picker.model[0])
        tryCompare(split, "isCompact", false)

        const devicePicker = findChild(page, "designSystemDevicePicker")
        verify(devicePicker !== null)
        compare(devicePicker.currentText, "Desktop")
        try {
            devicePicker.selected(1, "Mobile")
            compare(SizeClass.windowMinimumWidth, 360)
            verify(SizeClass.isCompact(SizeClass.windowMinimumWidth))
            compare(devicePicker.currentText, "Mobile")
        } finally {
            devicePicker.selected(0, "Desktop")
        }
        compare(SizeClass.windowMinimumWidth, 800)
    }

    function test_palettePickerFollowsEveryPalette() {
        const originalPaletteId = Theme.paletteId
        try {
            const page = createPage()
            const picker = findChild(page, "designSystemPalettePicker")
            verify(picker !== null)
            compare(picker.model.length, Theme.palettes.length)
            const levels = ["background", "surface1", "surface2", "surface3"]
            for (let i = 0; i < Theme.palettes.length; ++i) {
                const palette = Theme.palettes[i]
                picker.activated(picker.model[i].value)
                compare(Theme.paletteId, palette.paletteId)
                compare(picker.currentText, palette.displayName)
                const neutral0 = findChild(page, "designSystemColor_neutral0")
                verify(Qt.colorEqual(neutral0.value, palette.neutral0), palette.paletteId)
                verify(findChild(page, "designSystemColor_surfaceHover") !== null)
                verify(findChild(page, "designSystemColor_white") !== null)

                let parentBox = null
                for (let level = 0; level < levels.length; ++level) {
                    const box = findChild(page, "designSystemSurface_" + levels[level])
                    verify(box !== null, levels[level])
                    verify(Qt.colorEqual(box.color, palette.surface(level)), palette.paletteId + " " + levels[level])
                    if (parentBox !== null) {
                        let ancestor = box.parent
                        while (ancestor !== null && ancestor !== parentBox) ancestor = ancestor.parent
                        verify(ancestor === parentBox, levels[level] + " is not nested in " + levels[level - 1])
                    }
                    for (const role of ["surfaceHover", "surfacePressed", "surfaceSelected"]) {
                        const state = findChild(box, "designSystemSurfaceState_" + levels[level] + "_" + role)
                        verify(state !== null, levels[level] + " " + role)
                        verify(Qt.colorEqual(state.color, palette[role]), palette.paletteId + " " + levels[level] + " " + role)
                    }
                    const divider = findChild(box, "designSystemSurfaceDivider_" + levels[level])
                    verify(Qt.colorEqual(divider.color, palette.surfaceDivider))
                    parentBox = box
                }
            }
        } finally {
            Theme.paletteId = originalPaletteId
        }
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
            ["designSystemLongAlertButton", "designSystemLongAlert"],
            ["designSystemSuccessAlertButton", "designSystemSuccessAlert"],
            ["designSystemErrorAlertButton", "designSystemErrorAlert"],
            ["designSystemInputAlertButton", "designSystemInputAlert"],
            ["designSystemFeatureModalButton", "designSystemFeatureModal"],
            ["designSystemSheetButton", "designSystemSheet"]
        ]
        for (const [buttonName, alertName] of alerts) {
            const alert = findChild(page, alertName)
            verify(alert !== null, alertName)
            findChild(page, buttonName).clicked()
            tryCompare(alert, "opened", true)
            alert.close()
            tryCompare(alert, "visible", false)
        }

        const longAlert = findChild(page, "designSystemLongAlert")
        findChild(page, "designSystemLongAlertButton").clicked()
        tryCompare(longAlert, "opened", true)
        verify(longAlert.height <= longAlert.maximumHeight)
        verify(findChild(longAlert.contentItem, "alertMessageArea").interactive)
        longAlert.close()
        tryCompare(longAlert, "visible", false)
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

    function isType(item, typeName) {
        // QML types print as Name_QMLTYPE_n(...), delegates add _QML_n.
        return new RegExp("^" + typeName + "(_QMLTYPE_\\d+)?(_QML_\\d+)?\\(").test(String(item))
    }

    function isIcon(item) {
        return isType(item, "Icon")
    }

    function isButton(item) {
        return item.checkable !== undefined && item.autoRepeat !== undefined && item.pressed !== undefined
    }

    function isTabStop(item) {
        return item.activeFocusOnTab === true
            || (item.focusPolicy !== undefined && (item.focusPolicy & Qt.TabFocus) !== 0)
    }

    function accessibilityProblems(item, problems) {
        if (!item.visible || item.opacity === 0) return problems
        const ignored = item.Accessible.ignored
        if (isIcon(item) && !ignored) problems.push("decorative Icon is not ignored: " + describe(item))
        // A disabled button drawn as a label (a non-interactive AddressLabel,
        // OptionButton's directory chip) is static text, not a control.
        const staticButton = isButton(item) && !isTabStop(item)
            && (!item.enabled || item.Accessible.role === Accessible.StaticText)
        if (!ignored && !staticButton && (isButton(item) || isTabStop(item))) {
            const info = testAccessibility.info(item)
            const what = describe(item) + " (role " + info.role + ")"
            // Qt never exposes a name for a password field, so its text can't leak.
            const passwordField = item.Accessible.passwordEdit === true
            if (!passwordField && (!info.name || info.name.length === 0)) problems.push("no accessible name: " + what)
            if (testCase.noSemanticRoles.indexOf(info.role) !== -1) problems.push("no accessible role: " + what)
            if (isButton(item) && testCase.buttonRoles.indexOf(info.role) === -1) problems.push("button has a non-button role: " + what)
            if (isButton(item) && item.checkable && (!info.checkable || info.checked !== item.checked))
                problems.push("check state not exposed: " + what)
        }
        if (!ignored) {
            const role = item.Accessible.role
            if ((role === Accessible.PageTabList || role === Accessible.ProgressBar) && item.Accessible.name.length === 0)
                problems.push("unnamed " + (role === Accessible.ProgressBar ? "progress indicator" : "tab list") + ": " + describe(item))
        }
        for (let i = 0; i < item.children.length; ++i) accessibilityProblems(item.children[i], problems)
        return problems
    }

    function test_controlsExposeNameRoleAndState() {
        const page = createPage()
        testAccessibility.activate()
        try {
            const problems = accessibilityProblems(page, [])
            compare(problems.join("\n"), "")
        } finally {
            testAccessibility.deactivate()
        }
    }

    function itemsOfType(item, typeName, result) {
        if (!item.visible) return result
        if (isType(item, typeName)) result.push(item)
        for (let i = 0; i < item.children.length; ++i) itemsOfType(item.children[i], typeName, result)
        return result
    }

    function test_tabsBadgesAndIndicatorsUseTheirRoles() {
        const page = createPage()
        testAccessibility.activate()
        try {

            const tabs = itemsOfType(page, "NavigationTab", [])
            verify(tabs.length > 0)
            for (const tab of tabs) {
                const info = testAccessibility.info(tab)
                compare(info.role, Accessible.PageTab, describe(tab))
                compare(info.checkable, true, describe(tab))
                compare(info.checked, tab.checked, describe(tab))
            }

            const pickers = itemsOfType(page, "SegmentedPicker", [])
            verify(pickers.length > 0)
            for (const picker of pickers) {
                compare(testAccessibility.info(picker).role, Accessible.PageTabList, describe(picker))
                const segments = itemsOfType(picker, "ToggleButton", [])
                compare(segments.length, picker.model.length, describe(picker))
                for (const segment of segments)
                    compare(testAccessibility.info(segment).role, Accessible.PageTab, describe(segment))
            }
            compare(testAccessibility.info(findChild(page, "designSystemThemeRow")).name, "Palette")

            const badges = itemsOfType(page, "NetworkIndicator", [])
            compare(badges.length, 4)
            for (const badge of badges) {
                const info = testAccessibility.info(badge)
                compare(info.role, Accessible.StaticText, describe(badge))
                compare(info.name, badge.text)
                compare(badge.focusPolicy, Qt.NoFocus)
            }

            const spinners = itemsOfType(page, "SpinningIndicator", [])
            verify(spinners.length > 0)
            for (const spinner of spinners) {
                compare(spinner.Accessible.ignored, !spinner.running, describe(spinner))
                compare(testAccessibility.info(spinner).role, Accessible.ProgressBar)
                compare(testAccessibility.info(spinner).name, "Loading")
            }
        } finally {
            testAccessibility.deactivate()
        }
    }

    function test_segmentedPickerArrowKeysMoveSelection() {
        const page = createPage()
        const picker = itemsOfType(findChild(page, "designSystemSegmentedPicker"), "SegmentedPicker", [])[0]
        const segments = itemsOfType(picker, "ToggleButton", [])
        compare(picker.currentIndex, 0)
        segments[0].forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Right)
        compare(picker.currentIndex, 1)
        verify(segments[1].activeFocus)
        keyClick(Qt.Key_Left)
        compare(picker.currentIndex, 0)
        verify(segments[0].activeFocus)
        keyClick(Qt.Key_Left)
        compare(picker.currentIndex, 0)
    }

    function test_addressesAndAmountsReadAsOneValue() {
        const page = createPage()
        testAccessibility.activate()
        try {
            const addresses = itemsOfType(findChild(page, "designSystemAddressLabel"), "AddressLabel", [])
            verify(addresses.length > 0)
            let interactiveCount = 0
            for (const label of addresses) {
                const info = testAccessibility.info(label)
                compare(info.name, label.address)
                compare(info.role, label.interactive ? Accessible.Button : Accessible.StaticText)
                if (label.interactive) {
                    verify(info.description.length > 0)
                    ++interactiveCount
                }
                for (const text of itemsOfType(label, "CoreText", [])) verify(text.Accessible.ignored, describe(text))
            }
            verify(interactiveCount > 0)

            const amounts = itemsOfType(findChild(page, "designSystemBitcoinAmountDisplayLabel"), "BitcoinAmountDisplayLabel", [])
            verify(amounts.length > 0)
            for (const amount of amounts) {
                const info = testAccessibility.info(amount)
                compare(info.role, Accessible.StaticText)
                compare(info.name, amount.text)
                for (const text of itemsOfType(amount, "CoreText", [])) verify(text.Accessible.ignored, describe(text))
            }
        } finally {
            testAccessibility.deactivate()
        }
    }

    function test_rowsNameTheirControls() {
        const page = createPage()
        testAccessibility.activate()
        try {
            const startup = testAccessibility.info(findChild(page, "designSystemStartupSwitch"))
            compare(startup.name, "Open at login")
            compare(startup.description, "Start the app after signing in.")
            // The row itself stays unnamed, so the label is read once.
            compare(testAccessibility.info(findChild(page, "designSystemStartupRow")).name, "")

            const acknowledgement = findChild(page, "designSystemAcknowledgementRow")
            let info = testAccessibility.info(acknowledgement)
            compare(info.role, Accessible.CheckBox)
            compare(info.name, acknowledgement.description)
            compare(info.checkable, true)
            compare(info.checked, false)
            acknowledgement.clicked()
            testAccessibility.activate()
            compare(testAccessibility.info(acknowledgement).checked, true)
            const toggle = acknowledgement.loadedTrailingItem
            acknowledgement.forceActiveFocus(Qt.TabFocusReason)
            for (let i = 0; i < 3; ++i) {
                keyClick(Qt.Key_Space)
                compare(acknowledgement.checked, toggle.checked)
            }
            compare(toggle.checked, false)
            toggle.toggle()
            compare(acknowledgement.checked, true)
            verify(acknowledgement.loadedTrailingItem.Accessible.ignored)
        } finally {
            testAccessibility.deactivate()
        }
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
