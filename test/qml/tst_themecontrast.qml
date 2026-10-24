// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    id: testCase
    name: "ThemeContrast"

    property bool originalDark: true

    readonly property var palettes: [
        {tag: "dark", dark: true},
        {tag: "light", dark: false}
    ]

    readonly property var neutralSteps: ["neutral0", "neutral1", "neutral2", "neutral3", "neutral4"]

    // WCAG 2 thresholds: 4.5:1 for text, 3:1 for focus indicators and glyphs.
    // Neighbouring neutral steps must differ by at least 1.05:1, so hover,
    // pressed and divider colors that step through the scale stay visible.
    readonly property var textPairs: [
        {fg: "neutral9", bg: "neutral0", min: 4.5},
        {fg: "neutral9", bg: "neutral1", min: 4.5},
        {fg: "neutral9", bg: "neutral2", min: 4.5},
        {fg: "neutral7", bg: "neutral0", min: 4.5},
        {fg: "neutral7", bg: "neutral1", min: 4.5},
        {fg: "neutral7", bg: "neutral2", min: 4.5},
        {fg: "amber", bg: "neutral0", min: 4.5},
        {fg: "amber", bg: "neutral0", tint: "amber", alpha: 0.16, min: 4.5}
    ]
    readonly property var focusPairs: [
        {fg: "purple", bg: "neutral0", min: 3},
        {fg: "purple", bg: "neutral1", min: 3},
        {fg: "purple", bg: "neutral2", min: 3}
    ]
    // Content drawn on accent fills. White on orange, green and red is below
    // 4.5:1 in both palettes; these minimums only stop it getting worse.
    readonly property var onAccentPairs: [
        {fg: "white", bg: "orange", min: 2},
        {fg: "white", bg: "green", min: 2.5},
        {fg: "white", bg: "red", min: 3},
        {fg: "neutral0", bg: "amber", min: 4.5}
    ]
    readonly property real minimumNeutralStep: 1.05

    function initTestCase() {
        originalDark = Theme.dark
    }

    function cleanup() {
        Theme.dark = originalDark
    }

    function cleanupTestCase() {
        Theme.dark = originalDark
    }

    function channel(value) {
        return value <= 0.03928 ? value / 12.92 : Math.pow((value + 0.055) / 1.055, 2.4)
    }

    function luminance(color) {
        return 0.2126 * channel(color.r) + 0.7152 * channel(color.g) + 0.0722 * channel(color.b)
    }

    function contrast(a, b) {
        const la = luminance(a)
        const lb = luminance(b)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }

    function over(color, alpha, background) {
        return Qt.rgba(color.r * alpha + background.r * (1 - alpha),
                       color.g * alpha + background.g * (1 - alpha),
                       color.b * alpha + background.b * (1 - alpha), 1)
    }

    function checkPairs(palette, pairs) {
        Theme.dark = palette.dark
        for (const pair of pairs) {
            const fg = Theme.color[pair.fg]
            let bg = Theme.color[pair.bg]
            let name = pair.fg + " on " + pair.bg
            if (pair.tint !== undefined) {
                bg = over(Theme.color[pair.tint], pair.alpha, bg)
                name = pair.fg + " on " + pair.tint + " " + pair.alpha + " over " + pair.bg
            }
            const ratio = contrast(fg, bg)
            verify(ratio >= pair.min,
                   palette.tag + ": " + name + " is " + ratio.toFixed(2) + ":1, expected at least " + pair.min + ":1")
        }
    }

    function test_text_data() { return palettes }
    function test_text(data) { checkPairs(data, textPairs) }

    function test_focus_data() { return palettes }
    function test_focus(data) { checkPairs(data, focusPairs) }

    function test_onAccent_data() { return palettes }
    function test_onAccent(data) { checkPairs(data, onAccentPairs) }

    function test_neutralSteps_data() { return palettes }
    function test_neutralSteps(data) {
        Theme.dark = data.dark
        const direction = Math.sign(luminance(Theme.color[neutralSteps[neutralSteps.length - 1]])
                                    - luminance(Theme.color[neutralSteps[0]]))
        verify(direction !== 0)
        for (let i = 0; i + 1 < neutralSteps.length; ++i) {
            const a = Theme.color[neutralSteps[i]]
            const b = Theme.color[neutralSteps[i + 1]]
            const name = data.tag + ": " + neutralSteps[i] + " to " + neutralSteps[i + 1]
            compare(Math.sign(luminance(b) - luminance(a)), direction, name + " is not monotonic")
            const ratio = contrast(a, b)
            verify(ratio >= minimumNeutralStep,
                   name + " is " + ratio.toFixed(3) + ":1, expected at least " + minimumNeutralStep + ":1")
        }
    }
}
