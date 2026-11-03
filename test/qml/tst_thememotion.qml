// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/controls"

TestCase {
    id: testCase
    name: "ThemeMotion"

    property string initialMode: ""

    function init() {
        initialMode = Theme.motion.mode
    }

    function cleanup() {
        Theme.motion.mode = initialMode
    }

    function test_full_keeps_every_animation() {
        Theme.motion.mode = "full"
        verify(Theme.motion.animated)
        verify(!Theme.motion.reduced)
        verify(Theme.motion.decorativeLoops)
        compare(Theme.motion.fast, 100)
        compare(Theme.motion.normal, 150)
        compare(Theme.motion.moderate, 200)
        compare(Theme.motion.exit, 250)
        compare(Theme.motion.slow, 300)
        compare(Theme.motion.navigation, Theme.motion.slow)
        compare(Theme.motion.travel(300), 300)
        compare(Theme.motion.fade(300), 300)
        compare(Theme.motion.delay(650), 650)
        compare(Theme.motion.dialogOffset, 30)
        compare(Theme.motion.popoverScale, 0.96)
        compare(Theme.motion.pressScale, 0.98)
    }

    function test_reduced_removes_movement_and_keeps_feedback() {
        Theme.motion.mode = "reduced"
        verify(Theme.motion.animated)
        verify(Theme.motion.reduced)
        verify(!Theme.motion.decorativeLoops)
        compare(Theme.motion.normal, 150)
        compare(Theme.motion.travel(300), 0)
        compare(Theme.motion.fade(300), Theme.motion.normal)
        compare(Theme.motion.fade(100), 100)
        compare(Theme.motion.delay(650), 0)
        compare(Theme.motion.dialogOffset, 0)
        compare(Theme.motion.popoverScale, 1)
        compare(Theme.motion.pressScale, 1)
    }

    function test_off_makes_everything_instant() {
        Theme.motion.mode = "off"
        verify(!Theme.motion.animated)
        verify(Theme.motion.reduced)
        verify(!Theme.motion.decorativeLoops)
        for (const token of ["fast", "normal", "moderate", "exit", "slow", "navigation", "tooltipDelay"]) {
            compare(Theme.motion[token], 0, token)
        }
        compare(Theme.motion.travel(300), 0)
        compare(Theme.motion.fade(300), 0)
        compare(Theme.motion.delay(650), 0)
    }

    function test_reduce_motion_setting_only_lowers_full_motion() {
        compare(Theme.motion.preferredMode("full", false), "full")
        compare(Theme.motion.preferredMode("full", true), "reduced")
        compare(Theme.motion.preferredMode("reduced", false), "reduced")
        compare(Theme.motion.preferredMode("off", true), "off")
    }

    function test_easing_roles() {
        compare(Theme.motion.easing.enter, Easing.OutCubic)
        compare(Theme.motion.easing.exit, Easing.InCubic)
        compare(Theme.motion.easing.move, Easing.InOutCubic)
        compare(Theme.motion.easing.loop, Easing.Linear)
    }
}
