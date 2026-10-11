// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import org.bitcoincore.qt 1.0
import "../controls"

// MonospaceOutputView — a scrollable monospace text display backed by a
// list model.
//
// Architecture: ListView, one model row per line of output. Callers must feed
// it one line per row: a row holding a whole multi-line document defeats
// virtualization, since the view still has to lay that document out in full to
// know how tall the row is.
//
// The public model property is named "listModel" (not "model") to avoid
// shadowing the delegate's implicit "model" context property.
//
// Selection belongs to the view, not to the per-row TextEdits, so it can span
// rows: a mouse layer over the list tracks it as {row, pos} endpoints and each
// delegate's TextEdit only paints its own slice. On touch devices the TextEdits
// keep their own per-row selection and the list flicks as usual.
//
// Supports two optional side columns around the main content column:
//   Console:   [timestamp] | [content]
//   Debug log: [line#]     | [content] | [relative time]

Item {
    id: root

    Accessible.role: Accessible.List
    Accessible.name: accessibleName

    // ── Model ────────────────────────────────────────────────────────────

    property var listModel: null
    property string contentRole: "content"
    // Role holding the plain-text rendering of contentRole, if the model has
    // one. Search offsets are in the rendered document's coordinates, so
    // without it the view has to strip the markup itself (see _plainTextOf).
    property string plainContentRole: ""
    property string leftColumnRole: ""
    property string rightColumnRole: ""
    property string categoryRole: ""
    // Role whose value is shared by the rows of one entry (e.g. one RPC reply).
    // Without it every row is an entry of its own.
    property string entryRole: ""

    // ── Style ────────────────────────────────────────────────────────────

    property int   fontPixelSize:    12
    property string fontFamily:      "monospace"
    property string fontStyleName:   ""
    property int   textLineHeight:   0
    property int   contentTextFormat: Text.PlainText
    property color contentColor:     Theme.color.neutral9
    property color leftColumnColor:  Theme.color.neutral5
    property color rightColumnColor: Theme.color.neutral5
    property int   requestCategory:  0
    property int   replyCategory:    1
    property int   errorCategory:    2
    property color requestContentColor: contentColor
    property color replyContentColor: contentColor
    property color errorContentColor: contentColor
    property color requestLeftColumnColor: leftColumnColor
    property color replyLeftColumnColor: leftColumnColor
    property color errorLeftColumnColor: leftColumnColor
    property color selectionColor:    Theme.color.orange
    property color selectedTextColor: Theme.color.white
    property string searchText: ""
    readonly property int searchResultCount: _searchMatches.length
    property int currentSearchResultIndex: -1
    property var _searchMatches: []
    // The match to highlight, as {row, start, end}, or null. Always assigned a
    // fresh object so delegates see a change even when two matches share a row.
    property var _currentMatch: null
    property bool _resetSearchOnRefresh: false

    // ── Selection ────────────────────────────────────────────────────────

    readonly property bool hasSelection: _selection !== null
    // Endpoints as {row, pos}; the anchor stays put while the focus follows
    // the pointer, so the selection can run in either direction.
    property var _selAnchor: null
    property var _selFocus: null
    readonly property var _selection: root._normalizedSelection(_selAnchor, _selFocus)
    // Row last clicked, which Select All expands to its whole entry.
    property int _activeRow: -1
    readonly property bool _pointerSelection: AppMode.isDesktop

    // ── Layout metrics ───────────────────────────────────────────────────

    property int horizontalPadding: 16
    property int topPadding: 16
    property int bottomPadding: 16
    property int rowSpacing: 2
    property int columnSpacing: 8
    property int leftColumnWidth: 0
    property int rightColumnWidth: 0

    // ── Accessibility ────────────────────────────────────────────────────

    property string accessibleName: ""

    // ── Scroll behaviour ─────────────────────────────────────────────────

    property bool autoScrollToBottom: true
    // Cleared when the user scrolls away from the bottom, so a relayout (font
    // or width change) only re-pins the view if it was pinned before.
    property bool _followingTail: true

    // ── Header ───────────────────────────────────────────────────────────

    property Component header: null

    // ── Column-width samples ─────────────────────────────────────────────

    property string leftColumnSample:  "[00:00:00]"
    property string rightColumnSample: "99 hr ago"

    // ── Read-only scroll state ────────────────────────────────────────────

    readonly property bool atBottom: listView.atYEnd
    readonly property bool atTop:    listView.atYBeginning
    readonly property real contentY: listView.contentY
    readonly property int  count:    listView.count

    // ── Methods ──────────────────────────────────────────────────────────

    // positionViewAt{End,Beginning}() stop at the last/first row and leave the
    // margins out of view, so finish the job by hand. The position call still
    // has to happen first: it creates the end delegates that make contentHeight
    // exact.
    function scrollToBottom() {
        listView.positionViewAtEnd()
        listView.contentY = Math.max(root._minContentY(), root._maxContentY())
    }
    function scrollToTop() {
        listView.positionViewAtBeginning()
        listView.contentY = root._minContentY()
    }

    function _minContentY() {
        return listView.originY - listView.topMargin
    }
    function _maxContentY() {
        return listView.originY + listView.contentHeight + listView.bottomMargin - listView.height
    }

    function roleValueAt(row, role) {
        if (!root.listModel || row < 0 || row >= listView.count) return undefined
        const data = root.listModel.get(row)
        return data ? data[role] : undefined
    }

    function _entryAt(row) {
        return root.entryRole !== "" ? root.roleValueAt(row, root.entryRole) : row
    }

    // An entry's rows are contiguous, so gallop out from `row` and bisect the
    // boundary: O(log n) lookups even for a reply tens of thousands of lines long.
    function _entryBounds(row) {
        if (root.entryRole === "") return { first: row, last: row }
        const entry = root._entryAt(row)
        const inEntry = function(r) { return root._entryAt(r) === entry }
        const edge = function(direction, limit) {
            let inside = row
            let step = 1
            while ((inside + direction * step - limit) * direction <= 0
                   && inEntry(inside + direction * step)) {
                inside += direction * step
                step *= 2
            }
            let outside = inside + direction * step
            if ((outside - limit) * direction > 0) outside = limit + direction
            while (Math.abs(outside - inside) > 1) {
                const middle = Math.floor((inside + outside) / 2)
                if (inEntry(middle)) inside = middle
                else outside = middle
            }
            return inside
        }
        return { first: edge(-1, 0), last: edge(1, listView.count - 1) }
    }

    function entryTextAt(row) {
        if (row < 0 || row >= listView.count) return ""
        const bounds = root._entryBounds(row)
        const lines = []
        for (let r = bounds.first; r <= bounds.last; ++r) lines.push(root._plainTextOf(r))
        return lines.join("\n")
    }

    function _normalizedSelection(anchor, focus) {
        if (!anchor || !focus || (anchor.row === focus.row && anchor.pos === focus.pos)) return null
        const forward = anchor.row < focus.row || (anchor.row === focus.row && anchor.pos < focus.pos)
        const start = forward ? anchor : focus
        const end = forward ? focus : anchor
        return { startRow: start.row, startPos: start.pos, endRow: end.row, endPos: end.pos }
    }

    // The slice of `row` inside `selection`, as {start, end}; end -1 means to
    // the end of the row.
    function _rangeInRow(row, selection) {
        if (!selection || row < selection.startRow || row > selection.endRow) return null
        return {
            start: row === selection.startRow ? selection.startPos : 0,
            end: row === selection.endRow ? selection.endPos : -1
        }
    }

    function _selectionContains(point) {
        const selection = root._selection
        if (!selection || point.row < selection.startRow || point.row > selection.endRow) return false
        if (point.row === selection.startRow && point.pos < selection.startPos) return false
        if (point.row === selection.endRow && point.pos > selection.endPos) return false
        return true
    }

    function selectRange(startRow, startPos, endRow, endPos) {
        root._selAnchor = { row: startRow, pos: startPos }
        root._selFocus = { row: endRow, pos: endPos }
    }

    function selectEntryAt(row) {
        if (row < 0 || row >= listView.count) return
        const bounds = root._entryBounds(row)
        root.selectRange(bounds.first, 0, bounds.last, root._plainTextOf(bounds.last).length)
    }

    function selectAll() {
        if (listView.count === 0) return
        const last = listView.count - 1
        root.selectRange(0, 0, last, root._plainTextOf(last).length)
    }

    function clearSelection() {
        root._selAnchor = null
        root._selFocus = null
    }

    function selectedText() {
        const selection = root._selection
        if (!selection) return ""
        const lines = []
        for (let row = selection.startRow; row <= selection.endRow; ++row) {
            const text = root._plainTextOf(row)
            const range = root._rangeInRow(row, selection)
            lines.push(text.slice(range.start, range.end < 0 ? text.length : range.end))
        }
        return lines.join("\n")
    }

    function copySelection() {
        if (root.hasSelection) Clipboard.setText(root.selectedText())
    }

    // The {row, pos} under a point in the view, clamped into the view so a
    // drag past an edge still lands on the edge row.
    function _hitTest(x, y) {
        if (listView.count === 0) return null
        const viewY = Math.max(0, Math.min(listView.height - 1, y))
        const contentX = listView.width / 2
        const contentY = viewY + listView.contentY
        let row = listView.indexAt(contentX, contentY)
        if (row < 0) {
            const lastRow = listView.count - 1
            const last = listView.itemAtIndex(lastRow)
            if (last && contentY >= last.y)
                return { row: lastRow, pos: root._plainTextOf(lastRow).length }
            const first = listView.itemAtIndex(0)
            if (first && contentY < first.y) return { row: 0, pos: 0 }
            row = listView.indexAt(contentX, contentY + root.rowSpacing)
            if (row < 0) return null
            return { row: row, pos: 0 }
        }
        const item = listView.itemAtIndex(row)
        if (!item) return null
        const local = item.contentEditor.mapFromItem(pointerArea, x, viewY)
        return { row: row, pos: item.contentEditor.positionAt(local.x, local.y) }
    }

    // Words are runs of anything but whitespace and JSON punctuation, so a
    // double-click on a quoted hash or address takes it without the quotes.
    function _selectWordAt(point) {
        const text = root._plainTextOf(point.row)
        const isWordChar = function(c) { return !/[\s"',:{}\[\]()]/.test(c) }
        let start = Math.min(point.pos, text.length)
        let end = start
        if (start < text.length && isWordChar(text[start])) {
            while (start > 0 && isWordChar(text[start - 1])) --start
            while (end < text.length && isWordChar(text[end])) ++end
        } else if (start > 0 && isWordChar(text[start - 1])) {
            while (start > 0 && isWordChar(text[start - 1])) --start
        } else {
            end = Math.min(start + 1, text.length)
        }
        root.selectRange(point.row, start, point.row, end)
    }

    function _extendSelectionTo(x, y) {
        const hit = root._hitTest(x, y)
        if (hit && root._selAnchor) root._selFocus = hit
    }

    function scheduleSearchRefresh(resetCurrent) {
        root._resetSearchOnRefresh = root._resetSearchOnRefresh || resetCurrent
        searchRefreshTimer.restart()
    }

    // Plain text of row `index`, in the coordinates the delegate's TextEdit
    // reports. Falls back to undoing the markup when the model has no plain role.
    function _plainTextOf(index) {
        if (!root.listModel || typeof root.listModel.get !== "function") return ""
        const data = root.listModel.get(index)
        if (!data) return ""
        if (root.plainContentRole !== "" && data[root.plainContentRole] !== undefined)
            return String(data[root.plainContentRole])
        const markup = String(data[root.contentRole] ?? "")
        if (root.contentTextFormat === Text.PlainText) return markup
        return markup.replace(/<br\s*\/?>/gi, "\n")
                     .replace(/<[^>]*>/g, "")
                     .replace(/&nbsp;/g, " ")
                     .replace(/&lt;/g, "<")
                     .replace(/&gt;/g, ">")
                     .replace(/&quot;/g, "\"")
                     .replace(/&#39;/g, "'")
                     .replace(/&amp;/g, "&")
    }

    function rebuildSearchMatches(resetCurrent) {
        const matches = []
        if (root.searchText.length > 0) {
            const pattern = root.searchText.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")
            const expression = new RegExp(pattern, "gi")
            // Scan model data, not delegates: off-screen rows have none.
            for (let row = 0; row < listView.count; ++row) {
                const plainText = root._plainTextOf(row)
                if (plainText.length === 0) continue
                expression.lastIndex = 0
                let match
                while ((match = expression.exec(plainText)) !== null) {
                    matches.push({
                        row: row,
                        start: match.index,
                        end: match.index + match[0].length
                    })
                }
            }
        }

        root._searchMatches = matches
        if (matches.length === 0) {
            root.currentSearchResultIndex = -1
        } else if (resetCurrent || root.currentSearchResultIndex < 0) {
            root.currentSearchResultIndex = 0
        } else {
            root.currentSearchResultIndex = Math.min(root.currentSearchResultIndex,
                                                     matches.length - 1)
        }
        root.applyCurrentSearchMatch()
    }

    function applyCurrentSearchMatch() {
        if (root.currentSearchResultIndex < 0
                || root.currentSearchResultIndex >= root._searchMatches.length) {
            root._currentMatch = null
            return
        }

        const match = root._searchMatches[root.currentSearchResultIndex]
        root._currentMatch = { row: match.row, start: match.start, end: match.end }
        // Bring the row into existence before asking it where the match sits.
        listView.positionViewAtIndex(match.row, ListView.Contain)
        root._scrollToCurrentMatchRect()
    }

    // Scroll to the occurrence itself, not merely its containing row: a row
    // wrapped over several visual lines can hold the match well below its top.
    function _scrollToCurrentMatchRect() {
        if (!root._currentMatch) return
        const item = listView.itemAtIndex(root._currentMatch.row)
        if (!item || !item.contentEditor) return
        const editor = item.contentEditor
        const startRect = editor.positionToRectangle(root._currentMatch.start)
        const endRect = editor.positionToRectangle(Math.max(root._currentMatch.start,
                                                            root._currentMatch.end - 1))
        const matchTop = item.y + editor.y + startRect.y
        const matchBottom = item.y + editor.y + endRect.y + endRect.height
        if (matchTop < listView.contentY) {
            listView.contentY = matchTop
        } else if (matchBottom > listView.contentY + listView.height) {
            listView.contentY = matchBottom - listView.height
        }
        listView.returnToBounds()
    }

    function showNextSearchResult() {
        if (root.searchResultCount === 0) return
        root.clearSelection()
        root.currentSearchResultIndex = (root.currentSearchResultIndex + 1)
            % root.searchResultCount
        root.applyCurrentSearchMatch()
    }

    function showPreviousSearchResult() {
        if (root.searchResultCount === 0) return
        root.clearSelection()
        root.currentSearchResultIndex = (root.currentSearchResultIndex
                                         + root.searchResultCount - 1)
            % root.searchResultCount
        root.applyCurrentSearchMatch()
    }

    onSearchTextChanged: {
        root.clearSelection()
        scheduleSearchRefresh(true)
    }
    onListModelChanged: scheduleSearchRefresh(true)

    Timer {
        id: searchRefreshTimer
        interval: 0
        repeat: false
        onTriggered: {
            const resetCurrent = root._resetSearchOnRefresh
            root._resetSearchOnRefresh = false
            root.rebuildSearchMatches(resetCurrent)
        }
    }

    Connections {
        target: root.listModel
        ignoreUnknownSignals: true
        function onRowsInserted() { root._onRowsChanged() }
        function onRowsRemoved()  { root._onRowsShifted() }
        function onModelReset()   { root._onRowsShifted() }
        function onDataChanged()  { root.scheduleSearchRefresh(false) }
    }

    function _onRowsChanged() {
        root.scheduleSearchRefresh(false)
        if (root.autoScrollToBottom && root.searchText.length === 0) tailTimer.restart()
    }

    // Row indices held by the selection no longer point at the same lines.
    function _onRowsShifted() {
        root.clearSelection()
        root._activeRow = -1
        root._onRowsChanged()
    }

    function _onLayoutChanged() {
        if (root._followingTail && root.autoScrollToBottom && root.searchText.length === 0)
            tailTimer.restart()
    }

    // Following the tail waits for the new rows to be laid out, and ListView
    // refines its content height as it goes, so settle over two passes.
    Timer {
        id: tailTimer
        interval: 0
        repeat: false
        onTriggered: {
            root.scrollToBottom()
            Qt.callLater(function() {
                if (root.autoScrollToBottom && root.searchText.length === 0)
                    root.scrollToBottom()
            })
        }
    }

    // ── Signal ───────────────────────────────────────────────────────────

    signal scrolled(real y)
    // Right-click at (x, y) in the view's coordinates; row is -1 off the rows.
    signal contextMenuRequested(int row, real x, real y)

    Keys.onPressed: (event) => {
        if (event.matches(StandardKey.Copy)) {
            if (!root.hasSelection) return
            root.copySelection()
        } else if (event.matches(StandardKey.SelectAll)) {
            if (root._activeRow >= 0 && root._activeRow < listView.count) root.selectEntryAt(root._activeRow)
            else root.selectAll()
        } else if (event.key === Qt.Key_Escape && root.hasSelection) {
            root.clearSelection()
        } else {
            return
        }
        event.accepted = true
    }

    // ── Font metrics (shared across all delegates) ───────────────────────

    TextMetrics {
        id: leftMetrics
        font.family: root.fontFamily
        font.styleName: root.fontStyleName
        font.pixelSize: root.fontPixelSize
        text: root.leftColumnSample
    }
    TextMetrics {
        id: rightMetrics
        font.family: root.fontFamily
        font.styleName: root.fontStyleName
        font.pixelSize: root.fontPixelSize
        text: root.rightColumnSample
    }

    // ── Layout ───────────────────────────────────────────────────────────

    Component {
        id: headerWrapper
        Item {
            width: listView.width
            height: headerLoader.height
            Loader {
                id: headerLoader
                x: root.horizontalPadding
                width: listView.width - (root.horizontalPadding * 2)
                sourceComponent: root.header
            }
        }
    }

    ListView {
        id: listView
        objectName: root.objectName.length > 0 ? root.objectName + "_list" : ""
        anchors.fill: parent
        clip: true
        model: root.listModel
        spacing: root.rowSpacing
        topMargin: root.topPadding
        bottomMargin: root.bottomPadding
        boundsBehavior: Flickable.StopAtBounds
        header: root.header ? headerWrapper : null

        reuseItems: true
        cacheBuffer: Math.max(400, height)

        ScrollBar.vertical: ScrollBar {
            id: verticalScrollBar
            policy: ScrollBar.AsNeeded
            minimumSize: 0.05
        }

        onContentYChanged: {
            root._followingTail = atYEnd
            root.scrolled(contentY)
        }
        onContentHeightChanged: root._onLayoutChanged()
        onHeightChanged: root._onLayoutChanged()

        delegate: RowLayout {
            id: rowRoot

            // `model` is the delegate's implicit context property; marking it
            // required pins the reference so model[roleName] resolves reliably.
            required property var model
            required property int index
            readonly property string rowContent: rowRoot.model[root.contentRole] ?? ""
            readonly property int rowCategory: root.categoryRole !== ""
                                               ? Number(rowRoot.model[root.categoryRole] ?? -1)
                                               : -1
            readonly property bool useCategoryColors: root.categoryRole !== ""
            readonly property color effectiveContentColor: !useCategoryColors
                                                          ? root.contentColor
                                                          : rowCategory === root.requestCategory
                                                            ? root.requestContentColor
                                                            : rowCategory === root.errorCategory
                                                              ? root.errorContentColor
                                                              : rowCategory === root.replyCategory
                                                                ? root.replyContentColor
                                                                : root.contentColor
            readonly property color effectiveLeftColumnColor: !useCategoryColors
                                                            ? root.leftColumnColor
                                                            : rowCategory === root.requestCategory
                                                              ? root.requestLeftColumnColor
                                                              : rowCategory === root.errorCategory
                                                                ? root.errorLeftColumnColor
                                                                : rowCategory === root.replyCategory
                                                                  ? root.replyLeftColumnColor
                                                                  : root.leftColumnColor
            readonly property var rowEntry: root.entryRole !== ""
                                            ? rowRoot.model[root.entryRole] : rowRoot.index
            property alias contentEditor: contentTextEditor

            objectName: root.objectName.length > 0 ? root.objectName + "_row_" + index : ""
            x: root.horizontalPadding
            width: listView.width - (root.horizontalPadding * 2)
            spacing: root.columnSpacing

            Accessible.role: Accessible.ListItem
            Accessible.name: root.plainContentRole !== ""
                             ? (rowRoot.model[root.plainContentRole] ?? "")
                             : rowRoot.rowContent

            // Driven from the view rather than applied to a captured editor:
            // the row may not exist when the selection or match is made, and
            // may be recycled away and back while it stands.
            readonly property var selectionRange: root._rangeInRow(rowRoot.index, root._selection)
            readonly property var currentMatch: root._currentMatch
            onSelectionRangeChanged: rowRoot.syncSelection()
            onCurrentMatchChanged: rowRoot.syncSelection()
            Component.onCompleted: rowRoot.syncSelection()
            ListView.onReused: rowRoot.syncSelection()

            function syncSelection() {
                const range = rowRoot.selectionRange
                const match = root._currentMatch
                if (range) {
                    contentTextEditor.select(range.start,
                                             range.end < 0 ? contentTextEditor.length : range.end)
                } else if (match && match.row === rowRoot.index) {
                    contentTextEditor.select(match.start, match.end)
                } else if (contentTextEditor.selectionStart !== contentTextEditor.selectionEnd) {
                    contentTextEditor.deselect()
                }
            }

            // Left column (optional)
            Text {
                objectName: root.objectName.length > 0 ? root.objectName + "_left_" + rowRoot.index : ""
                visible: root.leftColumnRole !== ""
                text: root.leftColumnRole !== ""
                      ? (rowRoot.model[root.leftColumnRole] ?? "") : ""
                font.family: root.fontFamily
                font.styleName: root.fontStyleName
                font.pixelSize: root.fontPixelSize
                lineHeight: root.textLineHeight > 0 ? root.textLineHeight : 1.0
                lineHeightMode: root.textLineHeight > 0 ? Text.FixedHeight : Text.ProportionalHeight
                color: rowRoot.effectiveLeftColumnColor
                Layout.minimumWidth: Math.ceil(Math.max(leftMetrics.width, implicitWidth))
                Layout.preferredWidth: Math.max(root.leftColumnWidth, Layout.minimumWidth)
                Layout.alignment: Qt.AlignTop
                wrapMode: Text.NoWrap
            }

            // Main content column. The TextEdit paints the selection; on
            // desktop the view's pointer layer decides what is selected.
            TextEdit {
                id: contentTextEditor
                objectName: root.objectName.length > 0 ? root.objectName + "_content_" + rowRoot.index : ""
                text: rowRoot.rowContent
                readOnly: true
                selectByMouse: !root._pointerSelection
                activeFocusOnPress: !root._pointerSelection
                persistentSelection: true
                textFormat: root.contentTextFormat
                wrapMode: Text.WrapAnywhere
                font.family: root.fontFamily
                font.styleName: root.fontStyleName
                font.pixelSize: root.fontPixelSize
                color: rowRoot.effectiveContentColor
                selectionColor: root.selectionColor
                selectedTextColor: root.selectedTextColor
                // Recycling swaps the text, which resets the selection.
                onTextChanged: rowRoot.syncSelection()

                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
            }

            // Right column (optional)
            Text {
                objectName: root.objectName.length > 0 ? root.objectName + "_right_" + rowRoot.index : ""
                visible: root.rightColumnRole !== ""
                text: root.rightColumnRole !== ""
                      ? (rowRoot.model[root.rightColumnRole] ?? "") : ""
                font.family: root.fontFamily
                font.styleName: root.fontStyleName
                font.pixelSize: root.fontPixelSize
                lineHeight: root.textLineHeight > 0 ? root.textLineHeight : 1.0
                lineHeightMode: root.textLineHeight > 0 ? Text.FixedHeight : Text.ProportionalHeight
                color: root.rightColumnColor
                Layout.preferredWidth: root.rightColumnWidth > 0 ? root.rightColumnWidth : rightMetrics.width
                Layout.alignment: Qt.AlignTop
                horizontalAlignment: Text.AlignRight
                wrapMode: Text.NoWrap
            }
        }
    }

    MouseArea {
        id: pointerArea
        objectName: root.objectName.length > 0 ? root.objectName + "_pointer" : ""
        anchors.fill: listView
        anchors.rightMargin: verticalScrollBar.size < 1 ? verticalScrollBar.width : 0
        enabled: root._pointerSelection
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: true
        cursorShape: Qt.IBeamCursor

        property int tripleClickRow: -1

        onWheel: (wheel) => { wheel.accepted = false }

        onPressed: (mouse) => {
            root.forceActiveFocus()
            const hit = root._hitTest(mouse.x, mouse.y)
            root._activeRow = hit ? hit.row : -1
            if (mouse.button === Qt.RightButton) {
                if (!hit || !root._selectionContains(hit)) root.clearSelection()
                root.contextMenuRequested(hit ? hit.row : -1, mouse.x, mouse.y)
                return
            }
            if (!hit) {
                root.clearSelection()
            } else if ((mouse.modifiers & Qt.ShiftModifier) && root._selAnchor) {
                root._selFocus = hit
            } else if (tripleClickTimer.running && hit.row === tripleClickRow) {
                root.selectRange(hit.row, 0, hit.row, root._plainTextOf(hit.row).length)
            } else {
                root._selAnchor = hit
                root._selFocus = hit
            }
        }
        onDoubleClicked: (mouse) => {
            if (mouse.button !== Qt.LeftButton) return
            const hit = root._hitTest(mouse.x, mouse.y)
            if (!hit) return
            root._selectWordAt(hit)
            tripleClickRow = hit.row
            tripleClickTimer.restart()
        }
        onPositionChanged: (mouse) => {
            if (!(pressedButtons & Qt.LeftButton)) return
            root._extendSelectionTo(mouse.x, mouse.y)
            autoScrollTimer.running = mouse.y < 0 || mouse.y > height
        }
        onReleased: autoScrollTimer.stop()
        onCanceled: autoScrollTimer.stop()

        Timer {
            id: tripleClickTimer
            interval: Qt.styleHints.mouseDoubleClickInterval
        }

        // Scroll while a drag selection is held past the top or bottom edge,
        // faster the further out the pointer is.
        Timer {
            id: autoScrollTimer
            interval: 16
            repeat: true
            onTriggered: {
                const y = pointerArea.mouseY
                const overshoot = y < 0 ? y : y - pointerArea.height
                const step = Math.max(-60, Math.min(60, overshoot / 2))
                listView.contentY = Math.max(root._minContentY(),
                                             Math.min(Math.max(root._minContentY(), root._maxContentY()),
                                                      listView.contentY + step))
                root._extendSelectionTo(pointerArea.mouseX, y)
            }
        }
    }
}
