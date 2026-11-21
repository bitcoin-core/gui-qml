// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

.pragma library

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

// Values are integer satoshis. Start from a shared scale for known amounts,
// applying a visibility floor on each side without breaking the junction.
// Unknown connections have no amount scale.
// Input-to-output attribution is deliberately never constructed.
function calculate(inputs, outputs, inputHeights, outputHeights, width, facts) {
    const left = positions(inputs, inputHeights), right = positions(outputs, outputHeights)
    const height = Math.max(left.height, right.height)
    const inputWidth = width * 0.35
    const outputWidth = width * 0.35
    const outputX = width - outputWidth
    const junctionX = (inputWidth + outputX) / 2
    const proportional = !!facts.proportional
    const amountScale = facts.amountScale || 0
    const inputWidths = facts.inputWidths || []
    const outputWidths = facts.outputWidths || []
    const inferredInputs = !!facts.inferredInputs
    const ribbons = []
    let junctionMin = height / 2, junctionMax = height / 2
    const junctionOwnership = facts.junctionOwnership || "unknown"
    function side(group, isInput) {
        const widths = isInput ? inputWidths : outputWidths
        const extent = widths.reduce(function(total, width) { return total + width }, 0)
        const junctionTop = (height - extent) / 2
        let offset = 0
        group.nodes.forEach(function(node, index) {
            node.y += (height - group.height) / 2
            const lineWidth = widths[index] || 0
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
                thickness: lineWidth, proportional: node.entry.amountKnown && node.entry.amountSat > 0 && amountScale > 0,
                inferredWidth: isInput && inferredInputs && !node.entry.amountKnown,
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
        ribbons: ribbons, proportional: proportional, amountsKnown: !!facts.amountsKnown }
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
