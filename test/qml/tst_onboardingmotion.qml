// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/onboarding"
import "../../qml/controls"

TestCase {
    name: "OnboardingMotion"
    when: windowShown
    width: 224
    height: 224

    Component {
        id: stillMotion
        OnboardingMotion { reducedMotion: true }
    }
    Component {
        id: animatedMotion
        OnboardingMotion { }
    }

    function test_reduced_motion_retains_each_visual_state() {
        const motion = createTemporaryObject(stillMotion, this)
        verify(motion !== null)
        compare(motion.assembly, 1)
        compare(motion.logoOpacity, 1)
        verify(motion.markPoint(0)[0] > motion.markPoint(28)[0] + 25)
        const scattered = Array.from({ length: 30 }, (_, index) => motion.scatterPoint(index))
        verify(scattered.some(point => point[0] < 25))
        verify(scattered.some(point => point[0] > 195))
        verify(scattered.some(point => point[1] < 25))
        verify(scattered.some(point => point[1] > 195))
        verify(findChild(motion, "onboardingMotionDot29") !== null)
        verify(findChild(motion, "onboardingMotionBlock9") !== null)

        motion.toNetwork()
        compare(motion.logoOpacity, 0)
        compare(motion.networkProgress, 1)
        const peer = motion.dotPoint(0)
        const ring = motion.networkPoint(0)
        verify(Math.abs(peer[0] - ring[0]) < 0.01)
        verify(Math.abs(peer[1] - ring[1]) < 0.01)

        motion.toBlocks()
        compare(motion.blocksProgress, 1)
        compare(motion.clockProgress, 0)
        compare(motion.clockDetailsOpacity, 0)
        const firstLink = findChild(motion, "onboardingMotionChainLink0")
        const lastLink = findChild(motion, "onboardingMotionChainLink8")
        verify(firstLink !== null && lastLink !== null)
        compare(firstLink.opacity, 1)
        compare(lastLink.opacity, 1)

        motion.toClock()
        compare(motion.clockProgress, 1)
        compare(motion.clockDetailsOpacity, 1)
        compare(firstLink.opacity, 0)

        motion.backToBlocks()
        compare(motion.blocksProgress, 1)
        compare(motion.clockProgress, 0)
        compare(motion.clockDetailsOpacity, 0)
        motion.backToNetwork()
        compare(motion.clockDetailsOpacity, 0)
        compare(motion.blocksProgress, 0)
        motion.toLogo()
        compare(motion.networkProgress, 0)
        compare(motion.logoOpacity, 1)
    }

    function test_colors_and_blocks_survive_clock_transition() {
        const motion = createTemporaryObject(stillMotion, this)
        motion.toNetwork()
        compare(findChild(motion, "onboardingMotionDot27").color, Theme.color.orange)
        compare(findChild(motion, "onboardingMotionDot1").color, Theme.color.neutral9)
        compare(findChild(motion, "onboardingMotionSignal0").color, motion.signalColor)
        motion.toBlocks()
        const first = findChild(motion, "onboardingMotionBlock0")
        const last = findChild(motion, "onboardingMotionBlock9")
        compare(first.blockColor, motion.signalColor)
        compare(last.blockColor, Theme.color.orange)
        for (const progress of [0, 0.25, 0.5, 0.75, 1]) {
            motion.clockProgress = progress
            compare(first.opacity, 1)
            compare(last.opacity, 1)
            for (let i = 0; i < 10; ++i) {
                const point = motion.blockPoint(i)
                verify(Number.isFinite(point[0]) && Number.isFinite(point[1]))
            }
        }
        motion.toClock()
        compare(first.opacity, 1)
        compare(last.opacity, 1)
        compare(last.blockColor, Theme.color.orange)
        const tip = motion.blockPoint(9)
        compare(tip[0], motion.clockPoint(9)[0])
        compare(tip[1], motion.clockPoint(9)[1])
    }

    function test_clock_represents_half_a_day_from_midnight() {
        const motion = createTemporaryObject(stillMotion, this)
        const intervals = JSON.stringify(motion.clockSegments)
        compare(motion.clockSegments.length, 24)
        let elapsed = 0
        const lengths = new Set()
        for (const segment of motion.clockSegments) {
            compare(segment.startMinute, elapsed)
            verify(segment.durationMinutes >= 21 && segment.durationMinutes <= 39)
            lengths.add(segment.durationMinutes)
            elapsed += segment.durationMinutes
            compare(segment.endMinute, elapsed)
        }
        compare(elapsed, 12 * 60)
        for (let i = 0; i < 24; ++i) {
            compare(motion.clockSegmentColor(i), i === 23 ? motion.latestBlockColor
                : i >= 20 ? Theme.color.orange : motion.signalColor)
        }
        verify(lengths.size > 1)
        motion.toClock()
        compare(motion.minuteAngle(0), -Math.PI / 2)
        compare(motion.minuteAngle(720), Math.PI / 2)
        for (const side of [-1, 0, 1]) {
            const midnight = motion.blockBoundaryPoint(0, 0, side)
            verify(Math.abs(midnight[0] - 112) < 0.0001)
            verify(midnight[1] < 112)
            const noon = motion.blockBoundaryPoint(9, 720, side)
            verify(Math.abs(noon[0] - 112) < 0.0001)
            verify(noon[1] > 112)
        }
        motion.backToBlocks()
        motion.toClock()
        compare(JSON.stringify(motion.clockSegments), intervals)
    }

    function test_interrupted_return_can_reenter_network() {
        const motion = createTemporaryObject(animatedMotion, this)
        motion.reducedMotion = true
        motion.toClock()
        motion.reducedMotion = false
        motion.toLogo()
        wait(100)
        motion.toNetwork()
        tryCompare(motion, "networkProgress", 1, 1800)
        compare(motion.blocksProgress, 0)
        compare(motion.clockProgress, 0)
        compare(motion.clockDetailsOpacity, 0)
    }

    function test_intro_drift_overlaps_logo_assembly() {
        const motion = createTemporaryObject(animatedMotion, this)
        verify(motion !== null)
        const initial = motion.dotPoint(0)
        wait(300)
        const drifting = motion.dotPoint(0)
        verify(Math.hypot(drifting[0] - initial[0], drifting[1] - initial[1]) > 1)
        verify(motion.driftProgress < 1)
        verify(motion.assembly > 0.001)
        compare(motion.logoOpacity, 0)
        wait(300)
        compare(motion.driftProgress, 1)
        verify(motion.assembly < 1)
        wait(1000)
        compare(motion.logoOpacity, 0)
        tryVerify(function() { return motion.logoOpacity > 0.01 }, 600)
        tryCompare(motion, "logoOpacity", 1, 1000)
    }

    function test_animated_handoff_reaches_each_page_state() {
        const motion = createTemporaryObject(animatedMotion, this)
        verify(motion !== null)
        motion.toNetwork() // Also verifies an early Next can interrupt the intro.
        tryCompare(motion, "logoOpacity", 0, 1000)
        tryCompare(motion, "networkProgress", 1, 1800)
        motion.toBlocks()
        tryCompare(motion, "blocksProgress", 1, 1500)
        compare(motion.clockProgress, 0)
        motion.toClock()
        tryVerify(function() { return motion.clockDetailsOpacity > 0.05 && motion.clockProgress < 0.999 }, 1300)
        tryCompare(motion, "clockProgress", 1, 1500)
        tryCompare(motion, "clockDetailsOpacity", 1, 1500)
    }
}
