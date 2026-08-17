// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "AmountDisplayLayout.js" as AmountLayout

BitcoinAmountDisplayLabelBase {
    id: root

    readonly property int rollDuration: 520
    readonly property int settleDuration: 240
    readonly property real cellPitch: font.pixelSize * 1.4
    readonly property real bleed: font.pixelSize * 0.27
    property var numberLocale: Qt.locale()
    readonly property string numerals: {
        let result = ""
        for (let i = 0; i < 10; ++i) result += i.toLocaleString(numberLocale, "f", 0)
        return result
    }
    property int displayUnit: optionsModel.displayUnit
    // A layout can temporarily give an intrinsic-width label its old width
    // while a sibling changes. Parents with a known allocation can provide it.
    property real animationAvailableWidth: -1
    property string animationText: ""
    readonly property var formattedParts: unit.length && amount.length
        ? {amount: amount, unit: unit, prefix: unitPrefix} : AmountLayout.splitUnit(text)
    property var displayParts: null
    property var transitionUnits: []
    property real unitProgress: 0
    readonly property real alignmentFactor: horizontalAlignment === Text.AlignRight ? 1
        : horizontalAlignment === Text.AlignHCenter ? 0.5 : 0
    property var transitionCells: []
    property var pendingLayout: null
    property bool pendingClipped: false
    property bool animationPending: false
    property bool initialized: false
    property real fromWidth: 0
    property real toWidth: 0
    property real rollProgress: 0
    property real shiftProgress: 0
    property real exitProgress: 0
    property real elapsed: 0
    property int direction: 1

    supportsAnimation: true
    animating: motion.running || (animationPending && transitionCells.length > 0)
    implicitWidth: animating ? fromWidth + (toWidth - fromWidth) * shiftProgress : naturalWidth
    displayText: ""

    FontMetrics { id: metrics; font: root.font }
    Timer {
        id: updateStaticTextTimer
        interval: 0
        onTriggered: root.updateStaticText()
    }
    Timer {
        id: startUnitAnimationTimer
        interval: 0
        onTriggered: root.startUnitAnimation()
    }

    Component.onCompleted: {
        displayText = text
        displayParts = formattedParts
        initialized = true
    }
    onDisplayUnitChanged: {
        if (initialized && animateUnitChanges && supportsAnimation) queueUnitAnimation()
    }
    onTextChanged: {
        if (!initialized) return
        if (animating && text !== animationText) queueUnitAnimation()
        // Formatting and display-unit bindings can fire in either order.
        updateStaticTextTimer.restart()
    }
    onAnimateUnitChangesChanged: {
        if (initialized && !animateUnitChanges) finishAnimation()
    }
    onVisibleChanged: {
        if (initialized && !visible) finishAnimation()
    }

    function updateStaticText() {
        if (!animationPending && !animating) {
            displayText = text
            displayParts = formattedParts
        }
    }

    function layout(value, parts = null) {
        parts = parts || AmountLayout.splitUnit(value)
        const number = parts.amount
        const cells = AmountLayout.cells(number, numberLocale.decimalPoint, numerals)
        const numberX = parts.unit.length && parts.prefix ? metrics.advanceWidth(parts.unit + " ") : 0
        for (const cell of cells) {
            cell.x = metrics.advanceWidth(number.substring(0, cell.start))
            cell.width = metrics.advanceWidth(number.substring(0, cell.end)) - cell.x
            cell.x += numberX
            cell.offset = 0
            cell.opacity = 1
        }
        const units = parts.unit.length ? [{glyph: parts.unit,
            x: parts.prefix ? 0 : metrics.advanceWidth(number + " "),
            width: metrics.advanceWidth(parts.unit), opacity: 1}] : []
        return {cells: cells, units: units, width: Math.ceil(metrics.advanceWidth(value)), text: value}
    }

    function snapshot() {
        const cells = []
        for (let i = 0; i < columns.count; ++i) {
            const column = columns.itemAt(i)
            if (!column || column.opacity < 0.001) continue
            const data = column.modelData
            const travelled = -column.offset / (direction * cellPitch)
            const nearest = Math.round(travelled)
            cells.push({key: data.key, glyph: data.glyph,
                digit: data.digit < 0 ? -1 : ((data.fromDigit + direction * nearest) % 10 + 10) % 10,
                offset: column.offset + direction * nearest * cellPitch,
                x: column.x + alignmentFactor * implicitWidth, width: column.width, opacity: column.opacity})
        }
        const units = []
        for (let i = 0; i < unitLabels.count; ++i) {
            const label = unitLabels.itemAt(i)
            if (label.opacity > 0.001) units.push({glyph: label.text,
                x: label.x + alignmentFactor * implicitWidth, width: label.width, opacity: label.opacity})
        }
        return {cells: cells, units: units, width: implicitWidth, text: animationText}
    }

    function queueUnitAnimation() {
        if (!animationPending) {
            const availableWidth = animationAvailableWidth >= 0 ? animationAvailableWidth : width
            pendingClipped = availableWidth > 0 && naturalWidth > availableWidth + 1
            pendingLayout = animating ? snapshot() : layout(displayText, displayParts)
        }
        animationPending = true
        // Keep the current frame visible while the formatting bindings settle.
        motion.stop()
        startUnitAnimationTimer.restart()
    }

    function finishAnimation() {
        motion.stop()
        animationPending = false
        transitionCells = []
        transitionUnits = []
        pendingLayout = null
        animationText = ""
        displayText = text
        displayParts = formattedParts
    }

    function startUnitAnimation() {
        if (!animationPending) return
        const before = pendingLayout
        const after = layout(text, formattedParts)
        const availableWidth = animationAvailableWidth >= 0 ? animationAvailableWidth : width
        displayText = text
        displayParts = formattedParts
        if (!animateUnitChanges || !supportsAnimation || !visible || wrap
                || !before.cells.length || !after.cells.length || before.text === text
                || pendingClipped || (availableWidth > 0 && after.width > availableWidth + 1)) {
            finishAnimation()
            return
        }
        // Direction follows the displayed magnitude, as in the reference. Locale
        // signs do not affect the glyph mapping or the displayed amount itself.
        const sign = value => /[-\u2212]/.test(value) ? -1 : 1
        direction = sign(text) * AmountLayout.magnitude(after.cells)
            >= sign(before.text) * AmountLayout.magnitude(layout(before.text).cells) ? 1 : -1
        const previous = {}
        for (const cell of before.cells) previous[cell.key] = cell
        const plan = []
        let arriving = 0
        for (const cell of after.cells) {
            const old = previous[cell.key]
            delete previous[cell.key]
            plan.push({key: cell.key, glyph: cell.glyph, digit: cell.digit,
                fromDigit: old ? old.digit : cell.digit,
                steps: old && cell.digit >= 0 ? AmountLayout.steps(old.digit, cell.digit, direction) : 0,
                fromX: old ? old.x - alignmentFactor * before.width : cell.x - alignmentFactor * after.width,
                toX: cell.x - alignmentFactor * after.width,
                fromWidth: old ? old.width : cell.width, toWidth: cell.width,
                fromOffset: old ? old.offset : 0, fromOpacity: old ? old.opacity : 0,
                arriving: !old, leaving: false, delay: old ? 0 : Math.min(arriving++, 4) * 18})
        }
        for (const key in previous) {
            const old = previous[key]
            plan.push({key: old.key, glyph: old.glyph, digit: old.digit, fromDigit: old.digit,
                steps: old.digit >= 0 ? 3 : 0,
                fromX: old.x - alignmentFactor * before.width,
                toX: old.x - alignmentFactor * before.width, fromWidth: old.width, toWidth: old.width,
                fromOffset: old.offset, fromOpacity: old.opacity,
                arriving: false, leaving: true, delay: 0})
        }
        // Unit labels crossfade independently at their aligned positions. A
        // disappearing unit must not follow the shrinking number container.
        const unitPlan = []
        const oldUnits = before.units.slice()
        for (const unit of after.units) {
            const index = oldUnits.findIndex(old => old.glyph === unit.glyph)
            const old = index >= 0 ? oldUnits.splice(index, 1)[0] : null
            const x = unit.x - alignmentFactor * after.width
            unitPlan.push({glyph: unit.glyph, fromX: old ? old.x - alignmentFactor * before.width : x,
                toX: x, width: unit.width, fromOpacity: old ? old.opacity : 0, toOpacity: 1})
        }
        for (const old of oldUnits) {
            const x = old.x - alignmentFactor * before.width
            unitPlan.push({glyph: old.glyph, fromX: x, toX: x, width: old.width,
                fromOpacity: old.opacity, toOpacity: 0})
        }
        transitionUnits = unitPlan
        unitProgress = 0
        fromWidth = before.width
        toWidth = naturalWidth
        rollProgress = 0
        shiftProgress = 0
        exitProgress = 0
        elapsed = 0
        transitionCells = plan
        animationText = text
        motion.start()
        animationPending = false
    }

    // The mask stays still while the strip travels behind it. Its soft edge
    // reaches farther into a turning column at peak speed.
    Rectangle {
        id: edgeMask
        width: 8
        height: root.cellPitch + 2 * root.bleed
        visible: false
        layer.enabled: root.animating
        readonly property real phase: root.elapsed / root.rollDuration
        readonly property real veil: height > 0 ? root.bleed / height
            * (1 + 0.9 * Math.max(0, phase < 0.32 ? phase / 0.32 : (1 - phase) / 0.68)) : 0
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: edgeMask.veil * 0.2; color: Qt.rgba(1, 1, 1, 0.0343) }
            GradientStop { position: edgeMask.veil * 0.35; color: Qt.rgba(1, 1, 1, 0.06) }
            GradientStop { position: edgeMask.veil * 0.5; color: Qt.rgba(1, 1, 1, 0.2145) }
            GradientStop { position: edgeMask.veil * 0.68; color: Qt.rgba(1, 1, 1, 0.4) }
            GradientStop { position: edgeMask.veil * 0.8; color: Qt.rgba(1, 1, 1, 0.651) }
            GradientStop { position: edgeMask.veil * 0.9; color: Qt.rgba(1, 1, 1, 0.86) }
            GradientStop { position: edgeMask.veil; color: "white" }
            GradientStop { position: 1 - edgeMask.veil; color: "white" }
            GradientStop { position: 1 - edgeMask.veil * 0.9; color: Qt.rgba(1, 1, 1, 0.86) }
            GradientStop { position: 1 - edgeMask.veil * 0.8; color: Qt.rgba(1, 1, 1, 0.651) }
            GradientStop { position: 1 - edgeMask.veil * 0.68; color: Qt.rgba(1, 1, 1, 0.4) }
            GradientStop { position: 1 - edgeMask.veil * 0.5; color: Qt.rgba(1, 1, 1, 0.2145) }
            GradientStop { position: 1 - edgeMask.veil * 0.35; color: Qt.rgba(1, 1, 1, 0.06) }
            GradientStop { position: 1 - edgeMask.veil * 0.2; color: Qt.rgba(1, 1, 1, 0.0343) }
            GradientStop { position: 1; color: "transparent" }
        }
    }

    Item {
        visible: root.animating
        width: root.width
        height: root.height
        // Keep the alignment anchor fixed; cells interpolate in coordinates
        // relative to that anchor, including when a RowLayout resizes us.
        x: root.alignmentFactor * root.width
        Repeater {
            id: unitLabels
            model: root.transitionUnits
            CoreText {
                required property var modelData
                objectName: "amountUnit_" + modelData.glyph
                text: modelData.glyph
                font: root.font
                color: root.color
                wrap: false
                horizontalAlignment: Text.AlignLeft
                anchors.verticalCenter: parent.verticalCenter
                width: modelData.width
                x: modelData.fromX + (modelData.toX - modelData.fromX) * root.shiftProgress
                opacity: modelData.fromOpacity + (modelData.toOpacity - modelData.fromOpacity) * root.unitProgress
                Accessible.ignored: true
            }
        }
        Repeater {
            id: columns
            model: root.transitionCells
            BitcoinAmountDigit {
                id: column
                required property var modelData
                objectName: "amountColumn_" + modelData.key
                font: root.font
                color: root.color
                numerals: root.numerals
                glyph: modelData.glyph
                fromDigit: modelData.fromDigit
                steps: modelData.steps
                direction: root.direction
                pitch: root.cellPitch
                bleed: root.bleed
                mask: edgeMask
                turn: modelData.leaving ? root.exitProgress : root.rollProgress
                phase: root.elapsed / root.rollDuration
                readonly property real arrival: Math.max(0, Math.min(1, (root.elapsed - modelData.delay) / root.settleDuration))
                x: modelData.fromX + (modelData.toX - modelData.fromX) * root.shiftProgress
                width: modelData.fromWidth + (modelData.toWidth - modelData.fromWidth) * root.shiftProgress
                anchors.verticalCenter: parent.verticalCenter
                offset: modelData.fromOffset * (1 - turn) - direction * steps * pitch * turn
                opacity: modelData.leaving ? modelData.fromOpacity * (1 - root.exitProgress)
                    : modelData.arriving ? AmountLayout.arrivalOpacity(arrival)
                    : modelData.fromOpacity + (1 - modelData.fromOpacity) * root.shiftProgress
                transform: Scale {
                    origin.x: column.width / 2
                    xScale: modelData.arriving ? 0.55 + 0.45 * arrival : 1
                }
                blurAmount: modelData.leaving ? root.exitProgress : modelData.arriving ? 1 - arrival
                    : Math.min(1, steps / 4) * Math.max(0, phase < 0.15 ? phase / 0.15 : (0.62 - phase) / 0.47)
            }
        }
    }

    ParallelAnimation {
        id: motion
        onFinished: root.finishAnimation()
        NumberAnimation {
            target: root; property: "unitProgress"; from: 0; to: 1; duration: 180
            easing.type: Easing.OutCubic
        }
        NumberAnimation {
            target: root; property: "rollProgress"; from: 0; to: 1; duration: root.rollDuration
            easing.type: Easing.BezierSpline; easing.bezierCurve: [0.32, 0.72, 0, 1, 1, 1]
        }
        NumberAnimation {
            target: root; property: "shiftProgress"; from: 0; to: 1; duration: root.settleDuration
            easing.type: Easing.BezierSpline; easing.bezierCurve: [0.32, 0.72, 0, 1, 1, 1]
        }
        NumberAnimation {
            target: root; property: "exitProgress"; from: 0; to: 1; duration: root.settleDuration
            easing.type: Easing.BezierSpline; easing.bezierCurve: [0.4, 0, 1, 1, 1, 1]
        }
        NumberAnimation { target: root; property: "elapsed"; from: 0; to: root.rollDuration; duration: root.rollDuration }
    }
}
