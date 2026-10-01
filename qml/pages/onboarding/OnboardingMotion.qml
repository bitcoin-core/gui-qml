// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "../../controls"

// One persistent visual shared by the cover, network, blockchain, and clock pages.
Item {
    id: root
    objectName: "onboardingMotion"
    width: 224
    height: 224

    property bool reducedMotion: false
    property real driftProgress: 0
    property real assembly: 0
    property real networkProgress: 0
    property real blocksProgress: 0
    property real clockProgress: 0
    property real clockDetailsOpacity: 0
    property real logoOpacity: 0
    property real travel: 0

    readonly property var markCells: [
        [2,0],[4,0],[2,1],[4,1],[1,2],[4,2],[5,2],[1,3],[5,3],[6,3],
        [1,4],[5,4],[6,4],[1,5],[4,5],[5,5],[1,6],[5,6],[6,6],[1,7],
        [5,7],[6,7],[1,8],[4,8],[5,8],[2,9],[3,9],[4,9],[2,10],[4,10]
    ]
    readonly property var scatterPoints: [
        [34,151],[194,42],[157,49],[41,96],[130,110],[169,13],
        [81,54],[87,181],[89,213],[200,174],[144,149],[194,14],
        [158,181],[66,91],[104,155],[99,81],[90,133],[88,24],
        [179,205],[25,20],[115,33],[65,155],[16,77],[167,99],
        [156,124],[140,19],[192,73],[33,201],[208,139],[165,74]
    ]
    readonly property var links: [
        [27,5],[5,14],[14,21],[21,0],[0,12],[2,17],
        [6,24],[8,26],[10,28],[13,29],[16,3],[22,11]
    ]
    readonly property var signalLinks: [0, 1, 2, 3]
    readonly property color signalColor: "#F9D63C"
    readonly property color latestBlockColor: "#ED6E46"
    readonly property real clockSegmentGapMinutes: 6
    readonly property int minutesPerDay: 24 * 60
    readonly property int clockBlockCount: 24
    readonly property var clockSegments: makeClockSegments()
    readonly property var clockGroups: makeClockGroups()

    function makeClockSegments() {
        // Illustrative intervals, not live block data. Complementary pairs keep
        // the sample at exactly twelve hours; shuffle once with a fixed seed so
        // the dial never changes during animation or backward navigation.
        let seed = 8573
        function random() {
            seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0
            return seed / 4294967296
        }
        const durations = []
        const averageMinutes = minutesPerDay / 2 / clockBlockCount
        for (let i = 0; i < clockBlockCount / 2; ++i) {
            const minutes = averageMinutes * (7 + Math.floor(random() * 7)) / 10
            durations.push(minutes, 2 * averageMinutes - minutes)
        }
        for (let i = durations.length - 1; i > 0; --i) {
            const other = Math.floor(random() * (i + 1))
            const duration = durations[i]
            durations[i] = durations[other]
            durations[other] = duration
        }
        let elapsed = 0
        return durations.map(function(minutes) {
            const segment = { startMinute: elapsed, endMinute: elapsed + minutes,
                              durationMinutes: minutes }
            elapsed += minutes
            return segment
        })
    }
    function clockSegmentColor(index) {
        if (index === clockBlockCount - 1) return latestBlockColor
        if (index >= clockBlockCount - 4) return Theme.color.orange
        return signalColor
    }
    function makeClockGroups() {
        const groups = []
        for (let i = 0; i < 10; ++i) {
            const first = Math.floor(i * clockBlockCount / 10)
            const end = Math.floor((i + 1) * clockBlockCount / 10)
            groups.push({ first: first, end: end,
                startMinute: clockSegments[first].startMinute,
                endMinute: clockSegments[end - 1].endMinute })
        }
        return groups
    }
    readonly property real networkOpacity: Math.max(0, Math.min(1,
        (networkProgress - 0.3) / 0.7)) * (1 - blocksProgress)

    function clamp(value) { return Math.max(0, Math.min(1, value)) }
    function eased(value) { return value * value * (3 - 2 * value) }
    function scatterPoint(index) { return scatterPoints[index] }
    function driftOffset(index) {
        const phase = index * 2.39996323
        const speed = 0.8 + (index % 5) * 0.17
        return [9 * (Math.sin(phase + driftProgress * speed) - Math.sin(phase)),
                9 * (Math.cos(phase * 1.7 + driftProgress * speed) - Math.cos(phase * 1.7))]
    }
    function markPoint(index) {
        const cell = markCells[index]
        const x = (cell[0] - 3.5) * 15
        const y = (cell[1] - 5) * 15
        const angle = 14 * Math.PI / 180
        return [118 + x * Math.cos(angle) - y * Math.sin(angle),
                112 + x * Math.sin(angle) + y * Math.cos(angle)]
    }
    function networkPoint(index) {
        const angle = -Math.PI / 2 + index * 2 * Math.PI / 30
        return [112 + Math.cos(angle) * 91, 112 + Math.sin(angle) * 91]
    }
    function chainPoint(index) { return [26 + index * 19.1, 112] }
    function minuteAngle(minutes) { return -Math.PI / 2 + minutes / minutesPerDay * 2 * Math.PI }
    function clockPoint(index) {
        const group = clockGroups[index]
        const angle = minuteAngle((group.startMinute + group.endMinute) / 2)
        return [112 + Math.cos(angle) * 91, 112 + Math.sin(angle) * 91]
    }
    function blockProgress() { return eased(clockProgress) }
    function blockBoundaryPoint(index, minute, side) {
        // Grow the same straight chain into a half-day arc. Its leading edge,
        // rather than the first block's center, lands exactly at midnight.
        const progress = blockProgress()
        const group = clockGroups[index]
        const sourceDistance = index * 19.1 + 12 * (minute - group.startMinute)
            / (group.endMinute - group.startMinute)
        const targetDistance = minute / minutesPerDay * 2 * Math.PI * 91
        const distance = sourceDistance + (targetDistance - sourceDistance) * progress
        const length = 183.9 + (Math.PI * 91 - 183.9) * progress
        const curvature = Math.PI * progress / length
        const angle = distance * curvature
        const startX = 20 + 92 * progress
        const startY = 112 - 91 * progress - 16 * Math.sin(progress * Math.PI)
        const thickness = side * (6 - 4 * progress)
        return [startX + (curvature > 0.0001 ? Math.sin(angle) / curvature : distance)
                    - thickness * Math.sin(angle),
                startY + (curvature > 0.0001 ? (1 - Math.cos(angle)) / curvature : 0)
                    + thickness * Math.cos(angle)]
    }
    function blockPoint(index) {
        const group = clockGroups[index]
        return blockBoundaryPoint(index, (group.startMinute + group.endMinute) / 2, 0)
    }
    function paintBlockSegment(ctx, index, startMinute, endMinute, color) {
        const progress = blockProgress()
        const group = clockGroups[index]
        const sourceScale = 12 / (group.endMinute - group.startMinute)
        const targetScale = 2 * Math.PI * 91 / minutesPerDay
        const scale = sourceScale + (targetScale - sourceScale) * progress
        const radius = 1.2
        const cornerMinutes = Math.min((endMinute - startMinute) / 3, radius / scale)
        const cornerSide = radius / (6 - 4 * progress)
        function point(minute, side) { return blockBoundaryPoint(index, minute, side) }
        function line(minute, side) {
            const p = point(minute, side)
            ctx.lineTo(p[0], p[1])
        }
        function corner(controlMinute, controlSide, endMinute, endSide) {
            const control = point(controlMinute, controlSide)
            const end = point(endMinute, endSide)
            ctx.quadraticCurveTo(control[0], control[1], end[0], end[1])
        }
        const first = point(startMinute + cornerMinutes, -1)
        ctx.beginPath()
        ctx.moveTo(first[0], first[1])
        for (let step = 1; step <= 12; ++step)
            line(startMinute + cornerMinutes + (endMinute - startMinute - 2 * cornerMinutes) * step / 12, -1)
        corner(endMinute, -1, endMinute, -1 + cornerSide)
        line(endMinute, 1 - cornerSide)
        corner(endMinute, 1, endMinute - cornerMinutes, 1)
        for (let step = 1; step <= 12; ++step)
            line(endMinute - cornerMinutes - (endMinute - startMinute - 2 * cornerMinutes) * step / 12, 1)
        corner(startMinute, 1, startMinute, 1 - cornerSide)
        line(startMinute, -1 + cornerSide)
        corner(startMinute, -1, startMinute + cornerMinutes, -1)
        ctx.closePath()
        ctx.fillStyle = color
        // Round inward, keeping the first edge exactly at midnight and all
        // corners inside their intervals rather than extending caps into gaps.
        ctx.fill()
    }
    function dotPoint(index) {
        const scatter = scatterPoint(index)
        const drift = driftOffset(index)
        const mark = markPoint(index)
        const peer = networkPoint(index)
        const block = chainPoint(Math.floor(index / 3))
        // A few lead particles arrive first; the followers settle on curved
        // paths without changing the intro's overall duration.
        const delay = index % 6 === 0 ? 0 : 0.035 + (index % 5) * 0.018
        const gathered = eased(clamp((assembly - delay) / (1 - delay)))
        const connected = eased(networkProgress)
        const blocked = eased(blocksProgress)
        const startX = scatter[0] + drift[0]
        const startY = scatter[1] + drift[1]
        const bend = Math.sin(gathered * Math.PI) * (index % 2 === 0 ? 1 : -1) * 0.14
        const x = startX + (mark[0] - startX) * gathered - (mark[1] - startY) * bend
        const y = startY + (mark[1] - startY) * gathered + (mark[0] - startX) * bend
        const nx = x + (peer[0] - x) * connected
        const ny = y + (peer[1] - y) * connected
        return [nx + (block[0] - nx) * blocked,
                ny + (block[1] - ny) * blocked]
    }

    function stopTransitions() {
        intro.stop()
        toNetworkAnimation.stop()
        toBlocksAnimation.stop()
        toClockAnimation.stop()
        toLogoAnimation.stop()
        backToNetworkAnimation.stop()
        backToBlocksAnimation.stop()
    }
    function playIntro() {
        stopTransitions()
        if (reducedMotion) {
            assembly = 1
            logoOpacity = 1
        } else {
            intro.start()
        }
    }
    function toNetwork() {
        stopTransitions()
        if (reducedMotion) {
            assembly = 1
            logoOpacity = 0
            networkProgress = 1
            blocksProgress = 0
            clockProgress = 0
            clockDetailsOpacity = 0
        } else {
            toNetworkAnimation.start()
        }
    }
    function toLogo() {
        stopTransitions()
        if (reducedMotion) {
            assembly = 1
            networkProgress = 0
            blocksProgress = 0
            clockProgress = 0
            clockDetailsOpacity = 0
            logoOpacity = 1
        } else {
            toLogoAnimation.start()
        }
    }
    function toClock() {
        stopTransitions()
        if (reducedMotion) {
            assembly = 1
            logoOpacity = 0
            networkProgress = 1
            blocksProgress = 1
            clockProgress = 1
            clockDetailsOpacity = 1
        } else {
            toClockAnimation.start()
        }
    }
    function toBlocks() {
        stopTransitions()
        if (reducedMotion) {
            assembly = 1
            logoOpacity = 0
            networkProgress = 1
            blocksProgress = 1
            clockProgress = 0
            clockDetailsOpacity = 0
        } else {
            toBlocksAnimation.start()
        }
    }
    function backToNetwork() {
        stopTransitions()
        if (reducedMotion) {
            assembly = 1
            logoOpacity = 0
            networkProgress = 1
            clockDetailsOpacity = 0
            clockProgress = 0
            blocksProgress = 0
        } else {
            backToNetworkAnimation.start()
        }
    }
    function backToBlocks() {
        stopTransitions()
        if (reducedMotion) {
            assembly = 1
            logoOpacity = 0
            networkProgress = 1
            blocksProgress = 1
            clockDetailsOpacity = 0
            clockProgress = 0
        } else {
            backToBlocksAnimation.start()
        }
    }

    Component.onCompleted: playIntro()

    Canvas {
        id: networkLines
        anchors.fill: parent
        opacity: root.networkOpacity
        property color ringColor: Theme.color.neutral3
        property color lineColor: Theme.color.neutral5
        property color activeLineColor: Theme.color.neutral6
        onRingColorChanged: requestPaint()
        onLineColorChanged: requestPaint()
        onActiveLineColorChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.beginPath()
            ctx.arc(112, 112, 91, 0, 2 * Math.PI)
            ctx.strokeStyle = ringColor
            ctx.lineWidth = 1
            ctx.stroke()
            for (let i = 0; i < root.links.length; ++i) {
                const link = root.links[i]
                const a = root.networkPoint(link[0])
                const b = root.networkPoint(link[1])
                ctx.beginPath()
                ctx.moveTo(a[0], a[1])
                ctx.lineTo(b[0], b[1])
                ctx.strokeStyle = root.signalLinks.indexOf(i) >= 0
                    ? activeLineColor : lineColor
                ctx.stroke()
            }
        }
    }

    Repeater {
        model: 30
        delegate: Rectangle {
            objectName: "onboardingMotionDot" + index
            readonly property var point: root.dotPoint(index)
            width: 9
            height: 9
            radius: width / 2
            antialiasing: true
            x: point[0] - width / 2
            y: point[1] - height / 2
            color: index % 6 === 0 || index === 27 ? Theme.color.orange : Theme.color.neutral9
            opacity: 1 - Math.max(0, (root.blocksProgress - 0.68) / 0.32)
        }
    }

    Repeater {
        model: root.signalLinks.length
        delegate: Rectangle {
            readonly property var link: root.links[root.signalLinks[index]]
            readonly property var start: root.networkPoint(link[0])
            readonly property var end: root.networkPoint(link[1])
            objectName: "onboardingMotionSignal" + index
            readonly property real phase: root.travel - index
            readonly property real position: root.clamp(phase)
            width: 4
            height: 4
            radius: 2
            x: start[0] + (end[0] - start[0]) * position - width / 2
            y: start[1] + (end[1] - start[1]) * position - height / 2
            color: root.signalColor
            opacity: root.networkOpacity * (phase >= 0 && phase < 1 ? 1 : 0)
        }
    }

    Image {
        anchors.fill: parent
        source: "qrc:/icons/onboarding-bitcoin.svg"
        sourceSize.width: 224
        sourceSize.height: 224
        fillMode: Image.PreserveAspectFit
        opacity: root.logoOpacity
    }

    Canvas {
        id: clockDial
        anchors.fill: parent
        opacity: root.clockDetailsOpacity
        property color ringColor: Theme.color.neutral3
        onRingColorChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.beginPath()
            ctx.arc(112, 112, 91, 0, 2 * Math.PI)
            ctx.strokeStyle = ringColor
            ctx.lineWidth = 4
            ctx.stroke()
        }
    }

    Repeater {
        model: 9
        delegate: Rectangle {
            objectName: "onboardingMotionChainLink" + index
            readonly property var start: root.blockPoint(index)
            readonly property var end: root.blockPoint(index + 1)
            x: start[0]
            y: start[1] - 1
            width: Math.hypot(end[0] - start[0], end[1] - start[1])
            rotation: Math.atan2(end[1] - start[1], end[0] - start[0]) * 180 / Math.PI
            transformOrigin: Item.Left
            height: 2
            color: Theme.color.neutral5
            opacity: Math.max(0, Math.min(1, (root.blocksProgress - 0.62) / 0.38))
                * (1 - Math.min(1, root.clockProgress / 0.45))
                * (1 - root.clockDetailsOpacity)
        }
    }

    Repeater {
        model: 10
        delegate: Canvas {
            objectName: "onboardingMotionBlock" + index
            anchors.fill: parent
            readonly property real progress: root.blockProgress()
            readonly property color blockColor: index === 9 ? Theme.color.orange : root.signalColor
            opacity: root.clamp((root.blocksProgress - 0.62) / 0.38)
            onProgressChanged: requestPaint()
            onBlockColorChanged: requestPaint()
            readonly property color orangeColor: Theme.color.orange
            readonly property color latestColor: root.latestBlockColor
            onOrangeColorChanged: requestPaint()
            onLatestColorChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                const group = root.clockGroups[index]
                const split = root.eased(root.clamp((progress - 0.25) / 0.75))
                if (split === 0) {
                    root.paintBlockSegment(ctx, index, group.startMinute, group.endMinute, blockColor)
                    return
                }
                // Each large chain tile opens into its share of the 24 segments.
                // Small gaps separate intervals without shifting the day's start.
                for (let segmentIndex = group.first; segmentIndex < group.end; ++segmentIndex) {
                    const segment = root.clockSegments[segmentIndex]
                    const start = segment.startMinute + (segmentIndex === 0 ? 0 : root.clockSegmentGapMinutes / 2 * split)
                    const end = segment.endMinute - (segmentIndex === root.clockBlockCount - 1 ? 0 : root.clockSegmentGapMinutes / 2 * split)
                    const targetColor = root.clockSegmentColor(segmentIndex)
                    const segmentColor = Qt.rgba(
                        blockColor.r + (targetColor.r - blockColor.r) * split,
                        blockColor.g + (targetColor.g - blockColor.g) * split,
                        blockColor.b + (targetColor.b - blockColor.b) * split, 1)
                    root.paintBlockSegment(ctx, index, start, end, segmentColor)
                }
            }
        }
    }

    Image {
        anchors.centerIn: parent
        width: 182
        height: 182
        source: Theme.dark ? "qrc:/icons/onboarding-clock-face-dark.svg"
            : "qrc:/icons/onboarding-clock-face-light.svg"
        sourceSize.width: 400
        sourceSize.height: 400
        fillMode: Image.PreserveAspectFit
        opacity: root.clockDetailsOpacity
    }

    SequentialAnimation {
        running: root.visible && !root.reducedMotion && root.networkProgress > 0.99
            && root.blocksProgress < 0.01
        loops: Animation.Infinite
        NumberAnimation {
            target: root
            property: "travel"
            from: 0
            to: root.signalLinks.length
            duration: 1450
            easing.type: Easing.Linear
        }
        PauseAnimation { duration: 2300 }
    }

    ParallelAnimation {
        id: intro
        NumberAnimation { target: root; property: "driftProgress"; to: 1; duration: 500; easing.type: Easing.InOutSine }
        NumberAnimation { target: root; property: "assembly"; to: 1; duration: 2000; easing.type: Easing.InOutCubic }
        SequentialAnimation {
            PauseAnimation { duration: 1700 }
            NumberAnimation { target: root; property: "logoOpacity"; to: 1; duration: 700; easing.type: Easing.InOutCubic }
        }
    }
    SequentialAnimation {
        id: toNetworkAnimation
        ParallelAnimation {
            NumberAnimation { target: root; property: "assembly"; to: 1; duration: 250; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "logoOpacity"; to: 0; duration: 250; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "blocksProgress"; to: 0; duration: 250; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "clockProgress"; to: 0; duration: 250; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "clockDetailsOpacity"; to: 0; duration: 250; easing.type: Easing.InOutCubic }
        }
        NumberAnimation { target: root; property: "networkProgress"; to: 1; duration: 1050; easing.type: Easing.InOutCubic }
    }
    SequentialAnimation {
        id: toLogoAnimation
        ParallelAnimation {
            NumberAnimation { target: root; property: "assembly"; to: 1; duration: 1000; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "networkProgress"; to: 0; duration: 1000; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "blocksProgress"; to: 0; duration: 1000; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "clockProgress"; to: 0; duration: 1000; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "clockDetailsOpacity"; to: 0; duration: 300; easing.type: Easing.InOutCubic }
        }
        NumberAnimation { target: root; property: "logoOpacity"; to: 1; duration: 300; easing.type: Easing.InOutCubic }
    }
    SequentialAnimation {
        id: toBlocksAnimation
        ParallelAnimation {
            NumberAnimation { target: root; property: "assembly"; to: 1; duration: 200; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "networkProgress"; to: 1; duration: 200; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "logoOpacity"; to: 0; duration: 200; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "clockProgress"; to: 0; duration: 200; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "clockDetailsOpacity"; to: 0; duration: 200; easing.type: Easing.InOutCubic }
        }
        NumberAnimation { target: root; property: "blocksProgress"; to: 1; duration: 1100; easing.type: Easing.InOutCubic }
    }
    SequentialAnimation {
        id: toClockAnimation
        ParallelAnimation {
            NumberAnimation { target: root; property: "assembly"; to: 1; duration: 150; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "logoOpacity"; to: 0; duration: 150; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "networkProgress"; to: 1; duration: 150; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "blocksProgress"; to: 1; duration: 150; easing.type: Easing.InOutCubic }
        }
        ParallelAnimation {
            NumberAnimation { target: root; property: "clockProgress"; to: 1; duration: 1150; easing.type: Easing.InOutCubic }
            SequentialAnimation {
                PauseAnimation { duration: 700 }
                NumberAnimation { target: root; property: "clockDetailsOpacity"; to: 1; duration: 450; easing.type: Easing.InOutCubic }
            }
        }
    }
    ParallelAnimation {
        id: backToNetworkAnimation
        NumberAnimation { target: root; property: "assembly"; to: 1; duration: 1300; easing.type: Easing.InOutCubic }
        NumberAnimation { target: root; property: "logoOpacity"; to: 0; duration: 325; easing.type: Easing.InOutCubic }
        NumberAnimation { target: root; property: "networkProgress"; to: 1; duration: 1300; easing.type: Easing.InOutCubic }
        NumberAnimation { target: root; property: "clockDetailsOpacity"; to: 0; duration: 325; easing.type: Easing.InOutCubic }
        NumberAnimation { target: root; property: "clockProgress"; to: 0; duration: 1300; easing.type: Easing.InOutCubic }
        NumberAnimation { target: root; property: "blocksProgress"; to: 0; duration: 1300; easing.type: Easing.InOutCubic }
    }
    SequentialAnimation {
        id: backToBlocksAnimation
        NumberAnimation { target: root; property: "clockDetailsOpacity"; to: 0; duration: 250; easing.type: Easing.InOutCubic }
        ParallelAnimation {
            NumberAnimation { target: root; property: "assembly"; to: 1; duration: 1050; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "logoOpacity"; to: 0; duration: 250; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "networkProgress"; to: 1; duration: 1050; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "clockProgress"; to: 0; duration: 1050; easing.type: Easing.InOutCubic }
            NumberAnimation { target: root; property: "blocksProgress"; to: 1; duration: 1050; easing.type: Easing.InOutCubic }
        }
    }
}
