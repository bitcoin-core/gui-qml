// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components"

TestCase {
    name: "FeeSelection"
    when: windowShown
    width: 900
    height: 700

    Component {
        id: feeSelectionComponent

        FeeSelection {
            walletModel: testWalletModel
            currentTarget: testWalletModel.targetBlocks
            onFeeChanged: function(target) {
                testWalletModel.targetBlocks = target
            }
        }
    }

    SignalSpy {
        id: feeChangedSpy
    }

    function init() {
        testWalletModel.clearFeeEstimates()
        testWalletModel.setFeeEstimate(1, "0.00000750 ₿")
        testWalletModel.setFeeEstimate(2, "0.00000500 ₿")
        testWalletModel.setFeeEstimate(6, "0.00000250 ₿")
        testWalletModel.customFeeEnabled = false
        testWalletModel.customFeeRate = ""
        testWalletModel.targetBlocks = 2
    }

    function test_feeSelection_has_stable_selectors() {
        const control = createTemporaryObject(feeSelectionComponent, this)
        verify(control !== null)

        compare(control.objectName, "feeSelectionControl")
        verify(findChild(control, "feeSelectionEstimateLabel") !== null)
        verify(findChild(control, "feeSelectionCustomRateInput") !== null)
        verify(findChild(control, "feeSelectionCustomEstimateLabel") !== null)
        verify(findChild(control, "feeSelectionDropdownButton") !== null)
        verify(findChild(control, "feeSelectionPopup") !== null)
        verify(findChild(control, "feeSelectionList") !== null)
        compare(findChild(control, "feeSelectionIncludeFeeToggle"), null)

        const customFeeRateInput = findChild(control, "feeSelectionCustomRateInput")
        compare(customFeeRateInput.maximumLength, control.customFeeRateMaximumLength)
    }

    function test_feeSelection_matches_standard_fee_spec_labels_and_estimate() {
        testWalletModel.targetBlocks = 2

        const control = createTemporaryObject(feeSelectionComponent, this)
        verify(control !== null)

        compare(control.selectedIndex, 1)
        compare(control.selectedLabel, "Default")
        compare(control.selectedDuration, "(~20 mins)")
        compare(control.selectedEstimate, "0.00000500 ₿")

        const estimateLabel = findChild(control, "feeSelectionEstimateLabel")
        verify(estimateLabel !== null)
        compare(estimateLabel.text, "0.00000500 ₿")
    }

    function test_feeSelection_selecting_option_updates_state_and_emits() {
        testWalletModel.targetBlocks = 2

        const control = createTemporaryObject(feeSelectionComponent, this)
        verify(control !== null)

        const popup = findChild(control, "feeSelectionPopup")
        const picker = findChild(control, "feeSelectionList")
        verify(popup !== null)
        verify(picker !== null)

        feeChangedSpy.target = control
        feeChangedSpy.signalName = "feeChanged"
        feeChangedSpy.clear()

        popup.open()
        tryVerify(function() {
            return picker.itemAtIndex(2) !== null
        })

        const lowFeeOption = picker.itemAtIndex(2)
        verify(lowFeeOption !== null)
        compare(lowFeeOption.objectName, "feeSelectionOption2")
        compare(lowFeeOption.subtitle, "0.00000250 ₿")
        verify(findChild(lowFeeOption, "feeSelectionOptionEstimate2") !== null)

        mouseClick(lowFeeOption, lowFeeOption.width / 2, lowFeeOption.height / 2)

        compare(control.selectedIndex, 2)
        compare(control.selectedLabel, "Low")
        compare(control.selectedDuration, "(~60 mins)")
        compare(control.selectedEstimate, "0.00000250 ₿")
        compare(feeChangedSpy.count, 1)
        compare(feeChangedSpy.signalArguments[0][0], 6)
        tryCompare(popup, "visible", false)
    }

    function test_feeSelection_picker_estimates_react_to_walletModel() {
        const control = createTemporaryObject(feeSelectionComponent, this)
        verify(control !== null)

        const popup = findChild(control, "feeSelectionPopup")
        const picker = findChild(control, "feeSelectionList")
        verify(popup !== null)
        verify(picker !== null)

        popup.open()
        tryVerify(function() {
            return picker.itemAtIndex(0) !== null
                && picker.itemAtIndex(1) !== null
                && picker.itemAtIndex(2) !== null
        })

        compare(picker.itemAtIndex(0).subtitle, "0.00000750 ₿")
        compare(picker.itemAtIndex(1).subtitle, "0.00000500 ₿")
        compare(picker.itemAtIndex(2).subtitle, "0.00000250 ₿")
        compare(picker.itemAtIndex(3).subtitle, "")

        testWalletModel.setFeeEstimate(1, "0.00009999 ₿")
        tryCompare(picker.itemAtIndex(0), "subtitle", "0.00009999 ₿")
    }

    function test_feeSelection_current_target_syncs_selected_preset() {
        testWalletModel.targetBlocks = 6

        const control = createTemporaryObject(feeSelectionComponent, this)
        verify(control !== null)

        compare(control.selectedIndex, 2)
        compare(control.selectedLabel, "Low")
        compare(control.selectedDuration, "(~60 mins)")
        compare(control.selectedEstimate, "0.00000250 ₿")

        testWalletModel.targetBlocks = 1
        compare(control.selectedIndex, 0)
        compare(control.selectedLabel, "High")
        compare(control.selectedDuration, "(~10 mins)")
        compare(control.selectedEstimate, "0.00000750 ₿")
    }

    function test_feeSelection_custom_option_switches_to_fee_rate_mode() {
        const control = createTemporaryObject(feeSelectionComponent, this)
        verify(control !== null)

        const popup = findChild(control, "feeSelectionPopup")
        const picker = findChild(control, "feeSelectionList")
        const presetEstimateLabel = findChild(control, "feeSelectionEstimateLabel")
        const customFeeRateInput = findChild(control, "feeSelectionCustomRateInput")
        const customEstimateLabel = findChild(control, "feeSelectionCustomEstimateLabel")
        verify(popup !== null)
        verify(picker !== null)
        verify(presetEstimateLabel !== null)
        verify(customFeeRateInput !== null)
        verify(customEstimateLabel !== null)

        popup.open()
        tryVerify(function() {
            return picker.itemAtIndex(3) !== null
        })

        const customOption = picker.itemAtIndex(3)
        verify(customOption !== null)
        compare(customOption.subtitle, "")

        mouseClick(customOption, customOption.width / 2, customOption.height / 2)

        compare(control.selectedIndex, 3)
        compare(control.selectedLabel, "sats/vbyte")
        compare(control.selectedDuration, "")
        compare(testWalletModel.customFeeEnabled, true)
        tryCompare(presetEstimateLabel, "visible", false)
        compare(customEstimateLabel.text, "")
    }

    function test_feeSelection_custom_rate_updates_fee_estimate_and_presets_restore_layout() {
        const control = createTemporaryObject(feeSelectionComponent, this)
        verify(control !== null)

        const popup = findChild(control, "feeSelectionPopup")
        const picker = findChild(control, "feeSelectionList")
        const presetEstimateLabel = findChild(control, "feeSelectionEstimateLabel")
        const customFeeRateInput = findChild(control, "feeSelectionCustomRateInput")
        const customEstimateLabel = findChild(control, "feeSelectionCustomEstimateLabel")
        verify(popup !== null)
        verify(picker !== null)
        verify(presetEstimateLabel !== null)
        verify(customFeeRateInput !== null)
        verify(customEstimateLabel !== null)

        popup.open()
        tryVerify(function() {
            return picker.itemAtIndex(3) !== null
        })
        const customOption = picker.itemAtIndex(3)
        mouseClick(customOption, customOption.width / 2, customOption.height / 2)

        customFeeRateInput.text = "2"
        compare(testWalletModel.customFeeRate, "2")
        compare(testWalletModel.customFeeRateValid, true)
        tryCompare(customEstimateLabel, "text", "0.00000400 ₿")

        customFeeRateInput.text = "12345678901234567890"
        compare(customFeeRateInput.text.length, control.customFeeRateMaximumLength)
        compare(testWalletModel.customFeeRate, customFeeRateInput.text)

        popup.open()
        tryVerify(function() {
            return picker.itemAtIndex(2) !== null
        })
        const lowFeeOption = picker.itemAtIndex(2)
        mouseClick(lowFeeOption, lowFeeOption.width / 2, lowFeeOption.height / 2)

        compare(testWalletModel.customFeeEnabled, false)
        compare(control.selectedIndex, 2)
        compare(control.selectedLabel, "Low")
        compare(control.selectedEstimate, "0.00000250 ₿")
    }
}
