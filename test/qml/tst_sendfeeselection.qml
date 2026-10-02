// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtTest 1.2
import "../../qml/components"
import "../../qml/components/widgets"
import "../../qml/controls"
TestCase {
    name: "FeeSelection"
    when: windowShown
    visible: true
    width: 900; height: 700
    QtObject {
        id: estimates
        property var blockTargetRates: [8, 6, 4, 2, 1, 0.5, 0.25]
        property var rates: [8, 4, 2, 0.25]
        property real referenceRate: 2
        property bool active: false
    }
    Component { id: factory; SendFeeSelection { walletModel: testWalletModel; feeRatesModelRef: estimates; width: 800 } }
    Component { id: widgetFactory; FeeRatesWidget { feeRatesModelRef: estimates; width: 330; height: 148 } }
    function init() {
        testWalletModel.customFeeEnabled = false
        testWalletModel.customFeeRate = ""
        testWalletModel.targetBlocks = 6
        testWalletModel.feeEstimatePending = false
        estimates.blockTargetRates = [8, 6, 4, 2, 1, 0.5, 0.25]
        estimates.referenceRate = 2
        Theme.dark = true
    }
    function cleanup() { Theme.dark = true }
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

    function test_gradient_follows_live_estimates_and_history() {
        const control = createTemporaryObject(factory, this)
        const widget = createTemporaryObject(widgetFactory, this)
        const slider = findChild(control, "feeTargetBlocksSlider")
        selectOption(control, 3)
        compare(slider.trackGradientOpacity, 0.5)
        compare(slider.trackGradient.stops.length, 7)
        for (const dark of [true, false]) {
            Theme.dark = dark
            compare(control.targetColors[0], widget.rateColors[0])
            compare(control.targetColors[2], widget.rateColors[1])
            compare(control.targetColors[3], widget.rateColors[2])
            compare(control.targetColors[0], Theme.color.red)
            compare(control.targetColors[2], Theme.color.orange)
            compare(control.targetColors[3], Theme.color.blue)
            compare(control.targetColors[4], Theme.color.green)
            for (let index = 0; index < 7; ++index) {
                compare(slider.trackGradient.stops[index].color, control.targetColors[index])
                fuzzyCompare(slider.trackGradient.stops[index].position, index / 6, 0.0001)
            }
        }
        // Uniform fees produce a uniform color, regardless of target position.
        estimates.blockTargetRates = [4, 4, 4, 4, 4, 4, 4]
        for (let index = 0; index < 7; ++index) compare(slider.trackGradient.stops[index].color, Theme.color.orange)
        estimates.referenceRate = 8
        for (let index = 0; index < 7; ++index) compare(slider.trackGradient.stops[index].color, Theme.color.green)
        estimates.referenceRate = -1
        for (let index = 0; index < 7; ++index) compare(slider.trackGradient.stops[index].color, Theme.color.neutral6)
        estimates.referenceRate = 2
        estimates.blockTargetRates = [-1, 0, NaN, Infinity, 8, 1, 4]
        for (let index = 0; index < 4; ++index) compare(slider.trackGradient.stops[index].color, Theme.color.neutral6)
        compare(slider.trackGradient.stops[4].color, Theme.color.red)
        compare(slider.trackGradient.stops[5].color, Theme.color.green)
        compare(slider.trackGradient.stops[6].color, Theme.color.orange)
    }

    function test_slider_shares_estimator_activity_with_dashboard() {
        const control = createTemporaryObject(factory, this)
        compare(estimates.active, false)
        selectOption(control, 3)
        compare(estimates.active, true)
        control.visible = false
        compare(estimates.active, false)
        const widget = createTemporaryObject(widgetFactory, this)
        compare(estimates.active, true)
        control.visible = true
        widget.active = false
        compare(estimates.active, true)
        selectOption(control, 1)
        compare(estimates.active, false)
        selectOption(control, 3)
        control.destroy()
        wait(0)
        compare(estimates.active, false)
    }
}
