// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Shapes 1.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"
import "../../qml/components"

TestCase {
    name: "TransactionFlowRendering"
    when: windowShown
    visible: true
    width: 1180
    height: 900
    Component {
        id: flowComponent
        TransactionFlow {
            width: 1100
        }
    }
    Component {
        id: lineComponent
        Rectangle {
            id: line
            property var ribbon
            width: 220
            height: 700
            color: "black"
            Shape {
                anchors.fill: parent
                TransactionFlowRibbon {
                    ribbon: line.ribbon
                    sourceColor: "#c66bdd"
                    targetColor: "#ff9900"
                }
            }
        }
    }

    function entry(id, value, ownership) {
        return { id: id, kind: "output", amountSat: value, amountKnown: value !== null,
            ownership: ownership, address: "bcrt1qexampleaddress012345678901234567890", label: "", paymentRequests: [] }
    }
    function snapshot() {
        return { inputs: [entry("input:0", 100000, "wallet")],
            outputs: [entry("output:0", 75000, "external"), entry("output:1", 24200, "wallet")],
            complete: true, feeKnown: true, feeSat: 800, inputCount: 1, outputCount: 2 }
    }

    function test_known_amounts_render_gradient_ribbons() {
        const data = snapshot()
        data.inputs[0].amountSat = 10000000000
        data.outputs[0].amountSat = 1000000000
        data.outputs[1].amountSat = 8999999859
        data.feeSat = 141
        const flow = createTemporaryObject(flowComponent, this, { flow: data })
        verify(flow !== null)
        compare(flow.feeColor, Theme.color.blue)
        const shape = findChild(flow, "transactionFlowRibbons")
        for (const width of [1100, 720]) {
            flow.width = width
            verify(waitForRendering(flow))
            const image = grabImage(this)
            verify(image.width > 0 && image.height > 0, "The test platform must support image capture")
            for (const ribbon of flow.geometry.ribbons) {
                verify(ribbon.thickness >= 1)
                // Sample the thin fee near its horizontal endpoint, away from
                // the sloped edges. Check the other gradients at their midpoint.
                const thin = ribbon.thickness === 1
                const sampleX = thin ? ribbon.endX - 3 : (ribbon.startX + ribbon.endX) / 2
                const sampleY = thin ? ribbon.endY - 0.5 : (ribbon.startY + ribbon.endY) / 2
                const point = shape.mapToItem(this, sampleX, sampleY)
                const x = Math.floor(point.x * image.width / this.width)
                const y = Math.floor(point.y * image.height / this.height)
                const source = flow.ownershipColor(ribbon.sourceOwnership)
                const target = flow.ownershipColor(ribbon.targetOwnership)
                const progress = (sampleX - ribbon.startX) / (ribbon.endX - ribbon.startX)
                const expected = Qt.rgba(source.r + (target.r - source.r) * progress,
                    source.g + (target.g - source.g) * progress, source.b + (target.b - source.b) * progress, 1)
                const actual = image.pixel(x, y)
                // A 1px ribbon can partially cover a pixel. Account for its
                // coverage while still checking the gradient's color.
                const background = Theme.color.neutral1
                const coverage = thin ? (actual.b - background.b) / (expected.b - background.b) : 1
                verify(coverage > 0.25 && coverage <= 1.03, ribbon.id + " must remain visible")
                for (const channel of ["r", "g", "b", "a"]) {
                    const composited = background[channel] + (expected[channel] - background[channel]) * coverage
                    fuzzyCompare(actual[channel], composited, 0.03, ribbon.id + " must render its gradient")
                }
            }
        }
    }

    function test_steep_lines_remain_continuous_data() {
        return [
            { tag: "down", startY: 40, endY: 660, thickness: 1, proportional: true },
            { tag: "up", startY: 660, endY: 40, thickness: 1, proportional: true },
            { tag: "tall_distribution", startY: -15000, endY: 660, thickness: 64, proportional: true },
            { tag: "unknown", startY: 40, endY: 660, thickness: 4, proportional: false }
        ]
    }

    function test_steep_lines_remain_continuous(data) {
        const ribbon = Object.assign({ startX: 60, endX: 140 }, data)
        const line = createTemporaryObject(lineComponent, this, { ribbon: ribbon })
        verify(waitForRendering(line))
        const image = grabImage(line)
        verify(image.width > 0 && image.height > 0, "The test platform must support image capture")
        let checked = 0
        for (let i = 1; i < 512; ++i) {
            const t = i / 512, u = 1 - t
            const cx = u * u * u * 60 + 3 * u * t * 100 + t * t * t * 140
            const cy = data.startY + (data.endY - data.startY) * t * t * (3 - 2 * t)
            if (cy < 2 || cy > line.height - 2) continue
            const x = Math.floor(cx * image.width / line.width)
            const y = Math.floor(cy * image.height / line.height)
            let covered = false
            for (let dx = -1; dx <= 1; ++dx) {
                for (let dy = -1; dy <= 1; ++dy) {
                    if (image.pixel(x + dx, y + dy).r > 0.03) covered = true
                }
            }
            verify(covered, "Solid curve must have no gaps at t=" + t)
            checked++
        }
        verify(checked > 40)
    }
}
