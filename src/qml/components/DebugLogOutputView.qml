// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

Item {
    id: root

    Accessible.role: Accessible.List
    Accessible.name: accessibleName

    property var listModel: null
    property string accessibleName: ""

    property int horizontalPadding: 0
    property int topPadding: 10
    property int bottomPadding: 16
    property int rowSpacing: 10
    property int columnSpacing: 10
    property int contentSpacing: 2
    property int lineNumberWidth: 20
    property int fontPixelSize: 12
    property int textLineHeight: 17
    property string fontFamily: Theme.text.family
    property string fontStyleName: "Regular"
    property bool autoScrollToBottom: false

    readonly property int lineNumberDigits: String(Math.max(1, count)).length
    readonly property string lineNumberSampleText: lineNumberDigits <= 3
                                                    ? ""
                                                    : lineNumberDigits === 4
                                                        ? "8888"
                                                        : "88888"
    readonly property int effectiveLineNumberWidth: lineNumberDigits <= 3
                                                     ? lineNumberWidth
                                                     : Math.max(lineNumberWidth, Math.ceil(lineNumberMetrics.advanceWidth))
    readonly property bool atBottom: list.atYEnd
    readonly property bool atTop: list.atYBeginning
    readonly property real contentY: list.contentY
    readonly property real contentHeight: list.contentHeight
    readonly property real originY: list.originY
    readonly property int count: list.count
    readonly property int instantiatedDelegateCount: root._instantiatedDelegateCount

    // These values describe the topmost visible row immediately before a
    // newest-first insertion at row zero. Once the insertion completes, that
    // row has moved down by the size of the inserted batch. Restoring its
    // pixel offset keeps the text under the user's eyes stationary.
    property int _prependAnchorIndex: -1
    property real _prependAnchorOffset: 0
    property int _prependCount: 0
    property bool _prependRestorePending: false
    property int _prependRestoreGeneration: 0
    property real _appendAnchorContentY: 0
    property int _appendCount: 0
    property bool _appendRestorePending: false
    property int _appendRestoreGeneration: 0
    property int _instantiatedDelegateCount: 0

    signal scrolled(real y)

    function scrollToTop() {
        list.positionViewAtBeginning()
        list.returnToBounds()
    }

    function scrollToBottom() {
        list.forceLayout()
        list.positionViewAtEnd()
        // positionViewAtEnd() aligns the final delegate, but ListView's
        // bottomMargin sits beyond that delegate. Include it so atYEnd is true
        // and the external Load more affordance becomes available.
        if (list.contentHeight + list.bottomMargin > list.height) {
            list.contentY = list.originY + list.contentHeight + list.bottomMargin - list.height
        }
        list.returnToBounds()
    }

    function positionViewAtIndex(index, mode) {
        list.positionViewAtIndex(index, mode === undefined ? ListView.Visible : mode)
    }

    function itemAtIndex(index) {
        return list.itemAtIndex(index)
    }

    function forceLayout() {
        list.forceLayout()
    }

    function _firstVisibleIndex() {
        // contentY can fall in the spacing between two variable-height rows.
        // Scan a small distance into the viewport rather than treating that
        // gap as if the view had no visible anchor.
        // ListView's origin can move away from zero as variable-height rows are
        // inserted or removed. indexAt() expects content coordinates, so use
        // contentY directly rather than treating zero as the logical start.
        const firstY = list.contentY
        const scanDistance = Math.min(list.height, root.rowSpacing + root.textLineHeight + 2)
        for (let offset = 0; offset <= scanDistance; ++offset) {
            const candidate = list.indexAt(1, firstY + offset)
            if (candidate >= 0) return candidate
        }
        return -1
    }

    function _capturePrependAnchor(first, last) {
        root._prependAnchorIndex = -1
        root._prependCount = 0

        if (first !== 0) return

        // A full snapshot diff can publish an older suffix before its newer
        // prefix. Restore the pre-append viewport synchronously so the prepend
        // anchor is captured from what the user was actually looking at.
        root._restorePendingAppendAnchor()
        if (list.count === 0 || root.atTop) return

        list.forceLayout()
        const anchorIndex = root._firstVisibleIndex()
        if (anchorIndex < 0) return

        const anchorItem = list.itemAtIndex(anchorIndex)
        if (!anchorItem) return

        root._prependAnchorIndex = anchorIndex
        root._prependAnchorOffset = anchorItem.y - list.contentY
        root._prependCount = last - first + 1
    }

    function _schedulePrependAnchorRestore(first, last) {
        if (first !== 0 || root._prependAnchorIndex < 0 || root._prependCount !== last - first + 1) {
            root._prependAnchorIndex = -1
            root._prependCount = 0
            return
        }

        const targetIndex = root._prependAnchorIndex + root._prependCount
        const targetOffset = root._prependAnchorOffset
        root._prependAnchorIndex = -1
        root._prependCount = 0
        root._prependRestorePending = true
        const generation = ++root._prependRestoreGeneration
        ++root._appendRestoreGeneration
        root._appendRestorePending = false
        Qt.callLater(function() {
            if (generation !== root._prependRestoreGeneration) return
            root._restorePrependAnchor(targetIndex, targetOffset)
            root._prependRestorePending = false
        })
    }

    function _restorePrependAnchor(targetIndex, targetOffset) {
        if (targetIndex < 0 || list.count === 0) return

        const boundedIndex = Math.min(targetIndex, list.count - 1)
        list.forceLayout()
        list.positionViewAtIndex(boundedIndex, ListView.Beginning)
        list.forceLayout()

        const anchorItem = list.itemAtIndex(boundedIndex)
        if (!anchorItem) return

        list.contentY = anchorItem.y - targetOffset
        list.returnToBounds()
    }

    function _captureAppendAnchor(first, last) {
        root._appendCount = 0
        if (first !== list.count || first === 0 || root._prependRestorePending) return

        // Coalesce multiple suffix batches in the same event turn around the
        // viewport that preceded all of them.
        root._restorePendingAppendAnchor()
        root._appendAnchorContentY = list.contentY
        root._appendCount = last - first + 1
    }

    function _scheduleAppendAnchorRestore(first, last) {
        if (root._appendCount === 0) return

        if (root._appendCount !== last - first + 1) {
            root._appendCount = 0
            return
        }

        const anchoredContentY = root._appendAnchorContentY
        root._appendCount = 0
        root._appendRestorePending = true
        const generation = ++root._appendRestoreGeneration
        Qt.callLater(function() {
            if (generation !== root._appendRestoreGeneration || root._prependRestorePending) return
            list.forceLayout()
            list.contentY = anchoredContentY
            list.returnToBounds()
            root._appendRestorePending = false
        })
    }

    function _restorePendingAppendAnchor() {
        if (!root._appendRestorePending) return

        ++root._appendRestoreGeneration
        root._appendRestorePending = false
        list.forceLayout()
        list.contentY = root._appendAnchorContentY
        list.returnToBounds()
        list.forceLayout()
    }

    TextMetrics {
        id: lineNumberMetrics
        font.family: root.fontFamily
        font.styleName: root.fontStyleName
        font.pixelSize: root.fontPixelSize
        text: root.lineNumberSampleText
    }

    ListView {
        id: list
        objectName: root.objectName.length > 0 ? root.objectName + "_list" : ""
        x: root.horizontalPadding
        width: Math.max(0, root.width - (root.horizontalPadding * 2))
        height: root.height
        clip: true
        model: root.listModel
        spacing: root.rowSpacing
        cacheBuffer: root.textLineHeight * 4
        // Text selection belongs to an individual row. Avoid carrying a
        // TextEdit's selection state into a different row through pooling;
        // ListView remains virtualized even without delegate reuse.
        reuseItems: false
        bottomMargin: root.bottomPadding
        boundsBehavior: Flickable.StopAtBounds
        // Keep the top padding inside the scrollable content, matching the
        // existing geometry (the first row starts at y=topPadding).
        header: Item {
            width: list.width
            height: root.topPadding
        }
        headerPositioning: ListView.InlineHeader

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            minimumSize: 0.05
        }

        onContentYChanged: root.scrolled(contentY)

        delegate: RowLayout {
            id: rowRoot

            required property var model
            required property int index

            readonly property string rowCommand: rowRoot.model.command ?? ""
            readonly property string rowDate: rowRoot.model.dateLabel ?? ""
            readonly property string rowMessage: rowRoot.model.message ?? ""
            // The newest entry is always row one. Computing this from the
            // delegate index means a prepend does not require dataChanged for
            // every existing row merely to renumber it.
            readonly property string rowNumber: String(rowRoot.index + 1)
            readonly property int rowSeverity: Number(rowRoot.model.severity ?? DebugLogModel.InfoSeverity)
            readonly property bool hasCommand: rowCommand.length > 0

            objectName: root.objectName.length > 0 ? root.objectName + "_row_" + index : ""
            width: list.width
            height: implicitHeight
            spacing: root.columnSpacing

            Accessible.role: Accessible.ListItem
            Accessible.name: rowCommand.length > 0
                             ? rowCommand + " " + rowMessage
                             : rowMessage

            Component.onCompleted: ++root._instantiatedDelegateCount
            Component.onDestruction: --root._instantiatedDelegateCount

            Text {
                objectName: root.objectName.length > 0 ? root.objectName + "_lineNumber_" + rowRoot.index : ""
                text: rowRoot.rowNumber
                color: Theme.color.neutral7
                font.family: root.fontFamily
                font.styleName: root.fontStyleName
                font.pixelSize: root.fontPixelSize
                lineHeight: root.textLineHeight
                lineHeightMode: Text.FixedHeight
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.NoWrap

                Layout.preferredWidth: root.effectiveLineNumberWidth
                Layout.alignment: Qt.AlignTop
            }

            ColumnLayout {
                objectName: root.objectName.length > 0 ? root.objectName + "_entryContent_" + rowRoot.index : ""
                spacing: root.contentSpacing

                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop

                RowLayout {
                    objectName: root.objectName.length > 0 ? root.objectName + "_header_" + rowRoot.index : ""
                    spacing: root.columnSpacing

                    Layout.fillWidth: true

                    Text {
                        objectName: root.objectName.length > 0 ? root.objectName + "_command_" + rowRoot.index : ""
                        text: rowRoot.rowCommand
                        visible: rowRoot.hasCommand
                        color: rowRoot.rowSeverity === DebugLogModel.ErrorSeverity
                               ? Theme.color.red
                               : Theme.color.green
                        font.family: root.fontFamily
                        font.styleName: root.fontStyleName
                        font.pixelSize: root.fontPixelSize
                        lineHeight: root.textLineHeight
                        lineHeightMode: Text.FixedHeight
                        elide: Text.ElideRight
                        wrapMode: Text.NoWrap

                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                    }

                    TextEdit {
                        objectName: root.objectName.length > 0 ? root.objectName + "_commandlessMessage_" + rowRoot.index : ""
                        text: rowRoot.rowMessage
                        visible: !rowRoot.hasCommand
                        readOnly: true
                        selectByMouse: true
                        persistentSelection: false
                        textFormat: Text.PlainText
                        wrapMode: Text.WrapAnywhere
                        font.family: root.fontFamily
                        font.styleName: root.fontStyleName
                        font.pixelSize: root.fontPixelSize
                        color: Theme.color.neutral9
                        selectionColor: Theme.color.orange
                        selectedTextColor: Theme.color.white
                        activeFocusOnPress: true

                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                    }

                    Text {
                        objectName: root.objectName.length > 0 ? root.objectName + "_date_" + rowRoot.index : ""
                        text: rowRoot.rowDate
                        color: Theme.color.neutral7
                        font.family: root.fontFamily
                        font.styleName: root.fontStyleName
                        font.pixelSize: root.fontPixelSize
                        lineHeight: root.textLineHeight
                        lineHeightMode: Text.FixedHeight
                        horizontalAlignment: Text.AlignRight
                        wrapMode: Text.NoWrap

                        Layout.alignment: Qt.AlignTop
                    }
                }

                TextEdit {
                    objectName: root.objectName.length > 0 ? root.objectName + "_message_" + rowRoot.index : ""
                    text: rowRoot.rowMessage
                    visible: rowRoot.hasCommand
                    readOnly: true
                    selectByMouse: true
                    persistentSelection: false
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    font.family: root.fontFamily
                    font.styleName: root.fontStyleName
                    font.pixelSize: root.fontPixelSize
                    color: Theme.color.neutral9
                    selectionColor: Theme.color.orange
                    selectedTextColor: Theme.color.white
                    activeFocusOnPress: true

                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                }
            }
        }
    }

    Connections {
        target: root.listModel
        enabled: root.listModel !== null

        function onRowsAboutToBeInserted(parent, first, last) {
            root._capturePrependAnchor(first, last)
            root._captureAppendAnchor(first, last)
        }

        function onRowsInserted(parent, first, last) {
            root._schedulePrependAnchorRestore(first, last)
            root._scheduleAppendAnchorRestore(first, last)
        }
    }

    Connections {
        target: list
        enabled: root.autoScrollToBottom
        function onContentHeightChanged() {
            root.scrollToBottom()
        }
    }
}
