// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtTest 1.2
import "../../qml/components"
TestCase {
    name: "FeeSelection"
    when: windowShown
    visible: true
    width: 900; height: 700
    Component { id: factory; SendFeeSelection { walletModel: testWalletModel; width: 800 } }
    function init() { testWalletModel.customFeeEnabled = false; testWalletModel.customFeeRate = ""; testWalletModel.targetBlocks = 6; testWalletModel.feeEstimatePending = false }
    function selectOption(control, index) {
        const picker = findChild(control, "feeSelectionPicker")
        picker.open()
        compare(picker.opened, true)
        picker.itemAtIndex(index).clicked()
        tryCompare(picker, "opened", false)
    }
    function test_targets_and_selected_state() {
        const control = createTemporaryObject(factory, this)
        const picker = findChild(control, "feeSelectionPicker")
        compare(picker.currentText, "Standard")
        compare(picker.itemAtIndex(0).subtitle, "Targets 2 blocks · Usually higher fee")
        compare(picker.itemAtIndex(1).subtitle, "Targets 6 blocks · Balanced speed and cost")
        compare(picker.itemAtIndex(2).subtitle, "Targets 10 blocks · Usually lower fee")
        compare(picker.itemAtIndex(3).subtitle, "Choose your fee rate and block target")
        for (let i = 0; i < 3; ++i) {
            selectOption(control, i)
            compare(testWalletModel.targetBlocks, [2, 6, 10][i])
            compare(picker.currentText, ["Priority", "Standard", "Flexible"][i])
            compare(control.customSelected, false)
        }
    }
    function test_custom_slider_and_rate_are_editable() {
        const control = createTemporaryObject(factory, this)
        const rate = findChild(control, "feeSelectionCustomRateInput")
        selectOption(control, 3)
        compare(control.customSelected, true)
        compare(rate.visible, true)
        const slider = findChild(control, "feeTargetBlocksSlider")
        const targetRow = findChild(control, "feeTargetBlocksRow")
        const targets = [2, 3, 4, 6, 10, 25, 50]
        compare(slider.to, targets.length - 1)
        for (let index = 0; index < targets.length; ++index) {
            slider.value = index
            slider.moved()
            compare(testWalletModel.targetBlocks, targets[index])
            compare(targetRow.description, targets[index] === 50 ? "50+ blocks" : targets[index] + " blocks")
            compare(findChild(control, "feeTargetBlocksTick" + index).text,
                targets[index] === 50 ? "50+" : String(targets[index]))
        }
        rate.text = "4.125"; rate.textEdited()
        compare(testWalletModel.customFeeRate, "4.125")
        selectOption(control, 1)
        compare(control.customSelected, false)
        compare(rate.visible, false)
    }
    function test_custom_selection_uses_nearest_target() {
        testWalletModel.targetBlocks = 25
        const control = createTemporaryObject(factory, this)
        const slider = findChild(control, "feeTargetBlocksSlider")
        selectOption(control, 3)
        compare(testWalletModel.targetBlocks, 25)
        compare(slider.value, 5)

        selectOption(control, 1)
        testWalletModel.targetBlocks = 42
        selectOption(control, 3)
        compare(testWalletModel.targetBlocks, 50)
        compare(slider.value, 6)
    }
    function test_pending_estimate_does_not_show_stale_fee() {
        const control = createTemporaryObject(factory, this)
        const estimate = findChild(control, "feeSelectionEstimateLabel")
        testWalletModel.feeEstimatePending = true
        compare(estimate.value, "—")
        testWalletModel.feeEstimatePending = false
        verify(estimate.value.length > 0)
    }
}
