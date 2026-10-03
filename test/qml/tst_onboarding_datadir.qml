// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/components"
import "../../qml/controls"
import "../../qml/pages/onboarding"

TestCase {
    name: "OnboardingDataDir"
    when: windowShown
    visible: true
    width: 640
    height: 665

    Component {
        id: fullWizard
        OnboardingWizard {
            width: 640
            height: 665
            assumedChainstateSize: 8
        }
    }

    Component {
        id: fullPreInitWizard
        OnboardingWizard {
            width: 640
            height: 665
            preInit: true
            assumedChainstateSize: 8
        }
    }

    Component {
        id: storageLocation
        OnboardingStorageLocation {
            width: 640
            height: 665
            assumedChainstateSize: 8
        }
    }

    Component {
        id: storageAmount
        OnboardingStorageAmount {
            width: 640
            height: 665
            assumedBlockchainSize: 610
            assumedChainstateSize: 12
        }
    }

    Component {
        id: storageSettings
        StorageSettings {
            width: 640
            settingsModel: optionsModel
        }
    }

    Component {
        id: storageOptions
        StorageOptions {
            width: 640
            settingsModel: optionsModel
            assumedBlockchainSize: 610
            assumedChainstateSize: 12
        }
    }

    Component {
        id: storageLocations
        StorageLocations {
            width: 640
            settingsModel: optionsModel
            minimumStorageRequiredGB: 10
        }
    }

    Component {
        id: connection
        OnboardingConnection {
            width: 640
            height: 665
        }
    }

    function init() {
        optionsModel.clearCoreSettingStatusesForTest()
        optionsModel.existingProfile = false
        optionsModel.prune = true
        optionsModel.pruneSizeGB = 2
        optionsModel.setStorageStatusForTest(false, 123, "", "")
    }

    function triggerButton(rootItem, objectName) {
        const button = findChild(rootItem, objectName)
        verify(button !== null)
        button.clicked()
    }

    function findButtonByText(rootItem, text) {
        if (!rootItem) return null
        if (rootItem.text === text) return rootItem
        for (let i = 0; i < rootItem.children.length; ++i) {
            const found = findButtonByText(rootItem.children[i], text)
            if (found) return found
        }
        return null
    }

    function openStorageSettings(page) {
        mouseClick(findChild(page, "onboardingStorageSettingsButton"))
        const popup = findChild(page, "onboardingStorageSettingsPopup")
        tryCompare(popup, "opened", true)
        return popup
    }

    function editStorageTarget(popup, target) {
        const input = findChild(popup, "pruneTargetInput")
        mouseClick(input)
        verify(input.activeFocus)
        input.selectAll()
        keyClick(Qt.Key_Backspace)
        for (let i = 0; i < target.length; ++i) keyClick(target.charAt(i))
        mouseClick(findChild(popup, "onboardingStorageSettingsCloseButton"))
        tryCompare(popup, "visible", false)
    }

    function test_storage_popup_commits_latest_custom_target_after_selecting_reduce() {
        const page = createTemporaryObject(storageAmount, this)
        verify(page !== null)
        const popup = openStorageSettings(page)
        editStorageTarget(popup, "7")
        compare(optionsModel.pruneSizeGB, 7)
        compare(page.customStorage, true)
        compare(page.customStorageAmount, 7)

        mouseClick(findChild(page, "storageReduceOption"))
        compare(optionsModel.pruneSizeGB, 2)
        openStorageSettings(page)
        editStorageTarget(popup, "10")
        compare(optionsModel.pruneSizeGB, 10)
        compare(page.customStorageAmount, 10)
        const custom = findChild(page, "storageCustomOption")
        compare(custom.description, "Storing recent blocks up to 10 GB.")
        compare(custom.checked, true)

        mouseClick(findChild(page, "storageReduceOption"))
        compare(optionsModel.pruneSizeGB, 2)
        mouseClick(custom)
        compare(optionsModel.pruneSizeGB, 10)
    }

    function test_storage_popup_reopens_with_current_target_and_no_stale_error() {
        const page = createTemporaryObject(storageAmount, this)
        verify(page !== null)
        const popup = openStorageSettings(page)
        editStorageTarget(popup, "7")
        compare(optionsModel.pruneSizeGB, 7)
        mouseClick(findChild(page, "storageReduceOption"))
        compare(optionsModel.pruneSizeGB, 2)
        openStorageSettings(page)
        compare(findChild(popup, "pruneTargetInput").text, "2")

        editStorageTarget(popup, "200")
        compare(optionsModel.pruneSizeGB, 2)
        compare(page.customStorageAmount, 7)
        const settings = findChild(popup, "storageSettingsPage")
        verify(settings.pruneTargetError.length > 0)
        openStorageSettings(page)
        compare(findChild(popup, "pruneTargetInput").text, "2")
        compare(settings.pruneTargetError, "")
        popup.close()
        tryCompare(popup, "visible", false)
    }

    function test_preinit_onboarding_starts_at_cover() {
        const wizard = createTemporaryObject(fullPreInitWizard, this)
        verify(wizard !== null)
        compare(wizard.currentItem.objectName, "onboardingCover")
    }

    function test_preinit_cover_info_button_opens_about() {
        const wizard = createTemporaryObject(fullPreInitWizard, this)
        verify(wizard !== null)
        compare(wizard.currentItem.objectName, "onboardingCover")

        const infoButton = findChild(wizard.currentItem, "onboardingCoverInfoButton")
        verify(infoButton !== null)
        compare(infoButton.textColor, Theme.color.orange)

        infoButton.clicked()
        const popup = findChild(wizard.currentItem, "onboardingAboutPopup")
        verify(popup !== null)
        tryCompare(popup, "visible", true)
        compare(popup.width, 532)
        compare(popup.height, 504)
        const aboutPage = findChild(popup, "aboutSettingsPage")
        verify(aboutPage !== null)
        compare(aboutPage.title, "About Bitcoin Core")
        compare(findChild(aboutPage, "aboutVersionRow").dividerColor, Theme.color.neutral3)

        findChild(aboutPage, "aboutDeveloperRow").clicked()
        const developerPage = findChild(popup, "settingsDeveloper")
        verify(developerPage !== null)
        const developerSection = findChild(developerPage, "developerSettingsSection")
        verify(developerSection !== null)
        compare(developerSection.backgroundColor, Theme.color.neutral2)
        const dbcacheField = findChild(developerPage, "developerDbcacheField")
        const scriptThreadsField = findChild(developerPage, "developerScriptThreadsField")
        verify(dbcacheField !== null)
        verify(scriptThreadsField !== null)
        compare(dbcacheField.text, String(optionsModel.dbcacheSizeMiB))
        compare(dbcacheField.validator.bottom, optionsModel.minDbcacheSizeMiB)
        compare(dbcacheField.validator.top, optionsModel.maxDbcacheSizeMiB)
        compare(scriptThreadsField.text, String(optionsModel.scriptThreads))
        compare(scriptThreadsField.validator.bottom, optionsModel.minScriptThreads)
        compare(scriptThreadsField.validator.top, optionsModel.maxScriptThreads)
    }

    function test_normal_onboarding_starts_at_cover() {
        const wizard = createTemporaryObject(fullWizard, this)
        verify(wizard !== null)
        compare(wizard.currentItem.objectName, "onboardingCover")
    }

    function test_cover_footer_appears_after_content() {
        const wizard = createTemporaryObject(fullWizard, this)
        verify(wizard !== null)
        const footer = wizard.currentItem.footer
        compare(footer.opacity, 0)
        compare(footer.enabled, false)
        wait(1800)
        compare(footer.opacity, 0)
        tryCompare(footer, "opacity", 1, 2000)
        compare(footer.enabled, true)
    }

    function test_full_preinit_wizard_can_go_back_from_storage_amount() {
        const wizard = createTemporaryObject(fullPreInitWizard, this)
        verify(wizard !== null)
        compare(wizard.currentItem.objectName, "onboardingCover")
        compare(findChild(wizard.currentItem, "onboardingWizardBackButton").visible, false)
        compare(wizard.currentItem.footer.opacity, 0)
        const separator = findChild(wizard.currentItem, "onboardingFooterSeparator")
        const actions = findChild(wizard.currentItem, "onboardingFooterActions")
        verify(separator !== null)
        verify(actions !== null)
        compare(separator.width, wizard.width)
        compare(actions.width, wizard.width - 80)
        wizard.width = 1100
        compare(separator.width, 1100)
        compare(actions.width, 1020)

        triggerButton(wizard.currentItem, "onboardingCoverButton")
        tryVerify(function() { return wizard.currentItem.objectName === "onboardingStrengthen" })
        compare(wizard.currentItem.footer.opacity, 1)
        compare(wizard.busy, false)
        compare(wizard.currentItem.showBackButton, true)
        compare(wizard.currentItem.backButtonInFooter, true)
        const backButton = findChild(wizard.currentItem, "onboardingWizardBackButton")
        verify(backButton !== null)
        compare(backButton, wizard.currentItem.footerBackButton)
        tryCompare(backButton, "opacity", 1)
        compare(backButton.isOnSurface, false)
        const backButtonX = backButton.x
        const backButtonY = backButton.y

        triggerButton(wizard.currentItem, "onboardingStrengthenButton")
        tryVerify(function() { return wizard.currentItem.objectName === "onboardingBlockchain" })
        compare(wizard.currentItem.footer.opacity, 1)

        triggerButton(wizard.currentItem, "onboardingBlockchainButton")
        tryVerify(function() { return wizard.currentItem.objectName === "onboardingBlockclock" })
        compare(wizard.currentItem.footer.opacity, 1)

        triggerButton(wizard.currentItem, "onboardingBlockclockButton")
        tryVerify(function() { return wizard.currentItem.objectName === "onboardingStorageLocation" })

        triggerButton(wizard.currentItem, "onboardingStorageLocationButton")
        tryVerify(function() { return wizard.currentItem.objectName === "onboardingStorageAmount" })
        const storageBackButton = findChild(wizard.currentItem, "onboardingWizardBackButton")
        verify(storageBackButton !== null)
        compare(storageBackButton.x, backButtonX)
        compare(storageBackButton.y, backButtonY)

        storageBackButton.clicked()
        tryVerify(function() { return wizard.currentItem.objectName === "onboardingStorageLocation" })
        compare(wizard.busy, false)
    }

    function test_storage_location_uses_injected_chainstate_size() {
        const page = createTemporaryObject(storageLocation, this)
        verify(page !== null)
        verify(page.subheading.indexOf("10GB") !== -1)
    }

    function test_existing_profile_storage_location_uses_operational_minimum() {
        optionsModel.existingProfile = true
        optionsModel.setStorageStatusForTest(false, 1, "", "")

        const page = createTemporaryObject(storageLocation, this)
        verify(page !== null)
        const defaultOption = findChild(page, "storageDefaultLocationOption")
        verify(defaultOption !== null)

        compare(page.subheading, "Where do you want to store the downloaded block data? You need a minimum of 1GB of storage.")
        tryCompare(page, "primaryButtonEnabled", true)
        compare(defaultOption.showErrorText, false)
    }

    function test_connection_final_button_labels_start_commit_point() {
        const page = createTemporaryObject(connection, this)
        verify(page !== null)
        const button = findChild(page, "onboardingConnectionButton")
        verify(button !== null)
        compare(button.text, "Start")
    }

    function test_onboarding_settings_open_in_modals() {
        const amountPage = createTemporaryObject(storageAmount, this)
        verify(amountPage !== null)
        findChild(amountPage, "onboardingStorageSettingsButton").clicked()
        compare(findChild(amountPage, "onboardingStorageSettingsButton").textColor, Theme.color.orange)
        const storagePopup = findChild(amountPage, "onboardingStorageSettingsPopup")
        verify(storagePopup !== null)
        tryCompare(storagePopup, "visible", true)
        const storagePage = findChild(storagePopup, "storageSettingsPage")
        verify(storagePage !== null)
        compare(storagePage.onboarding, true)
        verify(findChild(storagePage, "pruneSwitch") !== null)
        storagePopup.close()

        const connectionPage = createTemporaryObject(connection, this)
        verify(connectionPage !== null)
        const connectionSettingsButton = findChild(connectionPage, "connectionSettingsButton")
        verify(connectionSettingsButton.width < connectionPage.width)
        compare(connectionSettingsButton.textColor, Theme.color.orange)
        connectionSettingsButton.clicked()
        const connectionPopup = findChild(connectionPage, "onboardingConnectionSettingsPopup")
        verify(connectionPopup !== null)
        tryCompare(connectionPopup, "visible", true)
        verify(findChild(connectionPopup, "connectionSettingsPage") !== null)
        findChild(connectionPopup, "proxySettingsRow").clicked()
        tryVerify(function() { return findChild(connectionPopup, "proxySettingsPage") !== null })
    }

    function test_storage_location_uses_folder_dialog_for_custom_directory() {
        const page = createTemporaryObject(storageLocation, this)
        verify(page !== null)
        const dialog = findChild(page, "customDataDirFolderDialog")
        verify(dialog !== null)
        verify(dialog.selectedFolder !== undefined)
        compare(dialog.fileMode, AppFileDialog.Directory)

        dialog.selectAndAccept("file:///tmp/first-datadir")
        compare(optionsModel.dataDir, "file:///tmp/first-datadir")
        dialog.selectAndAccept("file:///tmp/second-datadir")
        compare(optionsModel.dataDir, "file:///tmp/second-datadir")
    }

    function test_storage_location_option_bindings_survive_selection_clicks() {
        optionsModel.useDefaultDataDir()

        const page = createTemporaryObject(storageLocations, this)
        verify(page !== null)

        const defaultOption = findButtonByText(page, "Default")
        const customOption = findButtonByText(page, "Custom")
        verify(defaultOption !== null)
        verify(customOption !== null)

        compare(defaultOption.checked, true)
        compare(customOption.checked, false)
        compare(customOption.customDir, "")

        defaultOption.clicked()
        wait(0)

        optionsModel.selectCustomDataDir("/tmp/probe-custom")
        wait(0)
        compare(defaultOption.checked, false)
        compare(customOption.checked, true)
        compare(customOption.customDir, "/tmp/probe-custom")

        optionsModel.useDefaultDataDir()
        wait(0)
        compare(defaultOption.checked, true)
        compare(customOption.checked, false)
        compare(customOption.customDir, "")
    }

    function test_storage_location_shows_default_location_insufficient_storage_error() {
        optionsModel.useDefaultDataDir()
        optionsModel.setStorageStatusForTest(false, 8, "", "")

        const page = createTemporaryObject(storageLocations, this)
        verify(page !== null)

        const defaultOption = findChild(page, "storageDefaultLocationOption")
        const customOption = findChild(page, "storageCustomLocationOption")
        verify(defaultOption !== null)
        verify(customOption !== null)

        compare(defaultOption.checked, true)
        compare(defaultOption.description, "Your application directory.\n8GB available.")
        compare(defaultOption.showErrorText, true)
        compare(defaultOption.errorText, "Not enough storage available.")
        compare(customOption.showErrorText, false)
        compare(page.validSelection, false)
    }

    function test_storage_location_shows_custom_location_insufficient_storage_error() {
        optionsModel.selectCustomDataDir("/tmp/probe-custom")
        optionsModel.setStorageStatusForTest(false, 8, "", "")

        const page = createTemporaryObject(storageLocations, this)
        verify(page !== null)

        const defaultOption = findChild(page, "storageDefaultLocationOption")
        const customOption = findChild(page, "storageCustomLocationOption")
        verify(defaultOption !== null)
        verify(customOption !== null)

        compare(defaultOption.checked, false)
        compare(defaultOption.showErrorText, false)
        compare(customOption.checked, true)
        compare(customOption.description, "Choose the directory and storage device.\n8GB available.")
        compare(customOption.customDir, "/tmp/probe-custom")
        compare(customOption.showErrorText, true)
        compare(customOption.errorText, "Not enough storage available.")
        compare(page.validSelection, false)
    }

    function test_storage_location_uses_minimum_storage_for_selection_validity() {
        optionsModel.useDefaultDataDir()
        optionsModel.setStorageStatusForTest(false, 12, "", "")

        const page = createTemporaryObject(storageLocations, this)
        verify(page !== null)

        const defaultOption = findChild(page, "storageDefaultLocationOption")
        verify(defaultOption !== null)

        compare(defaultOption.description, "Your application directory.\n12GB available.")
        compare(defaultOption.showErrorText, false)
        compare(page.validSelection, true)
        compare(optionsModel.storageEnoughForSelected, false)
    }

    function test_storage_location_pending_check_disables_without_stale_error() {
        optionsModel.useDefaultDataDir()
        optionsModel.setStorageStatusForTest(true, 8, "", "")

        const page = createTemporaryObject(storageLocations, this)
        verify(page !== null)

        const defaultOption = findChild(page, "storageDefaultLocationOption")
        verify(defaultOption !== null)

        compare(defaultOption.description, "Your application directory.")
        compare(defaultOption.showErrorText, false)
        compare(defaultOption.errorText, "")
        compare(page.validSelection, false)
    }

    function test_storage_location_page_disables_next_when_location_below_minimum() {
        optionsModel.useDefaultDataDir()
        optionsModel.setStorageStatusForTest(false, 8, "", "")

        const page = createTemporaryObject(storageLocation, this)
        verify(page !== null)

        const button = findChild(page, "onboardingStorageLocationButton")
        const defaultOption = findChild(page, "storageDefaultLocationOption")
        verify(button !== null)
        verify(defaultOption !== null)

        compare(page.subheading, "Where do you want to store the downloaded block data? You need a minimum of 10GB of storage.")
        compare(button.enabled, false)
        compare(defaultOption.showErrorText, true)
        compare(defaultOption.errorText, "Not enough storage available.")
    }

    function test_storage_amount_uses_detected_available_space() {
        const page = createTemporaryObject(storageAmount, this)
        verify(page !== null)
        compare(page.heading, "Storage amount")
        compare(page.subheading, "Data retrieved from the Bitcoin network is stored on your device. You have 123GB of storage available.")
        verify(page.subheading.indexOf("500GB") === -1)
        compare(page.storageWarningText, "")
    }

    function test_storage_amount_disables_full_storage_when_space_is_insufficient() {
        const page = createTemporaryObject(storageAmount, this)
        verify(page !== null)
        const reduceOption = findChild(page, "storageReduceOption")
        const fullOption = findChild(page, "storageFullOption")
        verify(reduceOption !== null)
        verify(fullOption !== null)
        compare(reduceOption.enabled, true)
        compare(fullOption.enabled, false)
        compare(reduceOption.description, "Uses about 14GB. For regular wallet use.")
        compare(fullOption.description, "Uses about 622GB. Support the network.")
    }

    function test_existing_profile_storage_amount_keeps_full_storage_available() {
        optionsModel.existingProfile = true
        optionsModel.prune = false
        optionsModel.setStorageStatusForTest(false, 5, "", "")

        const page = createTemporaryObject(storageAmount, this)
        verify(page !== null)
        const infoPage = page
        const reduceOption = findChild(page, "storageReduceOption")
        const fullOption = findChild(page, "storageFullOption")
        verify(infoPage !== null)
        verify(reduceOption !== null)
        verify(fullOption !== null)

        compare(reduceOption.enabled, true)
        tryCompare(fullOption, "enabled", true)
        compare(fullOption.checked, true)
        tryCompare(infoPage, "primaryButtonEnabled", true)
    }

    function test_disabled_full_storage_does_not_hover() {
        const page = createTemporaryObject(storageAmount, this)
        verify(page !== null)
        const fullOption = findChild(page, "storageFullOption")
        verify(fullOption !== null)
        compare(fullOption.enabled, false)

        const initialBorderColor = fullOption.background.border.color.toString()
        mouseMove(fullOption, fullOption.width / 2, fullOption.height / 2)
        wait(50)
        compare(fullOption.hovered, false)
        compare(fullOption.background.border.color.toString(), initialBorderColor)
    }

    function test_storage_amount_command_line_prune_status_disables_options() {
        const text = "Set by command line (-prune). Remove the command-line option to change this here."
        optionsModel.prune = true
        optionsModel.pruneSizeGB = 7
        optionsModel.setStorageStatusForTest(false, 1000, "", "")
        optionsModel.setCoreSettingStatusForTest("prune", false, "command_line", text, false)

        const page = createTemporaryObject(storageOptions, this)
        verify(page !== null)
        page.customStorage = true
        page.customStorageAmount = 7
        wait(0)

        const reduceOption = findChild(page, "storageReduceOption")
        const fullOption = findChild(page, "storageFullOption")
        const customOption = findChild(page, "storageCustomOption")
        const info = findChild(page, "storagePruneCommandLineInfo")
        verify(reduceOption !== null)
        verify(fullOption !== null)
        verify(customOption !== null)
        verify(info !== null)

        compare(page.storageOptionsEditable, false)
        compare(reduceOption.enabled, false)
        compare(fullOption.enabled, false)
        compare(customOption.enabled, false)
        compare(customOption.checked, true)
        compare(info.text, text)
    }

    function test_storage_settings_rejects_prune_target_above_available_space() {
        optionsModel.prune = true
        optionsModel.pruneSizeGB = 2
        optionsModel.setStorageStatusForTest(false, 123, "", "")

        const page = createTemporaryObject(storageSettings, this)
        verify(page !== null)
        const setting = findChild(page, "pruneTargetSetting")
        const input = findChild(page, "pruneTargetInput")
        verify(setting !== null)
        verify(input !== null)

        input.text = "200"
        input.editingFinished()

        compare(setting.showErrorText, true)
        verify(setting.errorText.indexOf("111GB") !== -1)
        compare(optionsModel.pruneSizeGB, 2)
    }

    function test_custom_storage_option_shows_entered_target_and_reduce_can_reselect() {
        optionsModel.prune = true
        optionsModel.pruneSizeGB = 7
        optionsModel.setStorageStatusForTest(false, 123, "", "")

        const page = createTemporaryObject(storageAmount, this)
        verify(page !== null)
        page.customStorage = true
        page.customStorageAmount = 7
        wait(0)

        const customOption = findChild(page, "storageCustomOption")
        const reduceOption = findChild(page, "storageReduceOption")
        verify(customOption !== null)
        verify(reduceOption !== null)
        compare(customOption.checked, true)
        compare(customOption.text, "Custom")
        compare(customOption.description, "Storing recent blocks up to 7 GB.")
        verify(reduceOption.description.indexOf("14GB") !== -1)
        verify(reduceOption.description.indexOf("19GB") === -1)

        reduceOption.clicked()
        wait(0)

        compare(page.customStorage, true)
        compare(page.customStorageAmount, 7)
        compare(optionsModel.prune, true)
        compare(optionsModel.pruneSizeGB, 2)
        compare(reduceOption.checked, true)
        compare(customOption.checked, false)
        compare(customOption.text, "Custom")

        customOption.clicked()
        wait(0)

        compare(page.customStorage, true)
        compare(page.customStorageAmount, 7)
        compare(optionsModel.prune, true)
        compare(optionsModel.pruneSizeGB, 7)
        compare(reduceOption.checked, false)
        compare(customOption.checked, true)

        reduceOption.clicked()
        wait(0)

        compare(page.customStorage, true)
        compare(page.customStorageAmount, 7)
        compare(optionsModel.prune, true)
        compare(optionsModel.pruneSizeGB, 2)
        compare(reduceOption.checked, true)
        compare(customOption.checked, false)
    }
}
