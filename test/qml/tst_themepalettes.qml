// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    id: testCase
    name: "ThemePalettes"

    property string originalPaletteId: "core-dark"

    function initTestCase() {
        originalPaletteId = Theme.paletteId
    }

    function cleanup() {
        Theme.paletteId = originalPaletteId
    }

    function test_palettesHaveUniqueIds() {
        const ids = {}
        verify(Theme.palettes.length >= 2)
        for (let i = 0; i < Theme.palettes.length; ++i) {
            const palette = Theme.palettes[i]
            verify(palette.paletteId !== "", "palette " + i + " has no id")
            verify(palette.displayName !== "", palette.paletteId + " has no display name")
            verify(ids[palette.paletteId] === undefined, palette.paletteId + " is listed twice")
            ids[palette.paletteId] = true
        }
        verify(ids["core-dark"])
        verify(ids["core-light"])
    }

    function test_migrationFromDarkSetting_data() {
        return [
            {tag: "dark", storedId: "", storedDark: true, expected: "core-dark"},
            {tag: "light", storedId: "", storedDark: false, expected: "core-light"},
            {tag: "missing dark key", storedId: "", storedDark: undefined, expected: "core-dark"},
            {tag: "missing palette key", storedId: undefined, storedDark: false, expected: "core-light"},
            {tag: "palette wins over dark", storedId: "core-light", storedDark: true, expected: "core-light"},
            {tag: "unknown id is kept", storedId: "solarized-dark", storedDark: false, expected: "solarized-dark"}
        ]
    }

    function test_migrationFromDarkSetting(data) {
        compare(Theme.migratedPaletteId(data.storedId, data.storedDark), data.expected)
    }

    function test_unknownIdFallsBackByDarkness_data() {
        return [
            {tag: "dark", fallbackDark: true, expected: "core-dark"},
            {tag: "light", fallbackDark: false, expected: "core-light"}
        ]
    }

    function test_unknownIdFallsBackByDarkness(data) {
        compare(Theme.paletteFor("no-such-palette", data.fallbackDark).paletteId, data.expected)
        compare(Theme.paletteFor("core-light", data.fallbackDark).paletteId, "core-light")
        compare(Theme.paletteFor("core-dark", data.fallbackDark).paletteId, "core-dark")
    }

    function test_paletteIdSelectsPalette() {
        Theme.paletteId = "core-light"
        compare(Theme.color.paletteId, "core-light")
        compare(Theme.dark, false)
        Theme.paletteId = "core-dark"
        compare(Theme.color.paletteId, "core-dark")
        compare(Theme.dark, true)
    }

    function test_unknownPaletteIdKeepsBrightness() {
        Theme.paletteId = "core-light"
        Theme.paletteId = "no-such-palette"
        compare(Theme.paletteId, "no-such-palette")
        compare(Theme.color.paletteId, "core-light")
        compare(Theme.dark, false)

        Theme.paletteId = "core-dark"
        Theme.paletteId = "another-missing-palette"
        compare(Theme.color.paletteId, "core-dark")
        compare(Theme.dark, true)
    }
}
