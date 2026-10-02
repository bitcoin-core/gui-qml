// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import "../../qml/components/widgets"

RenderTestCase {
    id: testCase
    name: "WidgetDashboard"
    when: windowShown
    visible: true
    width: 1200
    height: 940

    Component {
        id: dashboardComponent
        WidgetDashboard {
            width: 1180
            height: 640
            storageKey: ""
            widgetRegistry: WidgetRegistry {
                definitions: [
                    WidgetDefinition {
                        widgetId: "clock"
                        title: "Clock"
                        source: Qt.resolvedUrl("../../qml/components/widgets/BlockClockWidget.qml")
                        sizes: [WidgetSize { columns: 2; rows: 2 }, WidgetSize { columns: 3; rows: 3 }]
                        defaultSize: 1
                    },
                    WidgetDefinition {
                        widgetId: "small"
                        title: "Small"
                        source: Qt.resolvedUrl("WidgetFixture.qml")
                        sizes: [WidgetSize { columns: 1; rows: 1 }, WidgetSize { columns: 2; rows: 1 },
                                WidgetSize { columns: 2; rows: 2 }, WidgetSize { columns: 3; rows: 2 },
                                WidgetSize { columns: 3; rows: 3 }]
                    },
                    WidgetDefinition {
                        widgetId: "wide"
                        title: "Wide"
                        source: Qt.resolvedUrl("WidgetFixture.qml")
                        sizes: [WidgetSize { columns: 2; rows: 1 }, WidgetSize { columns: 3; rows: 2 }, WidgetSize { columns: 1; rows: 1 }]
                    },
                    WidgetDefinition {
                        widgetId: "fixed"
                        title: "Fixed"
                        source: Qt.resolvedUrl("WidgetFixture.qml")
                        sizes: [WidgetSize { columns: 1; rows: 1 }]
                    }
                ]
            }
        }
    }
    Component {
        id: frameComponent
        WidgetFrame {
            width: 160; height: 160
            instanceId: "hidden"; widgetId: "fixture"; widgetTitle: "Fixture"
            widgetSource: Qt.resolvedUrl("WidgetFixture.qml")
            columnSpan: 1; rowSpan: 1; supportedSizes: []
            coordinateItem: testCase
        }
    }
    function test_hiddenFrameStartsInactive_data() {
        return [{ tag: "hidden", visible: false, inViewport: true },
                { tag: "offscreen", visible: true, inViewport: false }]
    }
    function test_hiddenFrameStartsInactive(data) {
        const frame = createTemporaryObject(frameComponent, testCase, { visible: data.visible, inViewport: data.inViewport })
        verify(frame !== null)
        tryVerify(function() { return frame.widgetContent !== null })
        compare(frame.widgetContent.activationCount, 0)
        compare(frame.widgetContent.active, false)
        frame.visible = true
        frame.inViewport = true
        tryCompare(frame.widgetContent, "active", true)
        compare(frame.widgetContent.activationCount, 1)
    }

    Component { id: defaultRegistryComponent; DefaultWidgetRegistry {} }

    function init() {
        testCase.Window.window.width = 1200
        testCase.Window.window.height = 940
    }

    function createDashboard() {
        const dashboard = createTemporaryObject(dashboardComponent, testCase)
        verify(dashboard !== null)
        wait(30)
        return dashboard
    }

    function drag(dashboard, item, dx, dy, border) {
        const start = item.mapToItem(dashboard, item.width / 2, item.height / 2)
        mousePress(dashboard, start.x, start.y)
        if (border) compare(border.border.width, 2)
        mouseMove(dashboard, start.x + dx / 2, start.y + dy / 2, 20)
        mouseMove(dashboard, start.x + dx, start.y + dy, 20)
        if (border) compare(border.border.width, 2)
        mouseRelease(dashboard, start.x + dx, start.y + dy)
        if (border) compare(border.border.width, 1)
        wait(180)
    }

    function chooseAction(dashboard, name) {
        mouseClick(findChild(dashboard, "widgetActionsButton"))
        const menu = findChild(dashboard, "widgetActionsMenu")
        tryCompare(menu, "opened", true)
        const edit = findChild(menu.contentItem, "editWidgetsButton")
        compare(edit.text, dashboard.editing ? "Save" : "Edit Widgets")
        compare(findChild(menu.contentItem, "addWidgetButton").text, "Add Widget…")
        mouseClick(findChild(menu.contentItem, name))
        tryCompare(menu, "visible", false)
    }

    function test_initialClockAndEditing() {
        const dashboard = createDashboard()
        compare(dashboard.layoutModel.count, 1)
        const frame = findChild(dashboard, "widget_clock")
        verify(frame !== null)
        compare(frame.columnSpan, 3)
        verify(findChild(frame, "blockClock") !== null)
        const clock = findChild(frame, "blockClock")
        const before = Qt.rect(frame.widgetContent.x, frame.widgetContent.y, frame.widgetContent.width, frame.widgetContent.height)
        const dialHeight = clock.height
        compare(findChild(frame, "blockClockWidgetTitle").text, "Blockclock")
        compare(findChild(frame, "widgetRemove_clock").visible, false)
        chooseAction(dashboard, "editWidgetsButton")
        compare(dashboard.editing, true)
        verify(frame.widgetContent.enabled)
        compare(findChild(frame, "widgetRemove_clock").visible, true)
        compare(Qt.rect(frame.widgetContent.x, frame.widgetContent.y, frame.widgetContent.width, frame.widgetContent.height), before)
        compare(clock.height, dialHeight)
        chooseAction(dashboard, "editWidgetsButton")
        compare(dashboard.editing, false)
    }

    function test_defaultClockSizeAndWidgetButtonIcon() {
        const registry = createTemporaryObject(defaultRegistryComponent, testCase)
        compare(registry.catalog[0].id, "blockclock")
        compare(registry.catalog[0].defaultSize, 1)
        const dashboard = createDashboard()
        const button = findChild(dashboard, "widgetActionsButton")
        compare(button.width, 40)
        compare(button.iconSource.toString(), "image://images/ellipsis")
        compare(button.iconSize, 40)
        const icon = button.iconItem
        compare(icon.width, 40)
        compare(icon.height, 40)
        compare(icon.mapToItem(button, 0, 0).x, 0)
        compare(icon.mapToItem(button, 0, 0).y, 0)
    }

    function test_contentContractTracksSizeAndVisibility() {
        const dashboard = createDashboard()
        const frame = findChild(dashboard, "widget_clock")
        const content = frame.widgetContent
        verify(content instanceof DashboardWidget)
        compare(content.columnSpan, 3)
        compare(content.rowSpan, 3)
        compare(content.active, true)
        const clock = findChild(content, "blockClock")
        verify(clock !== null)
        compare(clock.renderingActive, true)
        const grid = findChild(dashboard, "widgetGrid")
        grid.resizeTo("clock", 2, 2)
        compare(content.columnSpan, 2)
        compare(content.rowSpan, 2)
        dashboard.visible = false
        compare(content.active, false)
        compare(clock.renderingActive, false)
        dashboard.visible = true
        compare(content.active, true)
        compare(clock.renderingActive, true)
        compare(frame.widgetContent, content)
    }

    function test_scrolledWidgetsPauseAndResume() {
        const dashboard = createDashboard()
        dashboard.width = 400
        dashboard.height = 300
        const grid = findChild(dashboard, "widgetGrid")
        const viewport = findChild(dashboard, "widgetGridViewport")
        const surface = findChild(dashboard, "widgetGridSurface")
        tryCompare(grid, "columns", 3)
        dashboard.layoutModel.removeWidget("clock")
        for (let i = 0; i < 9; ++i) verify(dashboard.layoutModel.addWidget("fixed"))
        waitForLayout(dashboard)
        const frames = surface.children.filter(function(child) { return child.widgetId === "fixed" })
        const first = frames.filter(function(frame) { return frame.gridRow === 0 && frame.gridColumn === 0 })[0]
        const last = frames.filter(function(frame) { return frame.gridRow === 2 && frame.gridColumn === 0 })[0]
        const content = last.widgetContent
        compare(first.contentActive, true)
        compare(last.visible, true) // Clipping alone does not pause a widget.
        compare(content.active, false)

        viewport.contentY = surface.y + last.y + last.height - 1
        tryCompare(content, "active", true) // One visible pixel is enough.
        compare(first.widgetContent.active, false)
        viewport.contentY += 1
        tryCompare(content, "active", false)
        viewport.contentY = surface.y + last.y
        tryCompare(content, "active", true)
        compare(last.widgetContent, content)
        dashboard.visible = false
        compare(content.active, false)
        dashboard.visible = true
        compare(content.active, true)

        // Horizontal clipping follows the same contract on a narrow board.
        dashboard.width = 180
        waitForLayout(dashboard)
        viewport.contentY = 0
        const right = frames.filter(function(frame) { return frame.gridRow === 0 && frame.gridColumn === 2 })[0]
        tryCompare(right.widgetContent, "active", false)
        viewport.contentX = surface.x + right.x
        tryCompare(right.widgetContent, "active", true)
        compare(first.widgetContent.active, false)
    }

    function test_registryIsMetadataOnlyAndContentContractIsEnforced() {
        const dashboard = createDashboard()
        const definition = dashboard.widgetRegistry.definitions[3]
        // A registered component is not loaded until added to the board.
        definition.source = Qt.resolvedUrl("InvalidWidgetFixture.qml")
        compare(dashboard.layoutModel.catalog[3].source, definition.source)
        verify(findChild(dashboard, "widget_fixed") === null)
        ignoreWarning(/WidgetFrame: fixed .* must derive from DashboardWidget/)
        verify(dashboard.layoutModel.addWidget("fixed"))
        const frame = findChild(dashboard, "widget_fixed")
        compare(frame.widgetContent, null)
        compare(findChild(frame, "widgetContentError_fixed").visible, true)
        // A broken widget remains removable through the standard frame.
        dashboard.editing = true
        mouseClick(findChild(frame, "widgetRemove_fixed"))
        verify(!dashboard.layoutModel.contains("fixed"))
    }

    function test_registryRejectsInvalidDefinitions() {
        const dashboard = createDashboard()
        const registry = dashboard.widgetRegistry
        const definition = registry.definitions[3]
        ignoreWarning("WidgetRegistry: fixed: unsupported size 4x1")
        definition.sizes[0].columns = 4
        compare(registry.catalog.length, 3)
        compare(dashboard.layoutModel.addWidget("fixed"), false)
        definition.sizes[0].columns = 1
        compare(registry.catalog.length, 4)
        ignoreWarning("WidgetRegistry: fixed: defaultSize must index a supported size")
        definition.defaultSize = 2
        compare(registry.catalog.length, 3)
        definition.defaultSize = 0
        compare(registry.catalog.length, 4)
        ignoreWarning("WidgetRegistry: duplicate widgetId clock")
        definition.widgetId = "clock"
        compare(registry.catalog.length, 3)
    }

    function test_resizeSnapsAndMoveCommits() {
        const dashboard = createDashboard()
        dashboard.editing = true
        const grid = findChild(dashboard, "widgetGrid")
        const frame = findChild(dashboard, "widget_clock")
        const border = findChild(frame, "widgetBorder_clock")
        drag(dashboard, findChild(frame, "widgetResize_clock"), -grid.pitch, -grid.pitch, border)
        compare(dashboard.layoutModel.geometry("clock").columns, 2)
        compare(dashboard.layoutModel.geometry("clock").rows, 2)
        drag(dashboard, findChild(frame, "widgetMove_clock"), grid.pitch, 0, border)
        compare(dashboard.layoutModel.geometry("clock").column, 1)
        compare(dashboard.layoutModel.activeId, "")
    }

    function test_moveGestureLiftsAndSpringsBack() {
        const dashboard = createDashboard()
        dashboard.editing = true
        const grid = findChild(dashboard, "widgetGrid")
        const frame = findChild(dashboard, "widget_clock")
        const moveArea = findChild(frame, "widgetMove_clock")
        const start = moveArea.mapToItem(dashboard, moveArea.width / 2, moveArea.height / 2)

        mousePress(dashboard, start.x, start.y)
        mouseMove(dashboard, start.x + grid.pitch / 2, start.y, 20)
        tryCompare(frame, "dragActive", true)
        tryVerify(function() { return frame.scale > 1.04 })

        mouseRelease(dashboard, start.x + grid.pitch / 2, start.y)
        compare(frame.dragActive, false)
        tryVerify(function() { return Math.abs(frame.scale - 1) < 0.001 }, 1000)
    }

    function test_sizeMenuOrderPreservesSizeValues() {
        const dashboard = createDashboard()
        chooseAction(dashboard, "addWidgetButton")
        const picker = findChild(dashboard, "widgetPicker")
        tryCompare(picker, "opened", true)
        mouseClick(findChild(picker.contentItem, "widgetPickerRow_wide"))
        const smallPreview = findChild(picker.contentItem, "widgetPickerSize_wide_1x1")
        const mediumPreview = findChild(picker.contentItem, "widgetPickerSize_wide_2x1")
        const largePreview = findChild(picker.contentItem, "widgetPickerSize_wide_3x2")
        tryVerify(function() { return smallPreview.y < mediumPreview.y && mediumPreview.y < largePreview.y })
        compare(mediumPreview.contentItem.item.columnSpan, 2)
        compare(mediumPreview.contentItem.item.rowSpan, 1)
        compare(mediumPreview.contentItem.item.preview, true)
        compare(mediumPreview.contentItem.item.active, false)
        mouseClick(mediumPreview)
        tryCompare(picker, "visible", false)
        const frame = findChild(dashboard, "widget_wide")
        compare(frame.columnSpan, 2)
        const menu = findChild(frame, "widgetSizeMenu_wide")
        mouseClick(frame, frame.width / 2, frame.height / 2, Qt.RightButton)
        tryCompare(menu, "opened", true)
        const small = findChild(menu.contentItem, "widgetSize_wide_1x1")
        const medium = findChild(menu.contentItem, "widgetSize_wide_2x1")
        const large = findChild(menu.contentItem, "widgetSize_wide_3x2")
        verify(small.y < medium.y && medium.y < large.y)
        compare(medium.selected, true)
        mouseClick(small)
        compare(frame.columnSpan, 1)
        compare(frame.rowSpan, 1)
    }

    function test_rightClickSizeAndRemove() {
        const dashboard = createDashboard()
        const frame = findChild(dashboard, "widget_clock")
        const menu = findChild(frame, "widgetSizeMenu_clock")
        mouseClick(frame, frame.width / 2, frame.height / 2, Qt.RightButton)
        tryCompare(menu, "opened", true)
        compare(dashboard.editing, false)
        compare(menu.title, "Size")
        const medium = findChild(menu.contentItem, "widgetSize_clock_2x2")
        const large = findChild(menu.contentItem, "widgetSize_clock_3x3")
        compare(medium.text, "Medium")
        compare(large.text, "Large")
        compare(medium.selected, false)
        compare(large.selected, true)
        mouseClick(medium)
        tryCompare(menu, "opened", false)
        tryCompare(menu, "visible", false)
        compare(frame.columnSpan, 2)
        compare(frame.rowSpan, 2)

        dashboard.editing = true
        mouseClick(frame, frame.width / 2, frame.height / 2, Qt.RightButton)
        tryCompare(menu, "opened", true)
        compare(medium.selected, true)
        compare(large.selected, false)
        compare(dashboard.layoutModel.activeId, "")
        const remove = findChild(menu.contentItem, "widgetMenuRemove_clock")
        compare(remove.text, "Remove Widget")
        verify(remove._destructive)
        mouseClick(remove)
        tryCompare(dashboard.layoutModel, "count", 0)
    }

    function test_addPickerAllowsDuplicatesAndIndependentRemoval() {
        const dashboard = createDashboard()
        chooseAction(dashboard, "addWidgetButton")
        const picker = findChild(dashboard, "widgetPicker")
        tryCompare(picker, "opened", true)
        verify(picker.width > 560)
        compare(picker.sortedCatalog.map(function(widget) { return widget.title }), ["Clock", "Fixed", "Small", "Wide"])
        compare(picker.selectedWidget.id, "clock")
        compare(findChild(picker.contentItem, "widgetPickerNavigationSplitView").isCompact, false)
        mouseClick(findChild(picker.contentItem, "widgetPickerRow_small"))
        const firstPreview = findChild(picker.contentItem, "widgetPickerSize_small_1x1")
        const lastPreview = findChild(picker.contentItem, "widgetPickerSize_small_3x3")
        tryVerify(function() { return lastPreview.y > firstPreview.y })
        mouseClick(firstPreview)
        tryCompare(dashboard.layoutModel, "count", 2)
        tryCompare(picker, "visible", false)
        const original = findChild(dashboard, "widget_small")
        chooseAction(dashboard, "addWidgetButton")
        tryCompare(picker, "opened", true)
        compare(picker.selectedWidget.id, "clock")
        mouseClick(findChild(picker.contentItem, "widgetPickerRow_small"))
        const duplicatePreview = findChild(picker.contentItem, "widgetPickerSize_small_2x1")
        tryVerify(function() { return duplicatePreview.y > 0 })
        mouseClick(duplicatePreview)
        tryCompare(dashboard.layoutModel, "count", 3)
        tryCompare(picker, "visible", false)
        const surface = findChild(dashboard, "widgetGridSurface")
        const copies = surface.children.filter(function(child) { return child.widgetId === "small" })
        compare(copies.length, 2)
        const duplicate = copies.filter(function(child) { return child.instanceId !== "small" })[0]
        compare(original.columnSpan, 1)
        compare(duplicate.columnSpan, 2)
        verify(original.instanceId !== duplicate.instanceId)
        // The part of the minus button outside the frame must be clickable too.
        const remove = findChild(original, "widgetRemove_small")
        const overhang = remove.mapToItem(original, 0, 0)
        verify(overhang.x < 0 && overhang.y < 0)
        mouseClick(remove, 5, 5)
        tryCompare(dashboard.layoutModel, "count", 2)
        verify(dashboard.layoutModel.contains("small"))
        compare(findChild(dashboard, "widget_" + duplicate.instanceId), duplicate)
        duplicate.forceActiveFocus()
        keyClick(Qt.Key_Delete)
        tryCompare(dashboard.layoutModel, "count", 1)
        verify(!dashboard.layoutModel.contains("small"))
    }

    function test_pickerCompactNavigationAndReset() {
        const dashboard = createDashboard()
        testCase.Window.window.width = 580
        chooseAction(dashboard, "addWidgetButton")
        const picker = findChild(dashboard, "widgetPicker")
        tryCompare(picker, "opened", true)
        const split = findChild(picker.contentItem, "widgetPickerNavigationSplitView")
        tryCompare(split, "isCompact", true)
        compare(picker.selectedWidget.id, "clock")
        compare(split.compactColumn, 1)
        mouseClick(findChild(picker.contentItem, "widgetPickerBack"))
        tryCompare(findChild(split, "navigationSplitPrimary"), "x", 0)
        mouseClick(findChild(picker.contentItem, "widgetPickerRow_fixed"))
        tryCompare(findChild(split, "navigationSplitDetail"), "x", 0)
        compare(picker.selectedWidget.id, "fixed")
        mouseClick(findChild(picker.contentItem, "widgetPickerSize_fixed_1x1"))
        tryCompare(picker, "visible", false)
        verify(dashboard.layoutModel.contains("fixed"))
    }

    function test_pickerPausesOffscreenPreviews() {
        const dashboard = createDashboard()
        chooseAction(dashboard, "addWidgetButton")
        const picker = findChild(dashboard, "widgetPicker")
        tryCompare(picker, "opened", true)
        mouseClick(findChild(picker.contentItem, "widgetPickerRow_small"))
        const first = findChild(picker.contentItem, "widgetPickerSize_small_1x1")
        const last = findChild(picker.contentItem, "widgetPickerSize_small_3x3")
        const scroll = findChild(picker.contentItem, "widgetPickerScroll")
        waitForLayout(picker.contentItem)
        tryCompare(first.contentItem.item, "renderingActive", true)
        tryCompare(last.contentItem.item, "renderingActive", false)
        compare(first.contentItem.item.active, false)
        scroll.contentItem.contentY = last.y
        tryCompare(last.contentItem.item, "renderingActive", true)
        tryCompare(first.contentItem.item, "renderingActive", false)
        compare(last.contentItem.item.active, false)
    }

    function test_pickerKeepsOpenWhenBoardIsFull() {
        const dashboard = createDashboard()
        verify(dashboard.layoutModel.addWidget("clock"))
        chooseAction(dashboard, "addWidgetButton")
        const picker = findChild(dashboard, "widgetPicker")
        tryCompare(picker, "opened", true)
        mouseClick(findChild(picker.contentItem, "widgetPickerRow_fixed"))
        mouseClick(findChild(picker.contentItem, "widgetPickerSize_fixed_1x1"))
        compare(dashboard.layoutModel.count, 2)
        compare(picker.opened, true)
        verify(picker.addError.length > 0)
        mouseClick(findChild(picker.contentItem, "widgetPickerClose"))
        tryCompare(picker, "visible", false)
    }

    function test_keyboardMovementAndSave() {
        const dashboard = createDashboard()
        const frame = findChild(dashboard, "widget_clock")
        const border = findChild(frame, "widgetBorder_clock")
        const idleColor = border.border.color
        dashboard.editing = true
        frame.forceActiveFocus()
        compare(border.border.width, 1)
        keyClick(Qt.Key_Right)
        compare(dashboard.layoutModel.geometry("clock").column, 1)
        dashboard.layoutModel.beginInteraction("clock")
        dashboard.layoutModel.previewMove(0, 0)
        const preview = dashboard.layoutModel.geometry("clock")
        keyClick(Qt.Key_Escape)
        compare(dashboard.layoutModel.geometry("clock"), preview)
        compare(dashboard.layoutModel.activeId, "")
        compare(dashboard.editing, false)
        // Keyboard focus may remain on the widget after saving.
        frame.forceActiveFocus()
        compare(border.border.width, 1)
        compare(border.border.color, idleColor)
        dashboard.editing = true
        frame.forceActiveFocus()
        keyClick(Qt.Key_Delete)
        compare(dashboard.layoutModel.count, 0)
    }

    function test_escapeWithActionsFocused() {
        const dashboard = createDashboard()
        chooseAction(dashboard, "editWidgetsButton")
        findChild(dashboard, "widgetActionsButton").forceActiveFocus()
        keyClick(Qt.Key_Escape)
        compare(dashboard.editing, false)
    }

    function test_collisionKeepsDelegatesAndWindowReflowRestores() {
        const dashboard = createDashboard()
        const model = dashboard.layoutModel
        const clock = findChild(dashboard, "widget_clock")
        model.addWidget("small")
        model.addWidget("wide")
        compare(findChild(dashboard, "widget_clock"), clock)
        const small = findChild(dashboard, "widget_small")
        model.beginInteraction("clock")
        model.previewMove(2, 0)
        compare(findChild(dashboard, "widget_clock"), clock)
        compare(findChild(dashboard, "widget_small"), small)
        model.commitInteraction()
        const before = model.geometry("clock")
        dashboard.width = 400
        dashboard.height = 900
        wait(20)
        compare(model.columns, 3)
        verify(model.geometry("clock").column + model.geometry("clock").columns <= 3)
        dashboard.width = 1180
        dashboard.height = 640
        wait(20)
        compare(model.geometry("clock"), before)
        model.removeWidget("wide")
        compare(findChild(dashboard, "widget_clock"), clock)
    }

    function test_longPressEntersEditing() {
        const dashboard = createDashboard()
        const frame = findChild(dashboard, "widget_clock")
        const removeControl = findChild(frame, "widgetRemove_clock")
        const resizeControl = findChild(frame, "widgetResize_clock")
        mousePress(frame, 10, 10)
        wait(Qt.styleHints.mousePressAndHoldInterval + 100)
        compare(dashboard.editing, true)
        compare(frame.longPressActive, true)
        tryVerify(function() { return frame.scale > 1.04 })
        tryVerify(function() { return removeControl.opacity < 0.01 && resizeControl.opacity < 0.01 })
        compare(removeControl.enabled, false)
        compare(resizeControl.enabled, false)
        mouseRelease(frame, 10, 10)
        compare(frame.longPressActive, false)
        tryVerify(function() { return Math.abs(frame.scale - 1) < 0.001 }, 1000)
        tryVerify(function() { return removeControl.opacity > 0.99 && resizeControl.opacity > 0.99 })
    }

    function test_longPressContinuesIntoMoveAndDrop() {
        const dashboard = createDashboard()
        const grid = findChild(dashboard, "widgetGrid")
        const frame = findChild(dashboard, "widget_clock")
        const moveArea = findChild(frame, "widgetMove_clock")
        const removeControl = findChild(frame, "widgetRemove_clock")
        const resizeControl = findChild(frame, "widgetResize_clock")
        const start = moveArea.mapToItem(dashboard, moveArea.width / 2, moveArea.height / 2)

        mousePress(dashboard, start.x, start.y)
        wait(Qt.styleHints.mousePressAndHoldInterval + 100)
        compare(dashboard.editing, true)
        compare(frame.longPressActive, true)
        tryVerify(function() { return frame.scale > 1.04 })
        tryVerify(function() { return removeControl.opacity < 0.01 && resizeControl.opacity < 0.01 })
        compare(removeControl.enabled, false)
        compare(resizeControl.enabled, false)

        mouseMove(dashboard, start.x + grid.pitch, start.y, 20)
        tryCompare(frame, "dragActive", true)
        compare(dashboard.layoutModel.activeId, "clock")

        mouseRelease(dashboard, start.x + grid.pitch, start.y)
        compare(dashboard.layoutModel.activeId, "")
        compare(dashboard.layoutModel.geometry("clock").column, 1)
        tryVerify(function() { return Math.abs(frame.scale - 1) < 0.001 }, 1000)
        tryVerify(function() { return removeControl.opacity > 0.99 && resizeControl.opacity > 0.99 })
        compare(removeControl.enabled, true)
        compare(resizeControl.enabled, true)
    }

    function test_gridUsesAvailableAreaAndSquareCells() {
        const dashboard = createDashboard()
        dashboard.height = 1100
        dashboard.editing = true
        const grid = findChild(dashboard, "widgetGrid")
        const surface = findChild(dashboard, "widgetGridSurface")
        const cells = findChild(dashboard, "widgetGridCells")
        // Resize the dashboard while the containing window stays unchanged.
        for (const shape of [{aspect: 0.5, columns: 3, rows: 6}, {aspect: 0.8, columns: 4, rows: 5},
                             {aspect: 1.25, columns: 5, rows: 4}, {aspect: 2, columns: 6, rows: 3}]) {
            dashboard.width = 40 + grid.height * shape.aspect
            tryCompare(grid, "columns", shape.columns)
            compare(grid.rows, shape.rows)
            compare(cells.count, shape.columns * shape.rows)
            verify(surface.width <= grid.width + 0.01)
            verify(surface.height <= grid.height + 0.01)
            compare(cells.itemAt(0).width, cells.itemAt(0).height)
        }
    }

    function test_gridHysteresisAndDeferredSwitchDuringDrag() {
        const dashboard = createDashboard()
        dashboard.height = 1100
        const grid = findChild(dashboard, "widgetGrid")
        dashboard.width = 40 + grid.height * 1.25
        tryCompare(grid, "columns", 5)
        for (const aspect of [1.01, 0.95, 0.91, 1.05]) {
            dashboard.width = 40 + grid.height * aspect
            wait(20)
            compare(grid.columns, 5)
        }
        dashboard.width = 40 + grid.height * 0.89
        tryCompare(grid, "columns", 4)
        dashboard.width = 40 + grid.height * 1.05
        wait(20)
        compare(grid.columns, 4)
        dashboard.width = 40 + grid.height * 1.11
        tryCompare(grid, "columns", 5)
        dashboard.editing = true
        grid.beginGesture("clock", "move", 0, 0)
        const pitch = grid.pitch
        dashboard.width = 40 + grid.height * 0.5
        wait(20)
        compare(grid.columns, 5)
        compare(dashboard.layoutModel.activeId, "clock")
        compare(grid.pitch, pitch)
        dashboard.layoutModel.commitInteraction()
        tryCompare(grid, "columns", 3)
        compare(dashboard.layoutModel.activeId, "")
    }

    function test_compactLabelsAndMinimumCell() {
        const dashboard = createDashboard()
        dashboard.width = 760
        dashboard.height = 350
        const grid = findChild(dashboard, "widgetGrid")
        tryVerify(function() { return grid.cellSize >= 136 })
        const sizes = dashboard.layoutModel.catalog[1].sizes
        compare(sizes[0].label, "Small")
        compare(sizes[1].label, "Medium")
        compare(sizes[2].label, "Medium")
        compare(sizes[3].label, "Large")
        compare(sizes[4].label, "Large")
        verify(dashboard.layoutModel.addWidget("small", 0))
        const frame = findChild(dashboard, "widget_small")
        compare(frame.contentPadding, 16)
        verify(frame.width >= 136)
    }

    function test_dragStopsAtBottomEdge() {
        const dashboard = createDashboard()
        dashboard.layoutModel.removeWidget("clock")
        dashboard.layoutModel.addWidget("small")
        dashboard.editing = true
        const grid = findChild(dashboard, "widgetGrid")
        const frame = findChild(dashboard, "widget_small")
        drag(dashboard, findChild(frame, "widgetMove_small"), 0, grid.pitch * 3)
        compare(dashboard.layoutModel.geometry("small").row, 2)
        compare(dashboard.layoutModel.rows, 3)
    }

    function test_hidingDashboardClosesEditingAndPicker() {
        const dashboard = createDashboard()
        chooseAction(dashboard, "addWidgetButton")
        const picker = findChild(dashboard, "widgetPicker")
        tryCompare(picker, "opened", true)
        dashboard.visible = false
        tryCompare(picker, "opened", false)
        compare(dashboard.editing, false)
    }
}
