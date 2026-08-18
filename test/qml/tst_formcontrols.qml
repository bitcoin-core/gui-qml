// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15
import QtTest 1.2
import "../../qml/controls"
import "../../qml/pages/settings"

TestCase {
    id: testCase
    name: "FormControls"
    when: windowShown
    width: 640
    height: 640

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
        id: designSystemPageComponent

        SettingsDesignSystem {
            width: 600
            height: 900
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

        row.enabled = false
        compare(row.loadedTrailingItem.enabled, false)
        tryCompare(findChild(row, "exampleRowTitle"), "color", Theme.color.neutral4)
    }

    function test_formSectionOwnsCardAndContent() {
        const section = createTemporaryObject(formSectionComponent, host)
        verify(section !== null)
        const card = findChild(section, "exampleSectionCard")
        verify(card !== null)
        compare(card.color, Theme.color.neutral1)
        compare(card.border.width, 0)
        compare(card.radius, 16)
        const footer = findChild(section, "exampleSectionFooter")
        verify(footer !== null)
        compare(footer.text, "Changes apply immediately.")
        compare(footer.font.pixelSize, Theme.text.caption.font.pixelSize)
        verify(section.implicitHeight > 0)
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
        compare(button.defaultBgColor, Theme.color.background)
        compare(button.hoverBgColor, Theme.color.neutral2)
        compare(menu.backgroundColor, Theme.color.neutral1)

        picker.embedded = true
        compare(button.defaultBgColor, Theme.color.neutral2)
        compare(button.hoverBgColor, Theme.color.neutral3)
        compare(menu.backgroundColor, Theme.color.neutral2)

        picker.currentValue = "dark"
        compare(picker.currentText, "Dark")

        let activatedValue = ""
        picker.activated.connect(function(value) { activatedValue = value })
        tryVerify(function() { return picker.itemAtIndex(0) !== null })
        picker.itemAtIndex(0).triggered()

        compare(activatedValue, "light")
        compare(picker.currentValue, "dark")
    }

    function test_designSystemPageShowsGenericControlExamples() {
        const page = createTemporaryObject(designSystemPageComponent, host)
        verify(page !== null)
        verify(findChild(page, "settingsPageContentLayout") !== null)
        verify(findChild(page, "designSystemAppearanceSection") !== null)
        verify(findChild(page, "designSystemThemeRow") !== null)
        verify(findChild(page, "designSystemLanguagePicker") !== null)
        verify(findChild(page, "designSystemBehaviorSection") !== null)
        verify(findChild(page, "designSystemNavigationSection") !== null)
        verify(findChild(page, "designSystemValueSection") !== null)
        verify(findChild(page, "designSystemWebsiteRow") !== null)
        verify(findChild(page, "designSystemVersionRow") !== null)
        verify(findChild(page, "designSystemFieldSection") !== null)
        verify(findChild(page, "designSystemBlockStorageField") !== null)
        verify(findChild(page, "designSystemProxyLocationField") !== null)
        verify(findChild(page, "designSystemDataDirectoryValue") !== null)
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

    function test_valueRowDisplaysCallerOwnedValue() {
        const row = createTemporaryObject(valueRowComponent, host)
        verify(row !== null)
        const value = findChild(row, "exampleValueRowValue")
        verify(value !== null)
        compare(value.text, "v31.99.0-unk")

        row.value = "v32.0"
        compare(value.text, "v32.0")
    }

    function test_pageHeadingCentersProminentDescription() {
        const heading = createTemporaryObject(pageHeadingComponent, host)
        verify(heading !== null)
        const description = findChild(heading, "exampleHeadingDescription")
        verify(description !== null)
        compare(heading.descriptionTextStyle.font.pixelSize, Theme.text.description.font.pixelSize)
        compare(description.font.pixelSize, Theme.text.description.font.pixelSize)
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
        compare(row.field, row.loadedTrailingItem)
        compare(row.loadedBodyItem, null)
        compare(row.text, "127.0.0.1:9050")
        compare(row.field.horizontalAlignment, Text.AlignRight)
        compare(row.focusBorderColor, Theme.color.orange)
        compare(row.field.background.border.color, Theme.color.orange)

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

    function test_formRowAcceptsFullWidthBodyContent() {
        const row = createTemporaryObject(bodyRowComponent, host)
        verify(row !== null)
        verify(row.loadedBodyItem !== null)
        compare(row.loadedBodyItem.objectName, "exampleBodyContent")
        compare(row.loadedBodyItem.text, "/Users/example/Bitcoin")
        verify(row.implicitHeight > row.minimumRowHeight)
    }
}
