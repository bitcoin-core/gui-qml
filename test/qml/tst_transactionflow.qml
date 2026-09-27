// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Shapes 1.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"
import "../../qml/components"
import "../../qml/components/TransactionFlowLayout.js" as FlowLayout

TestCase {
    name: "TransactionFlow"
    when: windowShown
    visible: true
    width: 1180
    height: 900
    Component {
        id: flowComponent
        TransactionFlow {
            width: 1100
            property int geometryUpdates: 0
            onGeometryChanged: geometryUpdates++
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

    function batchSnapshot(count, walletCount) {
        const data = { inputs: [entry("input:0", count * 10000 + 800, "wallet")], outputs: [],
            complete: true, feeKnown: true, feeSat: 800, inputCount: 1, outputCount: count }
        for (let i = 0; i < count; ++i) {
            data.outputs.push(entry("output:" + i, 10000, i < walletCount ? "wallet" : "external"))
        }
        return data
    }

    function test_output_grouping_threshold_data() {
        return [
            { tag: "below_threshold", count: 9, walletCount: 2, collapsed: false, visibleCount: 10 },
            { tag: "exactly_ten", count: 10, walletCount: 2, collapsed: false, visibleCount: 11 },
            { tag: "eleven", count: 11, walletCount: 2, collapsed: true, visibleCount: 4 },
            { tag: "all_external", count: 11, walletCount: 0, collapsed: true, visibleCount: 2 },
            { tag: "all_wallet", count: 11, walletCount: 11, collapsed: false, visibleCount: 12 },
            { tag: "one_external", count: 11, walletCount: 10, collapsed: false, visibleCount: 12 }
        ]
    }

    function test_output_grouping_threshold(data) {
        const flow = createTemporaryObject(flowComponent, this, { flow: batchSnapshot(data.count, data.walletCount) })
        verify(waitForRendering(flow))
        compare(findChild(flow, "transactionFlowOutputsToggle").visible, data.collapsed)
        compare(flow.outputEntries.length, data.visibleCount)
        compare(flow.outputEntries.filter(function(e) { return e.ownership === "wallet" }).length, data.walletCount)
        compare(flow.outputEntries[flow.outputEntries.length - 1].kind, "fee")
        verify(flow.geometry.proportional)
    }

    function test_late_large_snapshot_never_exposes_hidden_outputs() {
        const flow = createTemporaryObject(flowComponent, this, {flow: {}})
        verify(waitForRendering(flow))
        let peakOutputs = 0
        flow.outputEntriesChanged.connect(function() { peakOutputs = Math.max(peakOutputs, flow.outputEntries.length) })
        flow.flow = batchSnapshot(1001, 1)
        verify(waitForRendering(flow))
        compare(flow.outputEntries.length, 3)
        verify(peakOutputs <= 3, "A collapsed snapshot must not briefly instantiate all outputs")
        compare(findChild(flow, "transactionFlowOutput_0").entry.id, "output:0")
        compare(findChild(flow, "transactionFlowOutput_1").entry.outputCount, 1000)
        compare(findChild(flow, "transactionFlowOutput_3"), null)
        verify(flow.geometry.proportional)
    }

    function test_toggle_outputs_preserves_amounts_counts_and_details() {
        const data = batchSnapshot(12, 2)
        data.outputs[0].paymentRequests = [{ requestId: "91" }]
        data.outputs[2].label = "Recipient note"
        const flow = createTemporaryObject(flowComponent, this, { flow: data, transactionId: "first" })
        verify(waitForRendering(flow))
        const toggle = findChild(flow, "transactionFlowOutputsToggle")
        const counts = findChild(flow, "transactionFlowCounts")
        compare(toggle.text, "Show all outputs")
        compare(counts.text, "1 input · 12 outputs")
        compare(flow.outputEntries.length, 4)
        compare(findChild(flow, "transactionFlowOutput_0").requests[0].requestId, "91")
        let group = findChild(flow, "transactionFlowOutput_2")
        compare(group.title, "10 outputs")
        compare(group.entry.amountSat, 100000)
        compare(group.amountText, "0.00100000 BTC")
        const total = FlowLayout.sum(flow.outputEntries)
        verify(flow.geometry.proportional)

        mouseClick(toggle)
        verify(waitForRendering(flow))
        compare(toggle.text, "Collapse non-wallet outputs")
        compare(flow.outputEntries.length, 13)
        compare(counts.text, "1 input · 12 outputs")
        compare(findChild(flow, "transactionFlowOutput_2").title, "Recipient note")
        compare(FlowLayout.sum(flow.outputEntries), total)
        verify(flow.geometry.proportional)

        mouseClick(toggle)
        verify(waitForRendering(flow))
        compare(flow.outputEntries.length, 4)
        compare(toggle.text, "Show all outputs")
        flow.displayUnit = BitcoinAmount.SAT
        tryCompare(findChild(flow, "transactionFlowOutput_2"), "amountText", "100000 sats")
        compare(data.outputs.length, 12)
        compare(data.outputs[2].label, "Recipient note")

        // Refreshing this transaction preserves the choice; opening another
        // transaction restores the collapsed default.
        mouseClick(toggle)
        flow.flow = batchSnapshot(12, 2)
        compare(flow.outputsExpanded, true)
        flow.transactionId = "second"
        compare(flow.outputsExpanded, false)
        compare(flow.outputEntries.length, 4)
        flow.title = ""
        verify(toggle.visible)
    }

    function test_grouped_unknown_amount_is_not_a_partial_total() {
        const data = batchSnapshot(11, 1)
        data.outputs[2].amountKnown = false
        data.outputs[2].amountSat = null
        data.complete = false
        const flow = createTemporaryObject(flowComponent, this, { flow: data })
        verify(waitForRendering(flow))
        const group = findChild(flow, "transactionFlowOutput_1")
        compare(group.entry.amountKnown, false)
        compare(group.entry.amountSat, null)
        compare(group.amountText, "Unknown amount")
        const path = flow.ribbonObjects.find(function(r) { return r.ribbon.id === "out:non-wallet-outputs" })
        verify(!path.ribbon.proportional)
        compare(path.strokeWidth, 4)
    }

    function test_small_fees_keep_a_minimum_width_and_other_ribbons_stay_proportional() {
        const data = snapshot()
        const graph = FlowLayout.calculate(data.inputs, FlowLayout.outputs(data), [80], [80, 80, 80], 1052, true)
        verify(graph.proportional)
        compare(graph.ribbons.length, 4)
        compare(graph.ribbons[0].thickness, 64)
        compare(graph.ribbons[3].thickness, 1)
        fuzzyCompare(graph.ribbons[1].thickness / graph.ribbons[2].thickness, 75000 / 24200, 0.00001)
        fuzzyCompare(graph.ribbons.slice(1).reduce(function(sum, r) { return sum + r.thickness }, 0), 64, 0.00001)
        const mixedInputs = [entry("in:0", 75000, "wallet"), entry("in:1", 25000, "external")]
        const mixed = FlowLayout.calculate(mixedInputs, FlowLayout.outputs(data), [80, 80], [80, 80, 80], 1052, true)
        compare(mixed.ribbons[2].sourceOwnership, "unknown")
    }

    function test_small_inputs_and_large_batches_preserve_the_junction_width() {
        const inputs = [entry("tiny:0", 1, "wallet"), entry("tiny:1", 1, "wallet"), entry("large", 999998, "wallet")]
        const outputs = [entry("a", 500000, "external"), entry("b", 500000, "wallet"), entry("zero", 0, "external")]
        const graph = FlowLayout.calculate(inputs, outputs, [], [], 1052, true)
        compare(graph.ribbons.length, 5)
        compare(graph.ribbons[0].thickness, 1)
        compare(graph.ribbons[1].thickness, 1)
        compare(graph.ribbons[2].thickness, 62)
        compare(graph.ribbons[3].thickness, 32)
        compare(graph.ribbons[4].thickness, 32)

        const batch = []
        for (let i = 0; i < 80; ++i) batch.push(entry("output:" + i, 1, "external"))
        const crowded = FlowLayout.calculate([entry("input", 80, "wallet")], batch, [], [], 1052, true)
        compare(crowded.ribbons[0].thickness, 80)
        crowded.ribbons.slice(1).forEach(function(ribbon) { compare(ribbon.thickness, 1) })
    }

    function test_unknown_and_zero_amounts_do_not_invent_values() {
        const data = snapshot()
        data.inputs[0].amountSat = null
        data.inputs[0].amountKnown = false
        data.feeKnown = false
        data.complete = false
        const graph = FlowLayout.calculate(data.inputs, FlowLayout.outputs(data), [80], [80, 80, 80], 800, false)
        verify(!graph.proportional)
        compare(FlowLayout.outputs(data)[2].amountSat, null)
        const input = graph.ribbons.find(function(r) { return r.id === "in:input:0" })
        const fee = graph.ribbons.find(function(r) { return r.id === "out:fee" })
        const received = graph.ribbons.find(function(r) { return r.id === "out:output:0" })
        const change = graph.ribbons.find(function(r) { return r.id === "out:output:1" })
        verify(!input.proportional)
        verify(input.inferredWidth)
        fuzzyCompare(input.thickness, received.thickness + change.thickness + fee.thickness, 0.001)
        verify(!fee.proportional)
        compare(fee.thickness, 2)
        verify(received.proportional)
        verify(change.proportional)
        fuzzyCompare(received.thickness / change.thickness, 75000 / 24200, 0.00001)
        graph.ribbons.forEach(function(r) { verify(FlowLayout.path(r).indexOf("NaN") === -1) })
        const zero = entry("data", 0, "external")
        const zeroGraph = FlowLayout.calculate([zero], [zero], [80], [80], 800, true)
        verify(!zeroGraph.proportional)
        verify(isFinite(zeroGraph.height))
        compare(zeroGraph.ribbons.length, 0)
    }

    function test_unknown_connections_are_solid_gray_and_known_outputs_keep_ribbons() {
        const data = snapshot()
        data.inputs[0].amountKnown = false
        data.inputs[0].amountSat = null
        data.feeKnown = false
        data.complete = false
        const flow = createTemporaryObject(flowComponent, this, { flow: data })
        verify(flow !== null)
        tryVerify(function() { return flow.ribbonObjects.length === 4 })
        const input = flow.ribbonObjects.find(function(r) { return r.ribbon.id === "in:input:0" })
        const fee = flow.ribbonObjects.find(function(r) { return r.ribbon.id === "out:fee" })
        const output = flow.ribbonObjects.find(function(r) { return r.ribbon.id === "out:output:0" })
        compare(input.strokeWidth, -1)
        verify(input.ribbon.inferredWidth)
        verify(!input.ribbon.proportional)
        compare(input.fillColor, input.sourceColor)
        compare(input.fillGradient, null)
        const outputWidth = flow.geometry.ribbons.filter(function(r) { return r.id.indexOf("out:") === 0 })
            .reduce(function(total, r) { return total + r.thickness }, 0)
        fuzzyCompare(input.ribbon.thickness, outputWidth, 0.001)
        compare(flow.flow.inputs[0].amountSat, null)
        verify(!flow.geometry.amountsKnown)
        compare(fee.strokeWidth, 2)
        for (const line of [fee]) {
            compare(line.strokeStyle, ShapePath.SolidLine)
            compare(line.strokeColor, Theme.color.neutral3)
            compare(line.fillGradient, null)
        }
        verify(output.ribbon.proportional)
        verify(output.fillGradient !== null)
        compare(output.strokeWidth, -1)
    }

    function test_unknown_inputs_share_remaining_band_equally() {
        const data = snapshot()
        data.inputs = [entry("known", 20000, "wallet"),
            entry("unknown-wallet", 0, "wallet"), entry("unknown-external", 0, "external")]
        for (let i = 1; i < data.inputs.length; ++i) {
            data.inputs[i].amountKnown = false
            data.inputs[i].amountSat = null
        }
        data.feeKnown = false
        data.complete = false
        const flow = createTemporaryObject(flowComponent, this, { flow: data })
        tryVerify(function() { return flow.ribbonObjects.length === 6 })
        const inputs = flow.ribbonObjects.filter(function(r) { return r.ribbon.id.indexOf("in:") === 0 })
        const outputs = flow.geometry.ribbons.filter(function(r) { return r.id.indexOf("out:") === 0 })
        compare(inputs[1].ribbon.thickness, inputs[2].ribbon.thickness)
        fuzzyCompare(inputs.reduce(function(total, r) { return total + r.ribbon.thickness }, 0),
            outputs.reduce(function(total, r) { return total + r.thickness }, 0), 0.001)
        for (let i = 1; i < inputs.length; ++i) {
            verify(inputs[i].ribbon.inferredWidth)
            verify(!inputs[i].ribbon.proportional)
            compare(inputs[i].fillColor, findChild(flow, "transactionFlowInput_" + i).accentColor)
            compare(inputs[i].fillGradient, null)
            compare(data.inputs[i].amountSat, null)
            verify(FlowLayout.path(inputs[i].ribbon).endsWith(" Z"))
        }
        verify(inputs[0].ribbon.proportional)
        verify(!flow.geometry.amountsKnown)
    }

    function test_component_resizes_and_request_cards_stay_connected() {
        const data = snapshot()
        data.inputs[0].label = "Savings"
        data.outputs[1].paymentRequests = [{ requestId: "91" }, { requestId: "92" }]
        const flow = createTemporaryObject(flowComponent, this, { flow: data })
        verify(flow !== null)
        tryVerify(function() { return flow.ribbonObjects.length === 4 })
        waitForRendering(flow)
        const input = findChild(flow, "transactionFlowInput_0")
        compare(input.title, "Savings")
        verify(!input.compact)
        const inputHeight = input.height
        const recipient = findChild(flow, "transactionFlowOutput_0")
        const received = findChild(flow, "transactionFlowOutput_1")
        verify(received.height > recipient.height)
        const oldX = flow.geometry.outputX
        flow.width = 720
        tryVerify(function() { return flow.geometry.outputX < oldX })
        verify(input.compact)
        tryVerify(function() { return input.height > inputHeight })
        fuzzyCompare(flow.geometry.ribbons[0].startX, input.x + input.width, 0.01)
        // Card measurements are batched after the compact layout settles.
        tryVerify(function() { return Math.abs(flow.geometry.ribbons[0].startY - (input.y + input.height / 2)) < 0.01 })
        const incoming = flow.geometry.ribbons[2]
        fuzzyCompare(incoming.endX, received.x, 0.01)
        fuzzyCompare(incoming.endY, received.y + received.height / 2, 0.01)
        verify(flow.geometry.outputs[2].y >= received.y + received.height + 12)
        flow.width = 380
        verify(findChild(flow, "transactionFlowViewport").contentWidth > 300)
        verify(flow.geometry.proportional)
    }

    function test_replaces_snapshot_without_stale_paths() {
        const flow = createTemporaryObject(flowComponent, this, { flow: snapshot() })
        verify(flow !== null)
        tryVerify(function() { return flow.ribbonObjects.length === 4 })
        const data = snapshot()
        data.inputs[0].amountKnown = false
        data.inputs[0].amountSat = null
        data.complete = false
        flow.flow = data
        tryVerify(function() { return !flow.geometry.proportional })
        verify(findChild(flow, "transactionFlowUnknownNotice").visible)
        flow.flow = { inputs: [], outputs: [], coinbase: true }
        tryVerify(function() { return flow.ribbonObjects.length === 0 })
        compare(flow.geometry.height, 0)
        flow.flow = snapshot()
        tryVerify(function() { return flow.ribbonObjects.length === 4 })
        compare(flow.ribbonObjects[0].ribbon.id, "in:input:0")
        verify(flow.geometry.proportional)
    }

    function test_large_transaction_keeps_ribbons_when_cards_resize() {
        // Large distributions must not recreate every ribbon as each card is
        // measured. This previously froze the macOS UI with 328 outputs.
        const data = { inputs: [entry("input:0", null, "external")], outputs: [], complete: false, feeKnown: false }
        for (let i = 0; i < 328; ++i) {
            const output = entry("output:" + i, 10000 + i, "external")
            output.amount = (output.amountSat / 100000000).toFixed(8) + " BTC"
            data.outputs.push(output)
        }
        data.outputs[1].amountSat = 0
        data.outputs[1].amount = "0.00000000 BTC"
        data.outputs[1].kind = "data"
        data.outputs[1].address = ""
        data.outputs[0].paymentRequests = [{ requestId: "1" }]
        const flow = createTemporaryObject(flowComponent, this, { flow: data })
        verify(flow !== null)
        verify(waitForRendering(flow))
        compare(flow.outputEntries.length, 2)
        mouseClick(findChild(flow, "transactionFlowOutputsToggle"))
        verify(waitForRendering(flow))
        compare(flow.outputEntries.length, 329)
        compare(findChild(flow, "transactionFlowOutput_1").entry.kind, "data")
        const paths = flow.ribbonObjects.slice()
        compare(paths.length, flow.geometry.ribbons.length)
        verify(flow.geometryUpdates < 15, "Card measurements must be batched during creation and expansion")
        const oldHeight = flow.geometry.height
        flow.width = 720
        verify(waitForRendering(flow))
        tryVerify(function() { return flow.geometry.height > oldHeight })
        verify(flow.geometryUpdates < 25, "Resizing must not recalculate once per card")
        compare(flow.ribbonObjects.length, paths.length)
        for (let i = 0; i < paths.length; ++i) compare(flow.ribbonObjects[i], paths[i])
        const output = findChild(flow, "transactionFlowOutput_327")
        const path = flow.ribbonObjects.find(function(item) { return item.ribbon.id === "out:output:327" })
        fuzzyCompare(path.ribbon.endX, output.x, 0.01)
        fuzzyCompare(path.ribbon.endY, output.y + output.height / 2, 0.01)
        mouseClick(findChild(flow, "transactionFlowOutputsToggle"))
        verify(waitForRendering(flow))
        compare(flow.outputEntries.length, 2)
        compare(flow.ribbonObjects.length, flow.geometry.ribbons.length)
    }

    function test_data_output_content_data() {
        const text = "<b>Payment reference</b>\nA readable message stored in the transaction, long enough to wrap inside a narrow output card."
        return [
            { tag: "text", fields: { dataText: text }, expected: text },
            { tag: "binary", fields: { dataHex: "00ff0102" }, expected: "Hex: 00ff0102" },
            { tag: "script", fields: { scriptHex: "6a51026162" }, expected: "Script (hex): 6a51026162" },
            { tag: "empty", fields: { dataText: "" }, expected: "No data" }
        ]
    }

    function test_data_output_content(data) {
        const snapshotData = snapshot()
        snapshotData.outputs[0] = Object.assign(entry("output:0", 0, "external"),
            { kind: "data", address: "", dataType: "OP_RETURN" }, data.fields)
        const flow = createTemporaryObject(flowComponent, this, { flow: snapshotData })
        verify(waitForRendering(flow))
        const output = findChild(flow, "transactionFlowOutput_0")
        const pill = findChild(output, "transactionFlowDataTypePill")
        const content = findChild(output, "transactionFlowDataText")
        compare(output.accentColor, Theme.color.neutral8)
        const line = flow.ribbonObjects.find(function(r) { return r.ribbon.id === "out:output:0" })
        verify(line !== undefined)
        compare(line.strokeWidth, 1)
        compare(line.strokeColor, Theme.color.neutral2)
        compare(line.strokeStyle, ShapePath.SolidLine)
        compare(line.fillGradient, null)
        compare(pill.color, Theme.color.neutral3)
        verify(pill.visible)
        verify(content.visible)
        compare(content.text, data.expected)
        compare(content.textFormat, Text.PlainText)
        const oldHeight = output.height
        flow.width = 720
        verify(waitForRendering(flow))
        if (data.tag === "text") tryVerify(function() { return output.height > oldHeight })
        tryVerify(function() { return flow.geometry.outputs[1].y >= output.y + output.height + 12 })
    }
}
