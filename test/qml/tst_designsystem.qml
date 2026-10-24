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
