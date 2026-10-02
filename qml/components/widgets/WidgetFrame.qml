// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Window 2.15
import "../../controls"
import "WidgetMetrics.js" as WidgetMetrics

FocusScope {
    id: root
    required property string instanceId
    required property string widgetId
    required property string widgetTitle
    required property url widgetSource
    required property int columnSpan
    required property int rowSpan
    required property var supportedSizes
    required property Item coordinateItem
    property real cellSize: Math.min((width - (columnSpan - 1) * 12) / columnSpan,
                                     (height - (rowSpan - 1) * 12) / rowSpan)
    property bool editing: false
    property bool interacting: false
    property bool dragActive: false
    readonly property bool longPressActive: moveArea.longPressActivated && moveArea.pressed
    readonly property bool liftActive: dragActive || longPressActive
    readonly property bool highlighted: editing && (longPressActive || moveArea.pressed || resizePointer.pressed || interacting)
    property bool animatePlacement: true
    readonly property real cornerRadius: columnSpan === 1 && rowSpan === 1 ? 20 : rowSpan === 1 ? 22 : 26
    readonly property int contentPadding: WidgetMetrics.contentPadding
    property int availableColumns: 6
    property int availableRows: 6
    readonly property DashboardWidget widgetContent: content.item instanceof DashboardWidget ? content.item : null
    property bool inViewport: true
    readonly property bool contentActive: visible && inViewport && (!Window.window || (Window.window.visible && Window.window.visibility !== Window.Minimized))
    objectName: "widget_" + instanceId
    activeFocusOnTab: editing
    Accessible.role: Accessible.Grouping
    Accessible.name: widgetTitle
    //: Keyboard instructions for arranging a dashboard widget.
    Accessible.description: editing ? qsTr("Use arrow keys to move, R to resize, and Delete to remove.") : ""

    signal editRequested()
    signal removeRequested()
    signal gestureStarted(string kind, real pointerX, real pointerY)
    signal gestureMoved(real pointerX, real pointerY)
    signal gestureEnded()
    signal gestureCanceled()
    signal moveRequested(int dx, int dy)
    signal resizeRequested(int columns, int rows)

    function openSizeMenu() {
        sizeMenu.showActions = false
        sizeMenu.open()
    }

    Behavior on x { enabled: root.animatePlacement && !root.interacting; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
    Behavior on y { enabled: root.animatePlacement && !root.interacting; NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }

    state: liftActive ? "lifted" : ""
    states: State {
        name: "lifted"
        PropertyChanges { root.scale: 1.05 }
    }
    transitions: [
        Transition {
            from: ""
            to: "lifted"
            NumberAnimation {
                property: "scale"
                duration: 100
                easing.type: Easing.OutCubic
            }
        },
        Transition {
            from: "lifted"
            to: ""
            SpringAnimation {
                property: "scale"
                spring: 5
                damping: 0.35
                epsilon: 0.001
            }
        }
    ]

    Rectangle {
        objectName: "widgetBorder_" + root.instanceId
        anchors.fill: parent
        radius: root.cornerRadius
        color: Theme.color.background
        border.width: root.highlighted ? 2 : 1
        border.color: root.highlighted ? Theme.color.orange : Theme.color.neutral2
    }

    Loader {
        id: content
        anchors.fill: parent
        anchors.margins: root.contentPadding
        function loadContent() { setSource(root.widgetSource, { active: false }) }
        Component.onCompleted: loadContent()
        Connections {
            target: root
            function onWidgetSourceChanged() { content.loadContent() }
        }
        visible: root.widgetContent !== null
        clip: true
        onLoaded: {
            if (!(item instanceof DashboardWidget)) {
                console.warn("WidgetFrame: " + root.widgetId + " (" + root.widgetSource + ") must derive from DashboardWidget")
                return
            }
            item.columnSpan = Qt.binding(function() { return root.columnSpan })
            item.rowSpan = Qt.binding(function() { return root.rowSpan })
            item.cellSize = Qt.binding(function() { return root.cellSize })
            item.active = Qt.binding(function() { return root.contentActive })
        }
    }

    CoreText {
        objectName: "widgetContentError_" + root.instanceId
        anchors.centerIn: parent
        width: Math.max(0, parent.width - 32)
        visible: content.status === Loader.Error || (content.status === Loader.Ready && root.widgetContent === null)
        //: A widget's content failed to load; editing controls remain available.
        text: qsTr("This widget could not be loaded.")
        color: Theme.color.neutral6
    }

    MouseArea {
        id: moveArea
        objectName: "widgetMove_" + root.instanceId
        anchors.fill: parent
        preventStealing: root.editing || longPressActivated
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property point pressPoint
        property bool moving: false
        property bool longPressActivated: false
        onPressed: function(mouse) {
            root.forceActiveFocus()
            pressPoint = mapToItem(root.coordinateItem, mouse.x, mouse.y)
            moving = false
            longPressActivated = false
        }
        onPressAndHold: function(mouse) {
            longPressActivated = true
            if (!root.editing) root.editRequested()
        }
        onPositionChanged: function(mouse) {
            if (!pressed || !root.editing) return
            const point = mapToItem(root.coordinateItem, mouse.x, mouse.y)
            if (!moving && Math.abs(point.x - pressPoint.x) + Math.abs(point.y - pressPoint.y) >= Qt.styleHints.startDragDistance) {
                moving = true
                root.gestureStarted("move", pressPoint.x, pressPoint.y)
            }
            if (moving) root.gestureMoved(point.x, point.y)
        }
        onReleased: {
            if (moving) root.gestureEnded()
            moving = false
            longPressActivated = false
        }
        onCanceled: {
            if (moving) root.gestureCanceled()
            moving = false
            longPressActivated = false
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: function(mouse) {
            if (root.interacting) return
            sizeMenu.showActions = true
            sizeMenu.pointerPosition = Qt.point(mouse.x, mouse.y)
            sizeMenu.open()
        }
    }

    CloseButton {
        id: removeControl
        objectName: "widgetRemove_" + root.instanceId
        // Place edit handles on the scroll surface so their overhanging parts
        // receive pointer events outside the widget's content bounds.
        parent: root.coordinateItem.parent
        visible: root.editing && root.visible
        enabled: root.editing && !root.liftActive
        opacity: root.liftActive ? 0 : 1
        x: root.coordinateItem.x + root.x - size / 3
        y: root.coordinateItem.y + root.y - size / 3
        z: root.z + 3
        Keys.forwardTo: [root]
        size: 30
        backgroundColor: Theme.color.neutral2
        backgroundHoverColor: Theme.color.neutral3
        backgroundPressedColor: Theme.color.neutral3
        iconColor: Theme.color.neutral9
        contentItem: Item {
            Rectangle {
                anchors.centerIn: parent
                width: 12
                height: 2
                radius: 1
                color: removeControl.iconColor
            }
        }
        //: Accessible name of the button that removes a named dashboard widget.
        Accessible.name: qsTr("Remove %1").arg(root.widgetTitle)
        onClicked: root.removeRequested()
        Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    }

    Button {
        id: resizeControl
        objectName: "widgetResize_" + root.instanceId
        parent: root.coordinateItem.parent
        visible: root.editing && root.visible && root.supportedSizes.length > 1
        enabled: root.editing && !root.liftActive
        opacity: root.liftActive ? 0 : 1
        x: root.coordinateItem.x + root.x + root.width - width + 5
        y: root.coordinateItem.y + root.y + root.height - height + 5
        z: root.z + 3
        Keys.forwardTo: [root]
        width: 44
        height: 44
        padding: 0
        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        //: Accessible name of the resize handle and size menu for a dashboard widget.
        Accessible.name: qsTr("Resize %1").arg(root.widgetTitle)
        onClicked: root.openSizeMenu()
        Behavior on opacity { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        contentItem: Item {}
        background: Canvas {
            readonly property color arcColor: resizePointer.pressed || resizeControl.hovered ? Theme.color.neutral3 : Theme.color.neutral2
            readonly property color outlineColor: resizeControl.visualFocus ? Theme.color.orange : Theme.color.neutral3
            onArcColorChanged: requestPaint()
            onOutlineColorChanged: requestPaint()
            onAvailableChanged: if (available) requestPaint()
            onVisibleChanged: if (visible) requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                // Center the stroke on the frame's rounded bottom-right border.
                const inset = 5
                ctx.beginPath()
                ctx.arc(width - inset - root.cornerRadius, height - inset - root.cornerRadius,
                        root.cornerRadius, 0, Math.PI / 2)
                ctx.lineCap = "round"
                ctx.strokeStyle = outlineColor
                ctx.lineWidth = 10
                ctx.stroke()
                ctx.strokeStyle = arcColor
                ctx.lineWidth = 8
                ctx.stroke()
            }
        }
        MouseArea {
            id: resizePointer
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.SizeFDiagCursor
            property point pressPoint
            property bool resizing: false
            onPressed: function(mouse) {
                root.forceActiveFocus()
                pressPoint = mapToItem(root.coordinateItem, mouse.x, mouse.y)
                resizing = false
            }
            onPositionChanged: function(mouse) {
                if (!pressed) return
                const point = mapToItem(root.coordinateItem, mouse.x, mouse.y)
                if (!resizing && Math.abs(point.x - pressPoint.x) + Math.abs(point.y - pressPoint.y) >= Qt.styleHints.startDragDistance) {
                    resizing = true
                    root.gestureStarted("resize", pressPoint.x, pressPoint.y)
                }
                if (resizing) root.gestureMoved(point.x, point.y)
            }
            onReleased: {
                if (resizing) root.gestureEnded()
                else root.openSizeMenu()
                resizing = false
            }
            onCanceled: { if (resizing) root.gestureCanceled(); resizing = false }
        }
    }

    ContextMenu {
        id: sizeMenu
        objectName: "widgetSizeMenu_" + root.instanceId
        property bool showActions: false
        property point pointerPosition
        modal: true
        dim: false
        margins: 8
        title: showActions ? qsTr("Size") : ""
        x: showActions ? pointerPosition.x : root.width - width
        y: showActions ? pointerPosition.y : root.height - resizeControl.height - height - 6

        ContextMenuPicker {
            objectNameRole: "objectName"
            enabledRole: "enabled"
            currentValue: root.columnSpan + "x" + root.rowSpan
            model: root.supportedSizes.slice().sort(function(a, b) { return a.columns * a.rows - b.columns * b.rows }).map(function(size) {
                const value = size.columns + "x" + size.rows
                return {
                    text: size.label,
                    value: value,
                    objectName: "widgetSize_" + root.instanceId + "_" + value,
                    enabled: size.columns <= root.availableColumns && size.rows <= root.availableRows
                }
            })
            onActivated: function(value) {
                sizeMenu.close()
                for (const size of root.supportedSizes) {
                    if (size.columns + "x" + size.rows === value) {
                        root.resizeRequested(size.columns, size.rows)
                        break
                    }
                }
            }
        }

        ContextMenuDivider { visible: sizeMenu.showActions }

        ContextMenuButton {
            objectName: "widgetMenuRemove_" + root.instanceId
            visible: sizeMenu.showActions
            text: qsTr("Remove Widget")
            role: ContextMenuButton.Destructive
            onTriggered: root.removeRequested()
        }
    }

    onEditingChanged: if (!editing) sizeMenu.close()
    onVisibleChanged: if (!visible) sizeMenu.close()

    Keys.onPressed: function(event) {
        if (!root.editing) return
        event.accepted = true
        switch (event.key) {
        case Qt.Key_Left: root.moveRequested(-1, 0); break
        case Qt.Key_Right: root.moveRequested(1, 0); break
        case Qt.Key_Up: root.moveRequested(0, -1); break
        case Qt.Key_Down: root.moveRequested(0, 1); break
        case Qt.Key_R: root.openSizeMenu(); break
        case Qt.Key_Delete: case Qt.Key_Backspace: root.removeRequested(); break
        default: event.accepted = false
        }
    }
}
