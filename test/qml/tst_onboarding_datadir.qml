// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components"
import "../../qml/controls"
import "../../qml/pages/onboarding"

TestCase {
    name: "OnboardingDataDir"
    when: windowShown
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

    Component {
        id: appFolderDialog
        AppFolderDialog { }
    }

    Component {
        id: signalSpy
        SignalSpy { }
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

        infoButton.clicked()
        tryVerify(function() { return findChild(wizard.currentItem, "settingsAbout") !== null })
    }

    function test_normal_onboarding_starts_at_cover() {
        const wizard = createTemporaryObject(fullWizard, this)
        verify(wizard !== null)
        compare(wizard.currentItem.objectName, "onboardingCover")
    }

    function test_full_preinit_wizard_can_go_back_from_storage_amount() {
        const wizard = createTemporaryObject(fullPreInitWizard, this)
        verify(wizard !== null)
        compare(wizard.currentItem.objectName, "onboardingCover")

        triggerButton(wizard.currentItem, "onboardingCoverButton")
        tryVerify(function() { return wizard.currentItem.objectName === "onboardingStrengthen" })

        triggerButton(wizard.currentItem, "onboardingStrengthenButton")
        tryVerify(function() { return wizard.currentItem.objectName === "onboardingBlockclock" })

        triggerButton(wizard.currentItem, "onboardingBlockclockButton")
        tryVerify(function() { return wizard.currentItem.objectName === "onboardingStorageLocation" })

        triggerButton(wizard.currentItem, "onboardingStorageLocationButton")
        tryVerify(function() { return wizard.currentItem.objectName === "onboardingStorageAmount" })
        const backButton = findChild(wizard.currentItem, "onboardingStorageAmountBackButton")
        verify(backButton !== null)

        backButton.clicked()
        tryVerify(function() { return wizard.currentItem.objectName === "onboardingStorageLocation" })
    }

    function test_storage_location_uses_injected_chainstate_size() {
        const page = createTemporaryObject(storageLocation, this)
        verify(page !== null)
        verify(page.description.indexOf("10GB") !== -1)
    }

    function test_existing_profile_storage_location_uses_operational_minimum() {
        optionsModel.existingProfile = true
        optionsModel.setStorageStatusForTest(false, 1, "", "")

        const page = createTemporaryObject(storageLocation, this)
        verify(page !== null)
        const defaultOption = findChild(page, "storageDefaultLocationOption")
        verify(defaultOption !== null)

        compare(page.description, "Where do you want to store the downloaded block data?\nYou need a minimum of 1GB of storage.")
        tryCompare(page, "buttonEnabled", true)
        compare(defaultOption.showErrorText, false)
    }

    function test_connection_final_button_labels_start_commit_point() {
        const page = createTemporaryObject(connection, this)
        verify(page !== null)
        const button = findChild(page, "onboardingConnectionButton")
        verify(button !== null)
        compare(button.text, "Start")
    }

    function test_storage_location_uses_folder_dialog_for_custom_directory() {
        const page = createTemporaryObject(storageLocation, this)
        verify(page !== null)
        const dialog = findChild(page, "customDataDirFolderDialog")
        verify(dialog !== null)
        verify(dialog.selectedFolder !== undefined)
    }

    function test_app_folder_dialog_compatibility_contract() {
        const dialog = createTemporaryObject(appFolderDialog, this)
        verify(dialog !== null)
        verify(dialog.selectedFolder !== undefined)
        compare(typeof dialog.open, "function")
        compare(typeof dialog.close, "function")

        const acceptedSpy = createTemporaryObject(signalSpy, this, {
            target: dialog,
            signalName: "accepted"
        })
        verify(acceptedSpy.valid)
        dialog.accepted()
        compare(acceptedSpy.count, 1)
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

        compare(page.description, "Where do you want to store the downloaded block data?\nYou need a minimum of 10GB of storage.")
        compare(button.enabled, false)
        compare(defaultOption.showErrorText, true)
        compare(defaultOption.errorText, "Not enough storage available.")
    }

    function test_storage_amount_uses_detected_available_space() {
        const page = createTemporaryObject(storageAmount, this)
        verify(page !== null)
        const info = findChild(page, "onboardingStorageAmountPage")
        verify(info !== null)
        compare(info.headerText, "Storage amount")
        compare(info.description, "Data retrieved from the Bitcoin network is stored on your device.\nYou have 123GB of storage available.")
        verify(info.description.indexOf("500GB") === -1)
        compare(info.subtext, "")
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
        const infoPage = findChild(page, "onboardingStorageAmountPage")
        const reduceOption = findChild(page, "storageReduceOption")
        const fullOption = findChild(page, "storageFullOption")
        verify(infoPage !== null)
        verify(reduceOption !== null)
        verify(fullOption !== null)

        compare(reduceOption.enabled, true)
        tryCompare(fullOption, "enabled", true)
        compare(fullOption.checked, true)
        tryCompare(infoPage, "buttonEnabled", true)
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
