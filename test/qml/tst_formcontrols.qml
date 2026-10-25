// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    id: testCase
    name: "FormControls"
    when: windowShown
    visible: true
    width: 640
    height: 640

    property string motionMode: ""

    function init() {
        motionMode = Theme.motion.mode
    }

    function cleanup() {
        Theme.motion.mode = motionMode
    }

    Item {
        id: host
        anchors.fill: parent
    }

    Component {
        id: formRowComponent

        FormRow {
            objectName: "exampleRow"
            width: 480
            title: "Theme"
            description: "Choose the application appearance."
            supportingText: "Managed by the application."
            trailingItem: OptionSwitch {
                objectName: "exampleSwitch"
                checked: true
            }
        }
    }

    Component {
        id: formSectionComponent

        FormSection {
            objectName: "exampleSection"
            width: 480
            title: "Appearance"
            footerText: "Changes apply immediately."

            FormRow {
                Layout.fillWidth: true
                title: "First"
            }

            FormRow {
                Layout.fillWidth: true
                title: "Second"
                showDivider: false
            }
        }
    }

    Component {
        id: sectionHeaderActionComponent

        NavButton {
            objectName: "exampleSectionHeaderAction"
            text: "More"
        }
    }

    Component {
        id: listRowComponent

        ListRow {
            objectName: "exampleDisclosureRow"
            width: 480
            title: "Display"
            selected: true
            showsDisclosureIndicator: true
            disclosureIndicatorObjectName: "exampleDisclosureRowDisclosureIndicator"
        }
    }

    Component {
        id: pageHeadingComponent

        PageHeading {
            objectName: "exampleHeading"
            width: 480
            title: "General"
            description: "Customize the application."
        }
    }

    Component {
        id: popupPickerComponent

        PopupPicker {
            objectName: "examplePicker"
            width: 180
            currentValue: "light"
            model: [
                { text: "Light", value: "light" },
                { text: "Dark", value: "dark" }
            ]
        }
    }

    Component {
        id: switchRowComponent

        SwitchRow {
            objectName: "exampleSwitchRow"
            width: 400
            title: "Open at login"
        }
    }

    Component {
        id: segmentedPickerComponent

        SegmentedPicker {
            objectName: "exampleSegments"
            width: 260
            model: ["Light", "Dark"]
            onSelected: function(index, option) { currentIndex = index }
        }
    }

    Component {
        id: optionButtonComponent

        OptionButton {
            property string model: "a"
            width: 300
            text: "Option B"
            selected: model === "b"
        }
    }

    Component {
        id: largeNeutralButtonComponent

        NeutralButton {
            objectName: "exampleLargeNeutralButton"
            buttonSize: NeutralButton.Large
            width: 140
            height: 46
            text: "Cancel"
        }
    }

    Component {
        id: continueButtonComponent

        ContinueButton {
            text: "Continue"
            enabled: false
        }
    }

    Component {
        id: closeButtonComponent

        CloseButton {
            objectName: "exampleCloseButton"
        }
    }

    Component {
        id: settingsPageComponent

        SettingsPage {
            objectName: "exampleSettingsPage"
            width: 720
            height: 640
            title: "Settings"
            backButtonObjectName: "exampleSettingsBack"
            maximumContentWidth: 420
            contentSpacing: 12
            rightItem: NavButton {
                objectName: "exampleSettingsAction"
                text: "Done"
            }

            FormSection {
                objectName: "exampleSettingsSection"
                Layout.fillWidth: true
                title: "General"

                FormRow {
                    Layout.fillWidth: true
                    title: "Example"
                    showDivider: false
                }
            }
        }
    }

    Component {
        id: valueRowComponent

        ValueRow {
            objectName: "exampleValueRow"
            width: 480
            title: "Version"
            value: "v31.99.0-unk"
        }
    }

    Component {
        id: linkRowComponent

        LinkRow {
            objectName: "exampleLinkRow"
            width: 480
            title: "Website"
            value: "bitcoincore.org"
            link: "https://bitcoincore.org"
        }
    }

    Component {
        id: textFieldRowComponent

        TextFieldRow {
            objectName: "exampleTextFieldRow"
            fieldObjectName: "exampleTextField"
            width: 480
            title: "Proxy location"
            text: "127.0.0.1:9050"
        }
    }

    Component {
        id: labeledTextFieldComponent

        LabeledTextField {
            objectName: "exampleLabeledTextField"
            width: 480
            label: "Username"
            fieldObjectName: "exampleLabeledTextFieldInput"
            placeholderText: "Enter username..."
        }
    }

    Component {
        id: passwordTextFieldComponent

        PasswordTextField {
            objectName: "examplePasswordTextField"
            width: 480
            label: "Password"
            fieldObjectName: "examplePasswordTextFieldInput"
            visibilityToggleObjectName: "examplePasswordTextFieldToggle"
            placeholderText: "Enter password..."
        }
    }

    Component {
        id: bodyRowComponent

        FormRow {
            objectName: "exampleBodyRow"
            width: 480
            title: "Data directory"
            bodyItem: CoreText {
                objectName: "exampleBodyContent"
                Layout.fillWidth: true
                text: "/Users/example/Bitcoin"
            }
        }
    }

    function test_formRowLoadsAndDisablesTrailingControl() {
        const row = createTemporaryObject(formRowComponent, host)
        verify(row !== null)
        tryVerify(function() { return row.loadedTrailingItem !== null })
        compare(row.loadedTrailingItem.objectName, "exampleSwitch")
        compare(row.loadedTrailingItem.enabled, true)
        compare(row.dividerColor, Theme.color.surfaceDivider)

        row.enabled = false
        compare(row.loadedTrailingItem.enabled, false)
        tryCompare(findChild(row, "exampleRowTitle"), "color", Theme.color.disabledContent)
    }

    function test_formRowValidationChangesHeightWithoutChangingPreferredWidth() {
        const row = createTemporaryObject(textFieldRowComponent, host, { width: 360 })
        verify(row !== null)
        wait(0)
        const preferredWidth = row.implicitWidth
        const normalHeight = row.implicitHeight

        row.errorText = "A validation message that must wrap within the row. ".repeat(8)
        tryVerify(function() { return row.implicitHeight > normalHeight })
        compare(row.implicitWidth, preferredWidth)
        row.width = 280
        tryVerify(function() { return row.field.mapToItem(row, 0, 0).y >= row.topPadding })
        compare(row.implicitWidth, preferredWidth)

        row.errorText = ""
        tryCompare(row, "implicitHeight", normalHeight)
    }

    function test_formSectionOwnsCardAndContent() {
        const section = createTemporaryObject(formSectionComponent, host)
        verify(section !== null)
        const card = findChild(section, "exampleSectionCard")
        verify(card !== null)
        compare(card.color, Theme.color.surface1)
        compare(card.radius, 16)
        const footer = findChild(section, "exampleSectionFooter")
        verify(footer !== null)
        compare(footer.text, "Changes apply immediately.")
        compare(footer.font.pixelSize, Theme.text.caption.font.pixelSize)
        verify(section.implicitHeight > 0)
    }

    function test_formSectionSizesHeaderAndContentIndependently() {
        const section = createTemporaryObject(formSectionComponent, host)
        verify(section !== null)
        const card = findChild(section, "exampleSectionCard")
        tryCompare(card, "height", 96)
        tryCompare(card, "width", section.width)
        const heightWithHeaderAndFooter = section.implicitHeight

        section.width = 320
        tryCompare(card, "width", 320)
        tryCompare(card, "height", 96)
        section.title = ""
        section.footerText = ""
        tryCompare(section, "implicitHeight", card.implicitHeight)

        section.headerTrailingItem = sectionHeaderActionComponent
        tryVerify(function() { return findChild(section, "exampleSectionHeaderAction") !== null })
        tryVerify(function() { return section.implicitHeight > card.implicitHeight })
        section.width = 480
        section.title = "Appearance"
        section.footerText = "Changes apply immediately."
        tryVerify(function() { return section.implicitHeight >= heightWithHeaderAndFooter })
        section.headerTrailingItem = null
        tryCompare(section, "implicitHeight", heightWithHeaderAndFooter)
    }

    function test_formSectionResizesWrappedContent() {
        const section = createTemporaryObject(formSectionComponent, host, {
            description: "A description that wraps to fit the available width. ".repeat(8),
            footerText: "Footer text that also wraps at narrow widths. ".repeat(4)
        })
        verify(section !== null)
        const card = findChild(section, "exampleSectionCard")
        const footer = findChild(section, "exampleSectionFooter")
        verify(card !== null)
        verify(footer !== null)
        wait(0)
        const wideHeight = section.implicitHeight

        section.width = 260
        tryVerify(function() { return section.implicitHeight > wideHeight })
        compare(card.width, section.width)
        verify(footer.y >= card.y + card.height)
        verify(footer.mapToItem(section, 0, footer.height).y <= section.height + 1)

        const narrowHeight = section.implicitHeight
        section.footerText = ""
        tryVerify(function() { return section.implicitHeight < narrowHeight })
        compare(card.width, section.width)
    }

    function test_listRowSelectionAndActivation() {
        const row = createTemporaryObject(listRowComponent, host)
        verify(row !== null)
        compare(row.selected, true)
        compare(row.background.color, row.selectedBackgroundColor)
        compare(row.cornerRadius, 16)
        compare(row.background.radius, 16)
        const disclosureIndicator = findChild(row, "exampleDisclosureRowDisclosureIndicator")
        verify(disclosureIndicator !== null)
        compare(disclosureIndicator.size, 14)

        let clickCount = 0
        row.clicked.connect(function() { clickCount += 1 })
        row.clicked()
        compare(clickCount, 1)
    }

    function test_pageHeadingUsesThemeTypography() {
        const heading = createTemporaryObject(pageHeadingComponent, host)
        verify(heading !== null)
        const title = findChild(heading, "exampleHeadingTitle")
        const description = findChild(heading, "exampleHeadingDescription")
        verify(title !== null)
        verify(description !== null)
        compare(title.font.pixelSize, Theme.text.headline.pixelSize)
        compare(description.font.pixelSize, Theme.text.description.font.pixelSize)
    }

    function test_popupPickerMapsValuesAndLeavesStateCallerOwned() {
        const picker = createTemporaryObject(popupPickerComponent, host)
        verify(picker !== null)
        compare(picker.currentText, "Light")
        const button = findChild(picker, "examplePickerButton")
        const menu = findChild(picker, "examplePickerMenu")
        verify(button !== null)
        verify(menu !== null)
        compare(button.defaultBgColor, Theme.color.surface1)
        compare(button.hoverBgColor, Theme.color.surface2)
        compare(menu.backgroundColor, Theme.color.surface1)

        picker.surfaceLevel = 1
        compare(button.defaultBgColor, Theme.color.surface2)
        compare(button.hoverBgColor, Theme.color.surface3)
        compare(menu.backgroundColor, Theme.color.surface2)

        picker.currentValue = "dark"
        compare(picker.currentText, "Dark")

        let activatedValue = ""
        picker.activated.connect(function(value) { activatedValue = value })
        tryVerify(function() { return picker.itemAtIndex(0) !== null })
        picker.itemAtIndex(0).triggered()

        compare(activatedValue, "light")
        compare(picker.currentValue, "dark")
    }

    function test_segmentedPickerPaintsSelection_data() {
        const cases = []
        for (const palette of ["core-light", "core-dark"]) {
            for (const mode of ["full", "reduced", "off"]) {
                for (const scale of [0.85, 1.3]) {
                    cases.push({tag: palette + "-" + mode + "-" + scale, palette: palette, mode: mode, scale: scale})
                }
            }
        }
        return cases
    }

    function test_segmentedPickerPaintsSelection(data) {
        const palette = Theme.paletteId
        const scale = Theme.textScale
        try {
            Theme.paletteId = data.palette
            Theme.textScale = data.scale
            Theme.motion.mode = data.mode
            const picker = createTemporaryObject(segmentedPickerComponent, host)
            verify(picker !== null)
            tryCompare(picker, "count", 2)
            for (const index of [0, 1, 0]) {
                const segment = picker.itemAt(index)
                verify(segment.width > 0 && segment.height > 0)
                mouseClick(segment)
                tryCompare(picker, "currentIndex", index)
                compare(picker.currentText, picker.model[index])
                tryCompare(segment.background, "color", Theme.color.accent)
                verify(waitForRendering(picker))
                const point = segment.mapToItem(picker, segment.width / 2, 2)
                let captured = false
                let color
                verify(picker.grabToImage(function(result) {
                    color = testImage.pixelAt(result.image, point, picker.width)
                    captured = true
                }))
                tryVerify(function() { return captured })
                compare(color, Theme.color.accent)
            }
        } finally {
            Theme.paletteId = palette
            Theme.textScale = scale
        }
    }

    function test_switchRowFlipsOnceFromRowOrSwitch() {
        const row = createTemporaryObject(switchRowComponent, host)
        verify(row !== null)
        const toggle = findChild(row, "exampleSwitchRowSwitch")
        verify(toggle !== null)
        verify(waitForRendering(row))
        let toggles = 0
        row.toggled.connect(function() { toggles += 1 })

        mouseClick(row, Theme.space.cardInset + 4, row.height / 2)
        compare(row.checked, true)
        compare(toggles, 1)
        mouseClick(toggle)
        compare(row.checked, false)
        compare(toggles, 2)
        toggle.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(row.checked, true)
        compare(toggles, 3)

        row.enabled = false
        mouseClick(row, Theme.space.cardInset + 4, row.height / 2)
        compare(row.checked, true)
        compare(toggles, 3)
    }

    function test_optionButtonSelectionStaysBoundToTheModel() {
        const option = createTemporaryObject(optionButtonComponent, host)
        verify(option !== null)
        compare(option.checked, false)
        compare(option.Accessible.role, Accessible.RadioButton)

        // A click the caller ignores leaves the tile unselected...
        mouseClick(option)
        compare(option.checked, false)
        option.forceActiveFocus()
        keyClick(Qt.Key_Space)
        compare(option.checked, false)

        // ...and the binding still follows the model afterwards.
        option.model = "b"
        compare(option.checked, true)
        mouseClick(option)
        compare(option.checked, true)
        option.model = "a"
        compare(option.checked, false)
    }

    function test_largeNeutralButtonAppearance() {
        Theme.motion.mode = "full"
        const button = createTemporaryObject(largeNeutralButtonComponent, host)
        verify(button !== null)
        const background = findChild(button, "exampleLargeNeutralButtonBackground")
        verify(background !== null)

        compare(button.buttonSize, NeutralButton.Large)
        compare(button.backgroundColor, Theme.color.surface2)
        compare(button.currentBackgroundColor, Theme.color.surface2)
        compare(button.implicitHeight, 46)

        button.down = true
        compare(button.currentBackgroundColor, Theme.color.surface3)
        tryCompare(button, "scale", Theme.motion.pressScale)

        button.down = false
        tryCompare(button, "scale", 1.0)
    }

    function test_continueButtonKeepsPrimaryColorWhenDisabled() {
        Theme.motion.mode = "full"
        const button = createTemporaryObject(continueButtonComponent, host)
        verify(button !== null)

        compare(button.enabled, false)
        compare(button.background.color, Theme.color.accent)
        compare(button.textColor, Theme.color.accentForeground)
        compare(button.opacity, 0.4)

        button.down = true
        compare(button.background.color, Theme.color.accent)
        compare(button.textColor, Theme.color.accentForeground)
        compare(button.scale, 1.0)

        button.enabled = true
        tryCompare(button.background, "color", Theme.color.accentPressed)
        compare(button.textColor, Theme.color.accentForeground)
        compare(button.opacity, 1.0)
        tryCompare(button, "scale", Theme.motion.pressScale)

        button.down = false
        tryCompare(button.background, "color", Theme.color.accent)
        tryCompare(button, "scale", 1.0)
    }

    function test_closeButtonInteractionStates() {
        Theme.motion.mode = "full"
        const button = createTemporaryObject(closeButtonComponent, host)
        verify(button !== null)
        const background = findChild(button, "exampleCloseButtonBackground")
        const icon = findChild(button, "exampleCloseButtonIcon")
        verify(background !== null)
        verify(icon !== null)

        mouseMove(host, host.width - 1, host.height - 1)
        compare(button.width, 30)
        compare(button.height, 30)
        compare(button.scale, 1.0)
        compare(background.radius, 15)
        tryCompare(background, "color", Theme.color.surface2)
        compare(icon.size, 10)
        compare(icon.color, Theme.color.neutral6)
        compare(button.backgroundHoverColor, Theme.color.surface3)
        compare(button.backgroundPressedColor, Theme.color.surface3)

        button.down = true
        tryCompare(background, "color", Theme.color.surface3)
        tryCompare(button, "scale", Theme.motion.pressScale)

        button.down = false
        tryCompare(button, "scale", 1.0)
        tryCompare(background, "color", Theme.color.surface2)
    }

    function test_settingsPageOwnsNavigationAndConstrainedScrollableContent() {
        const page = createTemporaryObject(settingsPageComponent, host)
        verify(page !== null)
        compare(page.pageHeader.title, "Settings")
        compare(page.pageHeader.backButtonObjectName, "exampleSettingsBack")
        verify(findChild(page, "exampleSettingsAction") !== null)
        verify(findChild(page, "exampleSettingsSection") !== null)
        compare(page.contentLayout.width, 420)
        compare(page.contentLayout.spacing, 12)
        compare(page.scrollView.contentWidth, page.scrollView.availableWidth)

        let backCount = 0
        page.back.connect(function() { backCount += 1 })
        page.pageHeader.back()
        compare(backCount, 1)
    }

    function test_settingsPageScrollHeightTracksWrappedContent() {
        const page = createTemporaryObject(settingsPageComponent, host, { height: 200 })
        verify(page !== null)
        const section = findChild(page, "exampleSettingsSection")
        verify(section !== null)
        section.description = "Content that must remain reachable when the page is narrow. ".repeat(20)
        tryVerify(function() { return page.scrollView.contentHeight > page.scrollView.availableHeight })
        const wideHeight = page.scrollView.contentHeight

        page.width = 360
        tryVerify(function() { return page.scrollView.contentHeight > wideHeight })
        compare(page.contentLayout.y, page.contentTopPadding)
        fuzzyCompare(page.scrollView.contentHeight,
                     page.contentTopPadding + page.contentLayout.height + page.contentBottomPadding, 1)

        const flickable = page.scrollView.contentItem
        flickable.contentY = page.scrollView.contentHeight - page.scrollView.availableHeight
        verify(flickable.contentY > 0)
    }

    function test_settingsPageKeepsImplicitWidthIndependentOfLayout() {
        const page = createTemporaryObject(settingsPageComponent, host)
        verify(page !== null)

        const implicitWidthBefore = page.implicitWidth
        const widthBefore = page.width
        page.width = widthBefore / 2

        verify(page.width !== widthBefore)
        compare(page.implicitWidth, implicitWidthBefore)
    }

    function test_valueRowDisplaysCallerOwnedValue() {
        const row = createTemporaryObject(valueRowComponent, host)
        verify(row !== null)
        const value = findChild(row, "exampleValueRowValue")
        verify(value !== null)
        compare(value.text, "v31.99.0-unk")

        row.value = "v32.0"
        compare(value.text, "v32.0")
    }

    function test_pageHeadingAlignsDescriptionWithTitle() {
        const heading = createTemporaryObject(pageHeadingComponent, host)
        verify(heading !== null)
        const description = findChild(heading, "exampleHeadingDescription")
        verify(description !== null)
        compare(heading.descriptionTextStyle.font.pixelSize, Theme.text.description.font.pixelSize)
        compare(description.font.pixelSize, Theme.text.description.font.pixelSize)
        compare(description.horizontalAlignment, Text.AlignLeft)
        heading.titleAlignment = Text.AlignHCenter
        compare(description.horizontalAlignment, Text.AlignHCenter)
    }

    function test_linkRowEmitsWithoutOpeningTheUrl() {
        const row = createTemporaryObject(linkRowComponent, host)
        verify(row !== null)

        let activatedLink = ""
        row.activated.connect(function(link) { activatedLink = link.toString() })
        row.clicked()

        compare(activatedLink, "https://bitcoincore.org")
        compare(findChild(row, "exampleLinkRowValue").text, "bitcoincore.org")
    }

    function test_textFieldRowUsesCompactTrailingEditor() {
        const row = createTemporaryObject(textFieldRowComponent, host)
        verify(row !== null)
        verify(row.field !== null)
        compare(row.field.objectName, "exampleTextField")
        compare(row.loadedTrailingAccessory, null)
        compare(row.loadedBodyItem, null)
        compare(row.text, "127.0.0.1:9050")
        compare(row.field.horizontalAlignment, Text.AlignRight)
        compare(row.focusBorderColor, Theme.color.neutral6)
        compare(row.fieldFocusRing.ringColor, Theme.color.neutral6)
        compare(row.field.background.color, Qt.rgba(0, 0, 0, 0))

        row.text = "127.0.0.1:9150"
        compare(row.field.text, "127.0.0.1:9150")
    }

    function test_textFieldRowResignsFocusWhenAccepted() {
        const row = createTemporaryObject(textFieldRowComponent, host)
        verify(row !== null)
        verify(row.field !== null)

        let acceptedCount = 0
        row.accepted.connect(function() { acceptedCount += 1 })

        row.field.forceActiveFocus()
        verify(row.field.activeFocus)
        keyClick(Qt.Key_Return)
        tryCompare(row.field, "activeFocus", false)
        compare(acceptedCount, 1)

        row.field.forceActiveFocus()
        verify(row.field.activeFocus)
        keyClick(Qt.Key_Enter)
        tryCompare(row.field, "activeFocus", false)
        compare(acceptedCount, 2)
    }

    function test_labeledTextFieldOwnsLabelSurfaceAndReturnBehavior() {
        const control = createTemporaryObject(labeledTextFieldComponent, host)
        verify(control !== null)
        const label = findChild(control, "exampleLabeledTextFieldLabel")
        const field = findChild(control, "exampleLabeledTextFieldInput")
        verify(label !== null)
        verify(field !== null)
        compare(label.text, "Username")
        compare(field.placeholderText, "Enter username...")
        compare(field.background.radius, 10)
        compare(field.background.color, Theme.color.surface1)
        const focusRing = findChild(control, "exampleLabeledTextFieldInputFocusRing")
        verify(focusRing !== null)
        compare(focusRing.visible, false)

        field.forceActiveFocus()
        tryCompare(focusRing, "visible", true)
        compare(focusRing.parent.ringColor, Theme.color.neutral6)
        keyClick(Qt.Key_Return)
        tryCompare(field, "activeFocus", false)
        compare(focusRing.visible, false)
    }

    function test_passwordTextFieldComposesSecureEntryAndVisibilityToggle() {
        const control = createTemporaryObject(passwordTextFieldComponent, host)
        verify(control !== null)
        const field = findChild(control, "examplePasswordTextFieldInput")
        const toggle = findChild(control, "examplePasswordTextFieldToggle")
        verify(field !== null)
        verify(toggle !== null)
        compare(control.passwordVisible, false)
        compare(field.echoMode, TextInput.Password)

        toggle.clicked()
        compare(control.passwordVisible, true)
        compare(field.echoMode, TextInput.Normal)

        toggle.clicked()
        compare(control.passwordVisible, false)
        compare(field.echoMode, TextInput.Password)
    }

    function test_formRowAcceptsFullWidthBodyContent() {
        const row = createTemporaryObject(bodyRowComponent, host)
        verify(row !== null)
        verify(row.loadedBodyItem !== null)
        compare(row.loadedBodyItem.objectName, "exampleBodyContent")
        compare(row.loadedBodyItem.text, "/Users/example/Bitcoin")
        verify(row.implicitHeight > row.minimumRowHeight)
    }
}
