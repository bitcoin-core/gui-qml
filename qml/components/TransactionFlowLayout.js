// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

.pragma library

function outputGroup(outputs) {
    const grouped = outputs.filter(function(entry) { return entry.ownership !== "wallet" })
    if (grouped.length < 2) return null
    const total = sum(grouped)
    return {
        id: "non-wallet-outputs", kind: "output-group", outputCount: grouped.length,
        ownership: grouped.every(function(entry) { return entry.ownership === "external" }) ? "external" : "unknown",
        amountKnown: total !== null, amountSat: total,
        address: "", label: "", paymentRequests: []
    }
}

function outputs(flow, group) {
    const entries = []
    let grouped = false
    // Replace the first non-wallet output with the summary, preserving the
    // order of wallet outputs. The fee is always a separate node.
    for (const entry of flow.outputs || []) {
        if (!group || entry.ownership === "wallet") entries.push(entry)
        else if (!grouped) {
            entries.push(group)
            grouped = true
        }
    }
    if (!flow.coinbase && entries.length > 0) entries.push({
        id: "fee", kind: "fee", ownership: "fee", amountKnown: !!flow.feeKnown,
        amountSat: flow.feeKnown ? flow.feeSat : null, amount: flow.feeAmount || "",
        address: "", label: "", paymentRequests: []
    })
    return entries
}

function amountKnown(entry) {
    return entry.amountKnown && entry.amountSat !== null && isFinite(entry.amountSat) && entry.amountSat >= 0
}

function sum(entries, knownOnly) {
    let total = 0
    for (let i = 0; i < entries.length; ++i) {
        const n = entries[i]
        if (!amountKnown(n)) {
            if (knownOnly) continue
            return null
        }
        total += Number(n.amountSat)
    }
    return total
}

function positions(entries, heights) {
    let height = 0
    const nodes = entries.map(function(entry, i) {
        const h = heights[i] || 80
        const node = { entry: entry, y: height, height: h }
        height += h + 12
        return node
    })
    return { nodes: nodes, height: Math.max(0, height - 12) }
}

function positiveAmount(entry) {
    return amountKnown(entry) && Number(entry.amountSat) > 0
}

// Reserve 1 px for small positive amounts, then share the remaining space in
// proportion to the remaining amounts. Zero-value data outputs get a 1 px line;
// unknown amounts keep their separate, fixed-width treatment.
function ribbonWidths(entries, amountScale, totalWidth) {
    const widths = entries.map(function(entry) { return amountKnown(entry)
        ? (entry.kind === "data" && Number(entry.amountSat) === 0 ? 1 : 0)
        : (entry.kind === "fee" ? 2 : 4) })
    const dataWidth = entries.reduce(function(total, entry, index) {
        return total + (amountKnown(entry) && entry.kind === "data" ? widths[index] : 0)
    }, 0)
    const known = []
    entries.forEach(function(entry, index) {
        if (positiveAmount(entry)) known.push({ index: index, amount: Number(entry.amountSat) })
    })
    known.sort(function(a, b) { return a.amount - b.amount })
    let remainingAmount = sum(entries, true)
    let remainingWidth = Math.max(known.length, amountScale > 0 ? remainingAmount / amountScale * totalWidth - dataWidth : 0)
    known.forEach(function(entry) {
        const width = Math.max(1, entry.amount / remainingAmount * remainingWidth)
        widths[entry.index] = width
        remainingAmount -= entry.amount
        remainingWidth -= width
    })
    return widths
}

