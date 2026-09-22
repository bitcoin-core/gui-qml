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
        root.currentSearchResultIndex = (root.currentSearchResultIndex + 1)
            % root.searchResultCount
        root.applyCurrentSearchMatch()
    }

    function showPreviousSearchResult() {
        if (root.searchResultCount === 0) return
        root.currentSearchResultIndex = (root.currentSearchResultIndex
                                         + root.searchResultCount - 1)
            % root.searchResultCount
        root.applyCurrentSearchMatch()
    }

    onSearchTextChanged: scheduleSearchRefresh(true)
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
        function onRowsRemoved()  { root._onRowsChanged() }
        function onModelReset()   { root._onRowsChanged() }
        function onDataChanged()  { root.scheduleSearchRefresh(false) }
    }

    function _onRowsChanged() {
        root.scheduleSearchRefresh(false)
        if (root.autoScrollToBottom && root.searchText.length === 0) tailTimer.restart()
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
            policy: ScrollBar.AsNeeded
            minimumSize: 0.05
        }

        onContentYChanged: root.scrolled(contentY)

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
            // the row holding the match may not exist when the match is found,
            // and may be recycled away and back while stepping through results.
            readonly property var currentMatch: root._currentMatch
            onCurrentMatchChanged: rowRoot.syncSearchSelection()
            Component.onCompleted: rowRoot.syncSearchSelection()
            ListView.onReused: rowRoot.syncSearchSelection()

            function syncSearchSelection() {
                const match = root._currentMatch
                if (match && match.row === rowRoot.index) {
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

            // Main content column: TextEdit for per-row select + copy.
            TextEdit {
                id: contentTextEditor
                objectName: root.objectName.length > 0 ? root.objectName + "_content_" + rowRoot.index : ""
                text: rowRoot.rowContent
                readOnly: true
                selectByMouse: true
                persistentSelection: root.searchText.length > 0
                textFormat: root.contentTextFormat
                wrapMode: Text.WrapAnywhere
                font.family: root.fontFamily
                font.styleName: root.fontStyleName
                font.pixelSize: root.fontPixelSize
                color: rowRoot.effectiveContentColor
                selectionColor: root.selectionColor
                selectedTextColor: root.selectedTextColor
                activeFocusOnPress: true

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
}
