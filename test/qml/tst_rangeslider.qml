// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    name: "RangeSlider"
    when: windowShown
    visible: true
    width: 400
    height: 120

    Component { id: sliderComponent; RangeSlider { width: 320; minValue: 0; maxValue: 100 } }

    function test_dragging_is_continuous_and_handles_cannot_cross() {
        const slider = createTemporaryObject(sliderComponent, this)
        slider.setValues(20, 80)
        waitForRendering(slider)
        const handle = slider.first.handle
        const start = handle.mapToItem(slider, handle.width / 2, handle.height / 2)
        mousePress(slider, start.x, start.y)
        mouseMove(slider, 135, start.y, 100)
        mouseRelease(slider, 135, start.y)
        verify(slider.lowerValue > 20 && slider.lowerValue < 80)
        verify(slider.lowerValue !== Math.round(slider.lowerValue))
        const moved = handle.mapToItem(slider, handle.width / 2, handle.height / 2)
        mousePress(slider, moved.x, moved.y)
        mouseMove(slider, slider.width, moved.y, 100)
        mouseRelease(slider, slider.width, moved.y)
        verify(slider.lowerValue <= slider.upperValue)
        compare(slider.upperValue, 80)
    }

    function test_keyboard_and_changing_bounds() {
        const slider = createTemporaryObject(sliderComponent, this)
        slider.setValues(20, 80)
        slider.first.handle.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Right)
        verify(slider.lowerValue > 20)
        slider.second.handle.forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Left)
        verify(slider.upperValue < 80)
        slider.maxValue = 30
        verify(slider.upperValue <= 30)
        verify(slider.lowerValue <= slider.upperValue)
        slider.maxValue = 0
        compare(slider.enabled, false)
        compare(slider.lowerValue, 0)
        compare(slider.upperValue, 0)
    }
}