// Values are integer satoshis. Start from a shared scale for known amounts,
// applying a visibility floor on each side without breaking the junction.
// Unknown connections have no amount scale.
// Input-to-output attribution is deliberately never constructed.
function calculate(inputs, outputs, inputHeights, outputHeights, width, complete) {
    const left = positions(inputs, inputHeights), right = positions(outputs, outputHeights)
    const height = Math.max(left.height, right.height)
    const inputWidth = width * 0.35
    const outputWidth = width * 0.35
    const outputX = width - outputWidth
    const junctionX = (inputWidth + outputX) / 2
    const inputTotal = sum(inputs), outputTotal = sum(outputs)
    const proportional = complete && inputTotal !== null && inputTotal > 0 && inputTotal === outputTotal
    const amountScale = Math.max(sum(inputs, true), sum(outputs, true))
    const totalWidth = Math.max(64, inputs.filter(positiveAmount).length, outputs.filter(positiveAmount).length)
    const inputWidths = ribbonWidths(inputs, amountScale, totalWidth)
    const outputWidths = ribbonWidths(outputs, amountScale, totalWidth)
    const unknownInputs = inputs.map(function(entry, index) { return amountKnown(entry) ? -1 : index })
        .filter(function(index) { return index >= 0 })
    let inferredInputs = false
    // Share the remaining visual band equally among missing inputs. These
    // display widths do not estimate input amounts or the unknown fee.
    if (unknownInputs.length > 0) {
        const remaining = outputWidths.reduce(function(total, width) { return total + width }, 0)
            - inputWidths.reduce(function(total, width, i) { return total + (amountKnown(inputs[i]) ? width : 0) }, 0)
        if (remaining > 0) {
            unknownInputs.forEach(function(index) { inputWidths[index] = remaining / unknownInputs.length })
            inferredInputs = true
        }
    }
    const ribbons = []
    let junctionMin = height / 2, junctionMax = height / 2
    let junctionOwnership = inputs.length ? inputs[0].ownership : "unknown"
    inputs.forEach(function(entry) {
        if (entry.ownership !== junctionOwnership) junctionOwnership = "unknown"
    })
    function side(group, isInput) {
        const widths = isInput ? inputWidths : outputWidths
        const extent = widths.reduce(function(total, width) { return total + width }, 0)
        const junctionTop = (height - extent) / 2
        let offset = 0
        group.nodes.forEach(function(node, index) {
            node.y += (height - group.height) / 2
            const lineWidth = widths[index]
            const portY = junctionTop + offset + lineWidth / 2
            offset += lineWidth
            if (lineWidth === 0) return
            junctionMin = Math.min(junctionMin, portY)
            junctionMax = Math.max(junctionMax, portY)
            ribbons.push({
                id: (isInput ? "in:" : "out:") + node.entry.id,
                startX: isInput ? inputWidth : junctionX,
                endX: isInput ? junctionX : outputX,
                startY: isInput ? node.y + node.height / 2 : portY,
                endY: isInput ? portY : node.y + node.height / 2,
                thickness: lineWidth, proportional: positiveAmount(node.entry) && amountScale > 0,
                inferredWidth: isInput && inferredInputs && !amountKnown(node.entry),
                sourceOwnership: isInput ? node.entry.ownership : junctionOwnership,
                targetOwnership: isInput ? junctionOwnership : node.entry.ownership,
                targetKind: isInput ? "" : node.entry.kind
            })
        })
    }
    side(left, true)
    side(right, false)
    // Unknown widths cannot form a continuous band through the junction. A
    // neutral connector joins the ports without inventing an input amount.
    if (!proportional && !inferredInputs && junctionMax > junctionMin) ribbons.unshift({
        id: "junction", startX: junctionX, endX: junctionX, startY: junctionMin, endY: junctionMax,
        thickness: 4, proportional: false, sourceOwnership: "unknown", targetOwnership: "unknown"
    })
    return { height: height, inputWidth: inputWidth, outputWidth: outputWidth,
        outputX: outputX, junctionX: junctionX, inputs: left.nodes, outputs: right.nodes,
        ribbons: ribbons, proportional: proportional, amountsKnown: inputTotal !== null && outputTotal !== null }
}

// A vertically offset band becomes subpixel-thin on steep curves. Widen only
// those sections along the curve's normal, keeping at least 1 px of visible
// width while preserving the amount-scaled widths at both ends.
function minimumWidthPath(r) {
    const dx = r.endX - r.startX, dy = r.endY - r.startY, half = r.thickness / 2
    function edges(t) {
        const u = 1 - t
        const x = r.startX + dx * (1.5 * u * u * t + 1.5 * u * t * t + t * t * t)
        const y = r.startY + dy * t * t * (3 - 2 * t)
        const tangentX = 1.5 * dx * (u * u + t * t), tangentY = 6 * dy * u * t
        const length = Math.sqrt(tangentX * tangentX + tangentY * tangentY)
        const nx = -tangentY / length, ny = tangentX / length
        const extra = Math.max(0, 0.5 - half * ny)
        const ox = nx * extra, oy = half + ny * extra
        return { top: { x: x - ox, y: y - oy }, bottom: { x: x + ox, y: y + oy } }
    }
    function deviation(point, a, b) {
        const x = b.x - a.x, y = b.y - a.y
        const cross = x * (point.y - a.y) - y * (point.x - a.x)
        return cross * cross / Math.max(x * x + y * y, 1e-12)
    }
    const start = edges(0), middle = edges(0.5), end = edges(1)
    const top = [start.top], bottom = [start.bottom]
    function append(t0, a, t1, b, depth) {
        const tm = (t0 + t1) / 2, m = edges(tm)
        // Adapt to curvature rather than diagram height, so tall batches do
        // not produce one point per pixel. Bound the outline error to 0.1 px.
        if (depth < 12 && Math.max(deviation(m.top, a.top, b.top), deviation(m.bottom, a.bottom, b.bottom)) > 0.01) {
            append(t0, a, tm, m, depth + 1)
            append(tm, m, t1, b, depth + 1)
        } else {
            top.push(b.top)
            bottom.push(b.bottom)
        }
    }
    // Split the S-curve at its inflection; its midpoint lies on the full chord.
    append(0, start, 0.5, middle, 0)
    append(0.5, middle, 1, end, 0)
    const outline = top.concat(bottom.reverse())
    return "M " + outline.map(function(p) { return p.x + " " + p.y }).join(" L ") + " Z"
}

function path(ribbon) {
    // A path may outlive its entry briefly when the snapshot shrinks.
    if (ribbon.startX === undefined) return ""
    const r = ribbon, mid = (r.startX + r.endX) / 2, half = r.thickness / 2
    if (!r.proportional && !r.inferredWidth) return "M " + r.startX + " " + r.startY + " C " + mid + " " + r.startY
        + " " + mid + " " + r.endY + " " + r.endX + " " + r.endY
    const dx = r.endX - r.startX, dy = r.endY - r.startY
    if (dx > 0 && r.thickness * dx < Math.sqrt(dx * dx + 4 * dy * dy)) return minimumWidthPath(r)
    return "M " + r.startX + " " + (r.startY - half)
        + " C " + mid + " " + (r.startY - half) + " " + mid + " " + (r.endY - half) + " " + r.endX + " " + (r.endY - half)
        + " L " + r.endX + " " + (r.endY + half)
        + " C " + mid + " " + (r.endY + half) + " " + mid + " " + (r.startY + half) + " " + r.startX + " " + (r.startY + half) + " Z"
}
