// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"
import "../../qml/components"

TestCase {
    id: testCase
    name: "FormControls"
    when: windowShown
    visible: true
    width: 640
    height: 640

    property string motionMode: ""

    function init() {
        testSetup.failOnWarnings()
        motionMode = Theme.motion.mode
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
        id: primaryButtonComponent

        PrimaryButton {
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

    function test_segmentedPickerWithoutSelectionHasNoText() {
        const picker = createTemporaryObject(segmentedPickerComponent, testCase)
        verify(picker !== null)
        picker.currentIndex = 1
        compare(picker.currentText, "Dark")
        // An unselected picker must not assign undefined (and warn).
        picker.currentIndex = -1
        compare(picker.currentText, "")
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

    function test_primaryButtonKeepsAccentColorWhenDisabled() {
        Theme.motion.mode = "full"
        const button = createTemporaryObject(primaryButtonComponent, host)
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
        compare(icon.color, Theme.color.neutral7)
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

    // #1132: a new passphrase drops pasted line endings; a field that unlocks
    // an existing wallet keeps exactly what was pasted.
    function test_passwordTextFieldDropsPastedLineEndingsOnlyForANewPassphrase_data() {
        return [
            { tag: "lf", pasted: "Correct-Horse-7\n", dropped: "Correct-Horse-7" },
            { tag: "crlf", pasted: "Correct-Horse-7\r\n", dropped: "Correct-Horse-7" },
            { tag: "cr", pasted: "Correct-Horse-7\r", dropped: "Correct-Horse-7" },
            { tag: "leading", pasted: "\nCorrect-Horse-7", dropped: "Correct-Horse-7" },
            { tag: "inner", pasted: "Correct\nHorse-7", dropped: "CorrectHorse-7" }
        ]
    }
    function test_passwordTextFieldDropsPastedLineEndingsOnlyForANewPassphrase(data) {
        const control = createTemporaryObject(passwordTextFieldComponent, host)
        const field = findChild(control, "examplePasswordTextFieldInput")
        field.insert(0, data.pasted)
        compare(field.text, data.pasted)

        control.dropsLineEndings = true
        compare(field.text, data.dropped)
        field.text = ""
        field.insert(0, "x")
        field.insert(1, data.pasted)
        compare(field.text, "x" + data.dropped)
        compare(field.cursorPosition, field.text.length)
        // Spaces and tabs are not line endings and stay.
        field.text = ""
        field.insert(0, "a b\tc")
        compare(field.text, "a b\tc")
    }

    function test_formRowAcceptsFullWidthBodyContent() {
        const row = createTemporaryObject(bodyRowComponent, host)
        verify(row !== null)
        verify(row.loadedBodyItem !== null)
        compare(row.loadedBodyItem.objectName, "exampleBodyContent")
        compare(row.loadedBodyItem.text, "/Users/example/Bitcoin")
        verify(row.implicitHeight > row.minimumRowHeight)
    }

    property var formHarnesses: []
    property bool formClipboardOwned: false
    property string formClipboardBefore: ""

    Component {
        id: formFieldHarnessComponent
        FocusScope {
            id: frame
            property alias control: control
            property alias focusSink: focusSink
            property int copyCalls: 0
            property int actionCalls: 0
            property int editCalls: 0
            property int acceptedCalls: 0
            property string lastCopiedText: ""
            property string lastActionText: ""
            property string lastEditedText: ""
            x: 16
            y: 16
            width: Math.max(0, Math.min(500, testCase.Window.window.contentItem.width - 32))
            height: focusSink.y + focusSink.height
            LabeledTextField {
                id: control
                objectName: "r10FormField"
                width: frame.width
                height: implicitHeight
                label: "Original label"
                supportingText: "Original supporting text"
                text: "Original text"
                fieldSurface: true
                showCopyButton: true
                actionText: "Inspect"
                onCopyRequested: {
                    frame.copyCalls++
                    frame.lastCopiedText = text
                    Clipboard.setText(text)
                }
                onActionRequested: { frame.actionCalls++; frame.lastActionText = text }
                onTextEdited: (text) => { frame.editCalls++; frame.lastEditedText = text }
                onAccepted: frame.acceptedCalls++
            }
            NeutralButton {
                id: focusSink
                objectName: "r10FormFieldFocusSink"
                y: control.height + 16
                width: 200
                height: 40
                focusPolicy: Qt.StrongFocus
                text: "Other control"
            }
        }
    }
    Component {
        id: formViewHarnessComponent
        FocusScope {
            id: frame
            property alias control: control
            property alias focusSink: focusSink
            property int copyCalls: 0
            property int actionCalls: 0
            property int editCalls: 0
            property string lastCopiedText: ""
            property string lastActionText: ""
            property string lastEditedText: ""
            x: 16
            y: 16
            width: Math.max(0, Math.min(500, testCase.Window.window.contentItem.width - 32))
            height: focusSink.y + focusSink.height
            LabeledTextView {
                id: control
                objectName: "r10FormView"
                width: frame.width
                height: implicitHeight
                fieldHeight: 90
                label: "Original notes label"
                supportingText: "Original supporting text"
                text: "Original text"
                showCopyButton: true
                actionText: "Inspect"
                onCopyRequested: {
                    frame.copyCalls++
                    frame.lastCopiedText = text
                    Clipboard.setText(text)
                }
                onActionRequested: { frame.actionCalls++; frame.lastActionText = text }
                onTextEdited: (text) => { frame.editCalls++; frame.lastEditedText = text }
            }
            NeutralButton {
                id: focusSink
                objectName: "r10FormViewFocusSink"
                y: control.height + 16
                width: 200
                height: 40
                focusPolicy: Qt.StrongFocus
                text: "Other control"
            }
        }
    }

    function cleanup() {
        for (let i = 0; i < formHarnesses.length; ++i) {
            const receipt = testSetup.watchDestruction(formHarnesses[i])
            formHarnesses[i].destroy()
            tryCompare(receipt, "pending", 0)
            testSetup.releaseDestructionWatch(receipt)
        }
        formHarnesses = []
        if (formClipboardOwned) Clipboard.currentText = formClipboardBefore
        formClipboardOwned = false
        Theme.motion.mode = motionMode
    }

    function clickFormControl(item, expectedEnabled) {
        verify(item !== null && item.visible)
        compare(item.enabled, expectedEnabled === undefined ? true : expectedEnabled)
        const window = item.Window.window
        verify(window !== null)
        compare(window, testCase.Window.window)
        window.requestActivate()
        tryCompare(window, "active", true)
        window.update()
        verify(waitForRendering(item))
        verify(item.width > 0 && item.height > 0)
        for (let ancestor = item; ancestor; ancestor = ancestor.parent) {
            verify(ancestor.visible)
            if (!ancestor.clip) continue
            const point = item.mapToItem(ancestor, item.width / 2, item.height / 2)
            verify(point.x >= 0 && point.x < ancestor.width)
            verify(point.y >= 0 && point.y < ancestor.height)
        }
        const point = item.mapToItem(window.contentItem, item.width / 2, item.height / 2)
        verify(point.x >= 0 && point.x < window.contentItem.width)
        verify(point.y >= 0 && point.y < window.contentItem.height)
        mouseClick(item, item.width / 2, item.height / 2)
    }

    function createFormHarness(component) {
        if (!formClipboardOwned) {
            formClipboardBefore = Clipboard.currentText
            formClipboardOwned = true
        }
        Clipboard.currentText = "Owned by another control"
        const frame = createTemporaryObject(component, testCase.Window.window.contentItem)
        verify(frame !== null)
        formHarnesses.push(frame)
        verify(frame.width >= 200)
        frame.visible = true
        clickFormControl(frame.focusSink)
        tryCompare(frame.focusSink, "activeFocus", true)
        return frame
    }

    function test_field_surface_tracks_focus_and_return_intent() {
        const frame = createFormHarness(formFieldHarnessComponent)
        const control = frame.control
        const field = control.field
        const surface = findChild(control, "r10FormFieldSurface")
        const border = findChild(control, "r10FormFieldFieldBorder")
        verify(surface !== null && border !== null)
        compare(surface.visible, true)
        compare(surface.radius, control.fieldCornerRadius)
        compare(control.fieldFocused, false)
        compare(control.showFieldBorder, false)
        compare(border.visible, false)
        const edits = frame.editCalls
        const clipboard = Clipboard.revision
        clickFormControl(field)
        tryCompare(field, "activeFocus", true)
        compare(control.fieldFocused, true)
        compare(control.showFieldBorder, true)
        compare(border.visible, true)
        compare(border.border.color, Qt.rgba(control.focusBorderColor.r, control.focusBorderColor.g, control.focusBorderColor.b, 0.5))
        // A surface field shows one focus border, not a second one on the input.
        const fieldRing = findChild(field, field.objectName + "FocusRing")
        verify(fieldRing === null || !fieldRing.visible)
        keyClick(Qt.Key_Return)
        tryCompare(field, "activeFocus", false)
        compare(frame.acceptedCalls, 1)
        compare(control.showFieldBorder, false)
        compare(border.visible, false)
        compare(control.text, "Original text")
        compare(frame.editCalls, edits)
        compare(frame.copyCalls, 0)
        compare(frame.actionCalls, 0)
        compare(Clipboard.revision, clipboard)
    }

    function test_field_surface_error_outlives_focus_and_clears() {
        const frame = createFormHarness(formFieldHarnessComponent)
        const control = frame.control
        const field = control.field
        const border = findChild(control, "r10FormFieldFieldBorder")
        const message = findChild(control, "r10FormFieldMessage")
        const edits = frame.editCalls
        const clipboard = Clipboard.revision
        control.errorText = "The caller rejected this value."
        compare(control.showFieldBorder, true)
        compare(control.activeBorderColor, control.errorBorderColor)
        compare(border.border.color, Qt.rgba(control.errorBorderColor.r, control.errorBorderColor.g, control.errorBorderColor.b, 0.5))
        compare(message.text, control.errorText)
        tryCompare(message, "color", control.errorTextColor)
        compare(field.Accessible.description, control.errorText)
        clickFormControl(field)
        tryCompare(field, "activeFocus", true)
        field.cursorPosition = field.text.length
        keyClick("x")
        compare(frame.editCalls, edits + 1)
        compare(frame.lastEditedText, "Original textx")
        compare(control.errorText, "The caller rejected this value.")
        clickFormControl(frame.focusSink)
        tryCompare(field, "activeFocus", false)
        compare(control.showFieldBorder, true)
        compare(control.activeBorderColor, control.errorBorderColor)
        control.errorText = ""
        compare(control.showFieldBorder, false)
        compare(border.visible, false)
        compare(message.text, "Original supporting text")
        tryCompare(message, "color", Theme.color.neutral7)
        compare(field.Accessible.description, "Original supporting text")
        compare(frame.copyCalls, 0)
        compare(frame.actionCalls, 0)
        compare(Clipboard.revision, clipboard)
    }

    function test_text_view_focus_border_preserves_error_accessibility() {
        const frame = createFormHarness(formViewHarnessComponent)
        const control = frame.control
        const field = control.field
        const edits = frame.editCalls
        const clipboard = Clipboard.revision
        const ring = findChild(field, field.objectName + "FocusRing")
        verify(ring !== null)
        compare(ring.visible, false)
        clickFormControl(field)
        tryCompare(field, "activeFocus", true)
        compare(ring.visible, true)
        compare(ring.border.color, Qt.rgba(control.focusBorderColor.r, control.focusBorderColor.g, control.focusBorderColor.b, 0.3))
        field.cursorPosition = field.text.length
        keyClick("x")
        compare(frame.editCalls, edits + 1)
        compare(frame.lastEditedText, "Original textx")
        control.errorText = "These notes need review."
        compare(field.Accessible.name, "Original notes label")
        compare(field.Accessible.description, "These notes need review.")
        compare(ring.border.color, Qt.rgba(control.errorBorderColor.r, control.errorBorderColor.g, control.errorBorderColor.b, 0.3))
        clickFormControl(frame.focusSink)
        tryCompare(field, "activeFocus", false)
        compare(ring.visible, false)
        compare(field.Accessible.description, "These notes need review.")
        control.errorText = ""
        compare(field.Accessible.description, "Original supporting text")
        compare(control.text, "Original textx")
        compare(frame.copyCalls, 0)
        compare(frame.actionCalls, 0)
        compare(Clipboard.revision, clipboard)
    }

    function test_disabled_field_owner_blocks_all_intents_and_reenables() {
        const frame = createFormHarness(formFieldHarnessComponent)
        const control = frame.control
        const copy = findChild(control, "r10FormFieldCopyButton")
        const action = findChild(control, "r10FormFieldActionButton")
        const edits = frame.editCalls
        const clipboard = Clipboard.revision
        control.enabled = false
        compare(control.labelColor, Theme.color.neutral4)
        compare(control.supportingTextColor, Theme.color.neutral4)
        compare(control.field.color, Theme.color.neutral4)
        compare(control.field.placeholderTextColor, Theme.color.neutral4)
        clickFormControl(copy, false)
        clickFormControl(action, false)
        clickFormControl(control.field, false)
        keyClick("x")
        compare(control.text, "Original text")
        compare(frame.editCalls, edits)
        compare(frame.copyCalls, 0)
        compare(frame.actionCalls, 0)
        compare(Clipboard.revision, clipboard)
        control.enabled = true
        compare(control.labelColor, Theme.color.neutral8)
        compare(control.supportingTextColor, Theme.color.neutral7)
        compare(control.field.color, Theme.color.neutral9)
        compare(control.field.placeholderTextColor, Theme.color.neutral6)
        clickFormControl(copy)
        compare(frame.copyCalls, 1)
        compare(frame.lastCopiedText, "Original text")
        compare(Clipboard.currentText, "Original text")
        compare(Clipboard.revision, clipboard + 1)
        clickFormControl(action)
        compare(frame.actionCalls, 1)
        compare(frame.lastActionText, "Original text")
        compare(frame.copyCalls, 1)
        compare(Clipboard.revision, clipboard + 1)
    }

    function test_disabled_copy_retains_action_and_editor_intents() {
        const frame = createFormHarness(formFieldHarnessComponent)
        const control = frame.control
        const copy = findChild(control, "r10FormFieldCopyButton")
        const action = findChild(control, "r10FormFieldActionButton")
        const edits = frame.editCalls
        const clipboard = Clipboard.revision
        control.copyButtonEnabled = false
        clickFormControl(copy, false)
        compare(frame.copyCalls, 0)
        compare(Clipboard.revision, clipboard)
        clickFormControl(action)
        compare(frame.actionCalls, 1)
        compare(frame.lastActionText, "Original text")
        compare(control.text, "Original text")
        clickFormControl(control.field)
        tryCompare(control.field, "activeFocus", true)
        control.field.cursorPosition = control.field.text.length
        keyClick("x")
        compare(frame.editCalls, edits + 1)
        compare(frame.lastEditedText, "Original textx")
        compare(Clipboard.revision, clipboard)
        control.copyButtonEnabled = true
        clickFormControl(copy)
        compare(frame.copyCalls, 1)
        compare(frame.lastCopiedText, "Original textx")
        compare(Clipboard.currentText, "Original textx")
        compare(Clipboard.revision, clipboard + 1)
        compare(frame.actionCalls, 1)
    }

    function test_disabled_action_retains_copy_and_multiline_editor_intents() {
        const frame = createFormHarness(formViewHarnessComponent)
        const control = frame.control
        const copy = findChild(control, "r10FormViewCopyButton")
        const action = findChild(control, "r10FormViewActionButton")
        const edits = frame.editCalls
        const clipboard = Clipboard.revision
        control.enabled = false
        compare(control.field.color, Theme.color.neutral4)
        compare(control.field.placeholderTextColor, Theme.color.neutral4)
        control.actionEnabled = false
        control.enabled = true
        compare(control.field.color, Theme.color.neutral9)
        compare(control.field.placeholderTextColor, Theme.color.neutral6)
        clickFormControl(action, false)
        compare(frame.actionCalls, 0)
        clickFormControl(copy)
        compare(frame.copyCalls, 1)
        compare(frame.lastCopiedText, "Original text")
        compare(Clipboard.revision, clipboard + 1)
        clickFormControl(control.field)
        tryCompare(control.field, "activeFocus", true)
        control.field.cursorPosition = control.field.text.length
        keyClick("x")
        compare(frame.editCalls, edits + 1)
        compare(frame.lastEditedText, "Original textx")
        compare(Clipboard.currentText, "Original text")
        control.actionEnabled = true
        clickFormControl(action)
        compare(frame.actionCalls, 1)
        compare(frame.lastActionText, "Original textx")
        compare(frame.copyCalls, 1)
        compare(Clipboard.revision, clipboard + 1)
    }

    function test_copy_only_header_hides_and_reveals_without_losing_editor() {
        const frame = createFormHarness(formFieldHarnessComponent)
        const control = frame.control
        const field = control.field
        const copy = findChild(control, "r10FormFieldCopyButton")
        const header = copy.parent
        control.label = ""
        control.actionText = ""
        control.supportingText = ""
        control.showCopyButton = false
        verify(waitForRendering(control))
        compare(header.visible, false)
        const withoutHeader = control.implicitHeight
        const clipboard = Clipboard.revision
        control.showCopyButton = true
        tryVerify(function() { return control.implicitHeight > withoutHeader })
        compare(header.visible, true)
        clickFormControl(copy)
        compare(frame.copyCalls, 1)
        compare(Clipboard.currentText, "Original text")
        compare(Clipboard.revision, clipboard + 1)
        control.visible = false
        compare(header.visible, false)
        control.visible = true
        compare(header.visible, true)
        compare(control.field, field)
        compare(control.text, "Original text")
        compare(frame.copyCalls, 1)
        compare(frame.actionCalls, 0)
        compare(Clipboard.revision, clipboard + 1)
        control.showCopyButton = false
        compare(header.visible, false)
        tryCompare(control, "implicitHeight", withoutHeader)
    }

    function test_action_only_header_keeps_intent_separate_from_text() {
        const frame = createFormHarness(formViewHarnessComponent)
        const control = frame.control
        const action = findChild(control, "r10FormViewActionButton")
        const header = action.parent
        control.label = ""
        control.showCopyButton = false
        control.actionText = ""
        control.supportingText = ""
        compare(header.visible, false)
        const edits = frame.editCalls
        const clipboard = Clipboard.revision
        control.actionText = "Review note"
        compare(header.visible, true)
        clickFormControl(action)
        compare(frame.actionCalls, 1)
        compare(frame.lastActionText, "Original text")
        compare(frame.copyCalls, 0)
        compare(Clipboard.revision, clipboard)
        control.actionText = ""
        compare(header.visible, false)
        clickFormControl(control.field)
        tryCompare(control.field, "activeFocus", true)
        control.field.cursorPosition = control.field.text.length
        keyClick("x")
        compare(frame.editCalls, edits + 1)
        compare(frame.lastEditedText, "Original textx")
        compare(header.visible, false)
        compare(frame.actionCalls, 1)
        compare(frame.copyCalls, 0)
        compare(Clipboard.currentText, "Owned by another control")
        compare(Clipboard.revision, clipboard)
    }

    function test_field_custom_names_keep_editor_and_validation_owned() {
        const frame = createFormHarness(formFieldHarnessComponent)
        const control = frame.control
        const field = control.field
        const label = findChild(control, "r10FormFieldLabel")
        const message = findChild(control, "r10FormFieldMessage")
        const action = findChild(control, "r10FormFieldActionButton")
        const copy = findChild(control, "r10FormFieldCopyButton")
        control.labelObjectName = "callerFieldLabel"
        control.messageObjectName = "callerFieldMessage"
        control.actionObjectName = "callerFieldAction"
        control.copyButtonObjectName = "callerFieldCopy"
        control.fieldObjectName = "callerFieldInput"
        compare(findChild(control, "callerFieldLabel"), label)
        compare(findChild(control, "callerFieldMessage"), message)
        compare(findChild(control, "callerFieldAction"), action)
        compare(findChild(control, "callerFieldCopy"), copy)
        compare(findChild(control, "callerFieldInput"), field)
        compare(findChild(control, "r10FormFieldField"), null)
        control.label = "Caller-owned label"
        control.errorText = "The caller still owns validation."
        compare(label.text, "Caller-owned label")
        compare(message.text, control.errorText)
        compare(field.Accessible.name, "Caller-owned label")
        compare(field.Accessible.description, control.errorText)
        const edits = frame.editCalls
        const clipboard = Clipboard.revision
        clickFormControl(field)
        field.cursorPosition = field.text.length
        keyClick("x")
        compare(frame.editCalls, edits + 1)
        compare(frame.lastEditedText, "Original textx")
        clickFormControl(action)
        compare(frame.actionCalls, 1)
        compare(frame.lastActionText, "Original textx")
        clickFormControl(copy)
        compare(frame.copyCalls, 1)
        compare(Clipboard.currentText, "Original textx")
        compare(Clipboard.revision, clipboard + 1)
        control.objectName = ""
        compare(field.objectName, "callerFieldInput")
        compare(label.objectName, "callerFieldLabel")
        control.fieldObjectName = ""
        compare(field.objectName, "")
        control.labelObjectName = ""
        control.messageObjectName = ""
        control.actionObjectName = ""
        control.copyButtonObjectName = ""
        compare(label.objectName, "")
        compare(message.objectName, "")
        compare(action.objectName, "")
        compare(copy.objectName, "")
        control.objectName = "renamedFieldOwner"
        compare(field.objectName, "renamedFieldOwnerField")
        compare(label.objectName, "renamedFieldOwnerLabel")
        compare(message.objectName, "renamedFieldOwnerMessage")
        compare(action.objectName, "renamedFieldOwnerActionButton")
        compare(copy.objectName, "renamedFieldOwnerCopyButton")
        compare(control.field, field)
        compare(control.text, "Original textx")
        compare(control.errorText, "The caller still owns validation.")
    }

    function test_text_view_custom_and_fallback_names_preserve_selection_and_edits() {
        const frame = createFormHarness(formViewHarnessComponent)
        const control = frame.control
        const field = control.field
        control.fieldObjectName = "callerNotesInput"
        control.labelObjectName = "callerNotesLabel"
        control.messageObjectName = "callerNotesMessage"
        compare(findChild(control, "callerNotesInput"), field)
        compare(findChild(control, "r10FormViewField"), null)
        control.objectName = ""
        compare(field.objectName, "callerNotesInput")
        control.fieldObjectName = ""
        compare(field.objectName, "")
        control.objectName = "renamedViewOwner"
        compare(field.objectName, "renamedViewOwnerField")
        compare(control.field, field)
        compare(findChild(control, "callerNotesLabel").text, "Original notes label")
        control.errorText = "The caller must review these notes."
        compare(field.Accessible.description, control.errorText)
        compare(findChild(control, "callerNotesMessage").text, control.errorText)
        const edits = frame.editCalls
        const clipboard = Clipboard.revision
        clickFormControl(field)
        tryCompare(field, "activeFocus", true)
        field.selectAll()
        compare(field.selectedText, "Original text")
        keyClick("n")
        compare(control.text, "n")
        compare(frame.editCalls, edits + 1)
        compare(frame.lastEditedText, "n")
        compare(field.selectedText, "")
        compare(field.cursorPosition, 1)
        compare(field.Accessible.description, "The caller must review these notes.")
        control.errorText = ""
        compare(field.Accessible.description, "Original supporting text")
        clickFormControl(findChild(control, "renamedViewOwnerCopyButton"))
        compare(frame.copyCalls, 1)
        compare(frame.lastCopiedText, "n")
        compare(frame.actionCalls, 0)
        compare(Clipboard.currentText, "n")
        compare(Clipboard.revision, clipboard + 1)
    }

}
