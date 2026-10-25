// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    id: testCase
    name: "ThemeContrast"

    readonly property var neutralSteps: ["neutral0", "neutral1", "neutral2", "neutral3", "neutral4"]
    readonly property var surfaces: ["background", "surface1", "surface2", "surface3"]

    // WCAG 2 thresholds: 4.5:1 for text, 3:1 for focus indicators and glyphs.
    // Light surface3 fails both for neutral7 text and the focus ring, so only
    // primary text is checked there.
    // Neighbouring neutral steps and surfaces must differ by at least 1.05:1,
    // so a level never looks the same as its parent and hover, pressed and
    // divider colors stay visible.
    readonly property var textPairs: [
        {fg: "neutral9", bg: "background", min: 4.5},
        {fg: "neutral9", bg: "surface1", min: 4.5},
        {fg: "neutral9", bg: "surface2", min: 4.5},
        {fg: "neutral9", bg: "surface3", min: 4.5},
        {fg: "neutral7", bg: "background", min: 4.5},
        {fg: "neutral7", bg: "surface1", min: 4.5},
        {fg: "neutral7", bg: "surface2", min: 4.5},
        {fg: "warning", bg: "background", min: 4.5},
        {fg: "placeholder", bg: "surface1", min: 4.5},
        {fg: "placeholder", bg: "surface2", min: 4.5},
        {fg: "accentText", bg: "background", min: 4.5},
        {fg: "accentText", bg: "surface1", min: 4.5},
        {fg: "selectedText", bg: "selection", min: 4.5}
    ]
    // Text on a tint uses the hue's *Text role. accentText is only checked
    // over background: over surface1 it is 4.28:1 in light.
    readonly property var tintTextPairs: [
        {hue: "accent", bg: "background"},
        {hue: "danger", bg: "background"}, {hue: "danger", bg: "surface1"},
        {hue: "success", bg: "background"}, {hue: "success", bg: "surface1"},
        {hue: "info", bg: "background"}, {hue: "info", bg: "surface1"},
        {hue: "warning", bg: "background"}, {hue: "warning", bg: "surface1"},
        {hue: "paymentRequest", bg: "background"}, {hue: "paymentRequest", bg: "surface1"}
    ]
    readonly property var focusPairs: [
        {fg: "controlBorder", bg: "surface1", min: 3},
        {fg: "controlBorder", bg: "surface2", min: 3},
        {fg: "sliderTrack", bg: "surface1", min: 3},
        {fg: "focus", bg: "background", min: 3},
        {fg: "focus", bg: "surface1", min: 3},
        {fg: "focus", bg: "surface2", min: 3}
    ]
    // Content drawn on accent fills. White on orange, green and red is below
    // 4.5:1 in both palettes; these minimums only stop it getting worse.
    readonly property var onAccentPairs: [
        {fg: "accentForeground", bg: "accent", min: 2},
        {fg: "accentForeground", bg: "success", min: 2.5},
        {fg: "accentForeground", bg: "danger", min: 3},
        {fg: "warningForeground", bg: "warning", min: 4.5},
        {fg: "activeForeground", bg: "activeFill", min: 4.5}
    ]
    readonly property var stateRoles: ["surfaceHover", "surfacePressed", "surfaceSelected", "surfaceDivider"]
    readonly property real minimumStep: 1.05

    function palettesData() {
        const data = []
        for (let i = 0; i < Theme.palettes.length; ++i)
            data.push({tag: Theme.palettes[i].paletteId, palette: Theme.palettes[i]})
        return data
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

    function checkPairs(data, pairs) {
        const palette = data.palette
        for (const pair of pairs) {
            const fg = palette[pair.fg]
            let bg = palette[pair.bg]
            let name = pair.fg + " on " + pair.bg
            if (pair.tint !== undefined) {
                bg = over(palette[pair.tint], pair.alpha, bg)
                name = pair.fg + " on " + pair.tint + " " + pair.alpha + " over " + pair.bg
            }
            const ratio = contrast(fg, bg)
            verify(ratio >= pair.min,
                   data.tag + ": " + name + " is " + ratio.toFixed(2) + ":1, expected at least " + pair.min + ":1")
        }
    }

    function checkSteps(data, steps) {
        const palette = data.palette
        const direction = Math.sign(luminance(palette[steps[steps.length - 1]]) - luminance(palette[steps[0]]))
        verify(direction !== 0)
        for (let i = 0; i + 1 < steps.length; ++i) {
            const a = palette[steps[i]]
            const b = palette[steps[i + 1]]
            const name = data.tag + ": " + steps[i] + " to " + steps[i + 1]
            compare(Math.sign(luminance(b) - luminance(a)), direction, name + " is not monotonic")
            const ratio = contrast(a, b)
            verify(ratio >= minimumStep, name + " is " + ratio.toFixed(3) + ":1, expected at least " + minimumStep + ":1")
        }
    }

    function test_text_data() { return palettesData() }
    function test_text(data) { checkPairs(data, textPairs) }

    function test_focus_data() { return palettesData() }
    function test_focus(data) { checkPairs(data, focusPairs) }

    function test_textOnTints_data() { return palettesData() }
    function test_textOnTints(data) {
        const palette = data.palette
        for (const pair of tintTextPairs) {
            const tinted = over(palette[pair.hue], palette.tintAlpha, palette[pair.bg])
            verify(Qt.colorEqual(palette.tint(palette[pair.hue]), palette[pair.hue + "Tint"]), data.tag + ": " + pair.hue + "Tint")
            verify(Qt.colorEqual(palette.textFor(palette[pair.hue]), palette[pair.hue + "Text"]), data.tag + ": textFor(" + pair.hue + ")")
            const ratio = contrast(palette[pair.hue + "Text"], tinted)
            verify(ratio >= 4.5, data.tag + ": " + pair.hue + "Text on " + pair.hue + "Tint over " + pair.bg + " is "
                   + ratio.toFixed(2) + ":1, expected at least 4.5:1")
        }
    }

    function test_onAccent_data() { return palettesData() }
    function test_onAccent(data) { checkPairs(data, onAccentPairs) }

    function test_neutralSteps_data() { return palettesData() }
    function test_neutralSteps(data) { checkSteps(data, neutralSteps) }

    function test_surfaceSteps_data() { return palettesData() }
    function test_surfaceSteps(data) {
        checkSteps(data, surfaces)
        for (let level = 0; level < surfaces.length; ++level)
            verify(Qt.colorEqual(data.palette.surface(level), data.palette[surfaces[level]]), data.tag + ": surface(" + level + ")")
    }

    function test_stateRolesOverSurfaces_data() { return palettesData() }
    function test_stateRolesOverSurfaces(data) {
        const palette = data.palette
        for (const surfaceName of surfaces) {
            const surface = palette[surfaceName]
            const ratios = {}
            for (const role of stateRoles) {
                const state = palette[role]
                verify(state.a > 0 && state.a < 1, data.tag + ": " + role + " should be translucent")
                ratios[role] = contrast(over(state, state.a, surface), surface)
                verify(ratios[role] >= minimumStep,
                       data.tag + ": " + role + " over " + surfaceName + " is " + ratios[role].toFixed(3)
                       + ":1, expected at least " + minimumStep + ":1")
            }
            verify(ratios.surfacePressed > ratios.surfaceHover,
                   data.tag + ": surfacePressed over " + surfaceName + " should differ more than surfaceHover")
        }
    }
}
