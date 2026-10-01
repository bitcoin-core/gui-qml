// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../../controls"

Item {
    id: root
    required property var layoutModel
    property bool editing: false
    property int gap: 12
    readonly property int minimumCellSize: 136
    property real gestureCellSize: minimumCellSize
    // Leave a hit-test margin for the edit controls outside the widget frames.
    readonly property int edgePadding: 10
    property int shapeIndex: -1
    readonly property var aspectBoundaries: [0.63, 1.0, 1.58]
    readonly property int columns: 3 + Math.max(0, shapeIndex)
    readonly property int rows: 9 - columns
    readonly property real cellSize: gestureKind !== "" ? gestureCellSize : Math.max(minimumCellSize, Math.min((width - 2 * edgePadding - gap * (columns - 1)) / columns,
                                                        (height - 2 * edgePadding - gap * (rows - 1)) / rows))
    readonly property real pitch: cellSize + gap
    property string gestureKind: ""
    property point pointerStart
    property point lastPointer
    property var originalGeometry: ({})
    property real dragX: 0
    property real dragY: 0
    property int layoutRevision: 0
    readonly property var activeGeometry: layoutRevision >= 0 ? layoutModel.geometry(layoutModel.activeId) : ({})
    objectName: "widgetGrid"
    signal editRequested()

    Binding { target: root.layoutModel; property: "columns"; value: root.columns }
    onEditingChanged: if (!editing) cancelGesture()
    onVisibleChanged: if (!visible) cancelGesture()
    onWidthChanged: Qt.callLater(updateShape)
    onHeightChanged: Qt.callLater(updateShape)
    Component.onCompleted: Qt.callLater(updateShape)

    function updateShape() {
        if (width <= 0 || height <= 0 || layoutModel.activeId !== "") return
        const aspect = width / height
        let next = shapeIndex
        if (next < 0) {
            next = 0
            while (next < 3 && aspect >= aspectBoundaries[next]) ++next
        } else {
            // Require 10% beyond a boundary to avoid oscillating while resizing.
            while (next < 3 && aspect > aspectBoundaries[next] * 1.1) ++next
            while (next > 0 && aspect < aspectBoundaries[next - 1] * 0.9) --next
        }
        const fittingColumns = Math.floor((width - 2 * edgePadding + gap) / (minimumCellSize + gap))
        shapeIndex = Math.min(next, Math.max(0, fittingColumns - 3))
    }

    Connections {
        target: root.layoutModel
        function onLayoutChanged() { root.layoutRevision++ }
        function onActiveIdChanged() {
            if (root.layoutModel.activeId === "") {
                root.gestureKind = ""
                // Let commit/cancel finish before saving or reflowing another board.
                Qt.callLater(root.updateShape)
            }
        }
    }

    function beginGesture(id, kind, x, y) {
        if (!layoutModel.beginInteraction(id)) return
        originalGeometry = layoutModel.geometry(id)
        pointerStart = Qt.point(x, y)
        lastPointer = pointerStart
        dragX = originalGeometry.column * pitch
        dragY = originalGeometry.row * pitch
        gestureCellSize = cellSize
        gestureKind = kind
    }

    function updateGesture(x, y) {
        if (layoutModel.activeId === "") return
        lastPointer = Qt.point(x, y)
        const dx = x - pointerStart.x
        const dy = y - pointerStart.y
        if (gestureKind === "move") {
            dragX = Math.max(0, Math.min((columns - originalGeometry.columns) * pitch, originalGeometry.column * pitch + dx))
            dragY = Math.max(0, Math.min((rows - originalGeometry.rows) * pitch, originalGeometry.row * pitch + dy))
            layoutModel.previewMove(Math.round(dragX / pitch), Math.round(dragY / pitch))
        } else if (gestureKind === "resize") {
            layoutModel.previewResize(originalGeometry.columns + dx / pitch, originalGeometry.rows + dy / pitch)
        }
    }

    function cancelGesture() {
        layoutModel.cancelInteraction()
        gestureKind = ""
    }

    function moveBy(id, dx, dy) {
        const rect = layoutModel.geometry(id)
        if (layoutModel.beginInteraction(id)) {
            layoutModel.previewMove(rect.column + dx, rect.row + dy)
            layoutModel.commitInteraction()
        }
    }

    function resizeTo(id, columns, rows) {
        if (layoutModel.beginInteraction(id)) {
            layoutModel.previewResize(columns, rows)
            layoutModel.commitInteraction()
        }
    }

    Flickable {
        id: viewport
        objectName: "widgetGridViewport"
        anchors.fill: parent
        clip: true
        contentWidth: Math.max(width, surface.width + 2 * root.edgePadding)
        contentHeight: Math.max(height, surface.height + 2 * root.edgePadding)
        interactive: root.layoutModel.activeId === ""
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {}

    Item {
        id: surface
        objectName: "widgetGridSurface"
        anchors.centerIn: parent
        width: root.columns * root.pitch - root.gap
        height: root.rows * root.pitch - root.gap
        Repeater {
            objectName: "widgetGridCells"
            model: root.editing ? root.columns * root.rows : 0
            delegate: Rectangle {
                required property int index
                x: (index % root.columns) * root.pitch
                y: Math.floor(index / root.columns) * root.pitch
                width: root.cellSize
                height: root.cellSize
                radius: 16
                color: "transparent"
                border.color: Theme.color.neutral2
            }
        }

        Rectangle {
            objectName: "widgetDropPreview"
            visible: root.layoutModel.activeId !== ""
            x: (root.activeGeometry.column || 0) * root.pitch
            y: (root.activeGeometry.row || 0) * root.pitch
            width: (root.activeGeometry.columns || 1) * root.pitch - root.gap
            height: (root.activeGeometry.rows || 1) * root.pitch - root.gap
            radius: 22
            color: Qt.rgba(Theme.color.orange.r, Theme.color.orange.g, Theme.color.orange.b, 0.15)
            border.color: Theme.color.orange
            border.width: 2
        }

        Repeater {
            model: root.layoutModel
            delegate: WidgetFrame {
                required property int gridColumn
                required property int gridRow
                x: interacting && root.gestureKind === "move" ? root.dragX : gridColumn * root.pitch
                y: interacting && root.gestureKind === "move" ? root.dragY : gridRow * root.pitch
                width: columnSpan * root.pitch - root.gap
                height: rowSpan * root.pitch - root.gap
                z: interacting ? 2 : 1
                opacity: interacting && root.gestureKind === "move" ? 0.85 : 1
                // Flickable clipping does not change Item.visible. Pause consumers
                // once their entire frame leaves the visible content rectangle.
                inViewport: surface.x + x + width > viewport.contentX
                    && surface.x + x < viewport.contentX + viewport.width
                    && surface.y + y + height > viewport.contentY
                    && surface.y + y < viewport.contentY + viewport.height
                cellSize: root.cellSize
                coordinateItem: surface
                editing: root.editing
                interacting: root.layoutModel.activeId === instanceId
                dragActive: interacting && root.gestureKind === "move"
                animatePlacement: root.gestureKind !== ""
                availableColumns: root.columns - gridColumn
                availableRows: root.rows - gridRow
                onEditRequested: root.editRequested()
                onRemoveRequested: root.layoutModel.removeWidget(instanceId)
                onGestureStarted: function(kind, x, y) { root.beginGesture(instanceId, kind, x, y) }
                onGestureMoved: function(x, y) { root.updateGesture(x, y) }
                onGestureEnded: root.layoutModel.commitInteraction()
                onGestureCanceled: root.cancelGesture()
                onMoveRequested: function(dx, dy) { root.moveBy(instanceId, dx, dy) }
                onResizeRequested: function(columns, rows) { root.resizeTo(instanceId, columns, rows) }
            }
        }
    }
    }
}
