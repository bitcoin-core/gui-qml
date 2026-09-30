// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import "../../controls"
import "WidgetMetrics.js" as WidgetMetrics

Popup {
    id: root
    required property var layoutModel
    property string addError: ""
    property bool reducedMotion: false
    property real verticalOffset: 0
    property int selectedIndex: 0
    readonly property real centeredY: parent ? Math.max(20, (parent.height - height) / 2) : 0
    readonly property var sortedCatalog: layoutModel.catalog.slice().sort(function(a, b) { return a.title.localeCompare(b.title) })
    readonly property var selectedWidget: sortedCatalog[selectedIndex] || null
    // Keep the catalog index for adding, even when the display order differs.
    readonly property var sortedSizes: {
        if (!selectedWidget) return []
        return selectedWidget.sizes.map(function(size, index) {
            return {columns: size.columns, rows: size.rows, label: size.label, sizeIndex: index}
        }).sort(function(a, b) { return a.columns * a.rows - b.columns * b.rows })
    }
    objectName: "widgetPicker"
    modal: true
    dim: true
    focus: true
    width: Math.min(1040, parent ? parent.width - 32 : 1040)
    height: Math.min(760, parent ? parent.height - 40 : 760)
    padding: width < 480 ? 16 : 24
    x: parent ? (parent.width - width) / 2 : 0
    y: centeredY + verticalOffset
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAboutToShow: {
        addError = ""
        selectedIndex = 0
        splitView.showDetail()
        resetScroll()
    }
    onSelectedWidgetChanged: {
        addError = ""
        resetScroll()
    }
    function resetScroll() {
        if (splitView.detailItem) splitView.detailItem.resetScroll()
    }
    function selectWidget(index) {
        selectedIndex = index
        splitView.showDetail()
    }
    function addSelectedSize(sizeIndex) {
        if (selectedWidget && layoutModel.addWidget(selectedWidget.id, sizeIndex)) {
            close()
        } else {
            addError = qsTr("There is not enough room for this widget. Resize or remove an existing widget to make room.")
        }
    }
    background: Rectangle {
        color: Theme.color.neutral1
        radius: 16
        border.color: Theme.color.neutral3
        SurfaceGradientBorder { anchors.fill: parent; surfaceColor: parent.color; cornerRadius: parent.radius }
    }
    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.6)
        Behavior on opacity { NumberAnimation { duration: 180 } }
    }
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 180 }
            NumberAnimation {
                property: "verticalOffset"
                from: root.reducedMotion ? 0 : 16
                to: 0
                duration: root.reducedMotion ? 0 : 280
                easing.type: Easing.OutCubic
            }
        }
    }
    exit: Transition { NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 150 } }
    contentItem: ColumnLayout {
        spacing: 16
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            CoreText {
                objectName: "widgetPickerTitle"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                font: Theme.text.title.font
                text: qsTr("Add widgets")
            }
            CloseButton {
                objectName: "widgetPickerClose"
                iconColor: Theme.color.neutral7
                Accessible.name: qsTr("Close widget picker")
                onClicked: root.close()
            }
        }
        CoreText {
            objectName: "widgetPickerError"
            Layout.fillWidth: true
            visible: root.addError.length > 0
            text: root.addError
            font: Theme.text.caption.font
            color: Theme.color.neutral7
            horizontalAlignment: Text.AlignLeft
            Accessible.role: Accessible.AlertMessage
        }
        NavigationSplitView {
            id: splitView
            objectName: "widgetPickerNavigationSplitView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            primaryMinimumWidth: 200
            primaryPreferredWidth: 240
            primaryMaximumWidth: 280
            primaryWidthRatio: 0.26
            detailMinimumWidth: 280
            separatorWidth: 0
            transitionDuration: root.reducedMotion ? 0 : 200
            primaryComponent: Component {
                ScrollView {
                    id: widgetList
                    objectName: "widgetPickerList"
                    clip: true
                    contentWidth: availableWidth
                    contentHeight: widgetTable.implicitHeight
                    background: Rectangle { color: Theme.color.neutral2; radius: 16 }
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                    function focusRow(index) {
                        const row = widgetRows.itemAt(Math.max(0, Math.min(widgetRows.count - 1, index)))
                        if (!row) return
                        row.forceActiveFocus()
                        if (row.y < contentItem.contentY) contentItem.contentY = row.y
                        else if (row.y + row.height > contentItem.contentY + availableHeight)
                            contentItem.contentY = row.y + row.height - availableHeight
                    }

                    FormSection {
                        id: widgetTable
                        objectName: "widgetPickerTable"
                        width: widgetList.availableWidth
                        showBackground: false
                        showGradientBorder: false

                        Repeater {
                            id: widgetRows
                            model: root.sortedCatalog
                            delegate: ItemDelegate {
                                id: choice
                                required property var modelData
                                required property int index
                                objectName: "widgetPickerRow_" + modelData.id
                                Layout.fillWidth: true
                                implicitHeight: contentItem.implicitHeight
                                padding: 0
                                hoverEnabled: true
                                focusPolicy: Qt.StrongFocus
                                Accessible.name: modelData.title
                                Accessible.selected: root.selectedIndex === index
                                onClicked: root.selectWidget(index)
                                Keys.onUpPressed: widgetList.focusRow(index - 1)
                                Keys.onDownPressed: widgetList.focusRow(index + 1)
                                background: Item {
                                    Rectangle {
                                        anchors.fill: parent
                                        anchors.margins: 4
                                        radius: 12
                                        color: choice.down || root.selectedIndex === choice.index || choice.hovered
                                            ? Theme.color.neutral3 : "transparent"
                                    }
                                    FocusBorder {
                                        visible: choice.visualFocus
                                        borderRadius: 12
                                        topMargin: 4; bottomMargin: 4; leftMargin: 4; rightMargin: 4
                                    }
                                }
                                contentItem: FormRow {
                                    title: choice.modelData.title
                                    titleTextStyle: Theme.text.description
                                    minimumRowHeight: 56
                                    dividerColor: Theme.color.neutral3
                                    showDivider: choice.index < widgetRows.count - 1
                                }
                            }
                        }
                    }
                }
            }
            detailComponent: Component {
                ColumnLayout {
                    spacing: 12
                    function resetScroll() { previewScroll.contentItem.contentY = 0 }
                    NavButton {
                        objectName: "widgetPickerBack"
                        visible: splitView.isCompact
                        Layout.leftMargin: 4
                        iconSource: "image://images/caret-left"
                        text: qsTr("Widgets")
                        bold: false
                        onClicked: splitView.showPrimary()
                    }
                    ScrollView {
                        id: previewScroll
                        objectName: "widgetPickerScroll"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: availableWidth
                        contentHeight: previews.implicitHeight
                        clip: true
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        Column {
                            id: previews
                            width: previewScroll.availableWidth
                            topPadding: 4
                            bottomPadding: 12
                            spacing: 24
                            readonly property real maxColumns: root.sortedSizes.reduce(function(value, size) { return Math.max(value, size.columns) }, 1)
                            readonly property real cellSize: Math.min(160, Math.max(1, (width - 48 - (maxColumns - 1) * 12) / maxColumns))
                            Repeater {
                                model: root.visible ? root.sortedSizes : []
                                delegate: Button {
                                    id: preview
                                    required property var modelData
                                    objectName: "widgetPickerSize_" + root.selectedWidget.id + "_" + modelData.columns + "x" + modelData.rows
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: modelData.columns * (previews.cellSize + 12) - 12
                                    height: modelData.rows * (previews.cellSize + 12) - 12
                                    padding: WidgetMetrics.contentPadding
                                    hoverEnabled: true
                                    focusPolicy: Qt.StrongFocus
                                    Accessible.name: qsTr("Add %1, %2 by %3").arg(root.selectedWidget.title).arg(modelData.columns).arg(modelData.rows)
                                    onClicked: root.addSelectedSize(modelData.sizeIndex)
                                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                                    scale: down ? 0.98 : 1
                                    background: Rectangle {
                                        radius: preview.modelData.columns === 1 && preview.modelData.rows === 1 ? 20 : preview.modelData.rows === 1 ? 22 : 26
                                        color: Theme.color.background
                                        border.width: preview.hovered || preview.visualFocus ? 2 : 1
                                        border.color: preview.hovered || preview.visualFocus ? Theme.color.orange : Theme.color.neutral2
                                    }
                                    contentItem: Loader {
                                        id: previewContent
                                        enabled: false
                                        clip: true
                                        function loadPreview() {
                                            if (!root.selectedWidget) return
                                            setSource(root.selectedWidget.source, {
                                                columnSpan: preview.modelData.columns,
                                                rowSpan: preview.modelData.rows,
                                                cellSize: previews.cellSize,
                                                preview: true,
                                                active: false
                                            })
                                        }
                                        Component.onCompleted: loadPreview()
                                        Connections {
                                            target: root
                                            function onSelectedWidgetChanged() { previewContent.loadPreview() }
                                        }
                                        onLoaded: {
                                            if (item instanceof DashboardWidget) {
                                                item.cellSize = Qt.binding(function() { return previews.cellSize })
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
