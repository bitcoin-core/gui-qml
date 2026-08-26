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
// Architecture: Flickable + Column + Repeater. Column sums child heights
// exactly (no ListView estimation) so the scrollbar stays stable on long
// rows. Trade-off: no virtualization — callers must bound the model size.
//
// The public model property is named "listModel" (not "model") to avoid
// shadowing the Repeater delegate's implicit "model" context property.
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
    readonly property string normalizedSearchText: searchText.toLowerCase()
    readonly property int searchResultCount: _searchMatches.length
    property int currentSearchResultIndex: -1
    property var _searchMatches: []
    property var _selectedSearchEditor: null
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

    readonly property bool atBottom: flick.contentHeight <= flick.height ||
                                     flick.contentY + flick.height >= flick.contentHeight - 1
    readonly property bool atTop:    flick.contentY <= 0
    readonly property real contentY: flick.contentY
    readonly property int  count:    rowRepeater.count

    // ── Methods ──────────────────────────────────────────────────────────

    function scrollToBottom() {
        if (flick.contentHeight > flick.height) {
            flick.contentY = flick.contentHeight - flick.height
        } else {
            flick.contentY = 0
        }
        flick.returnToBounds()
    }
    function scrollToTop() {
        flick.contentY = 0
        flick.returnToBounds()
    }

    function scheduleSearchRefresh(resetCurrent) {
        root._resetSearchOnRefresh = root._resetSearchOnRefresh || resetCurrent
        searchRefreshTimer.restart()
    }

    function rebuildSearchMatches(resetCurrent) {
        const matches = []
        if (root.normalizedSearchText.length > 0) {
            for (let row = 0; row < rowRepeater.count; ++row) {
                const item = rowRepeater.itemAt(row)
                if (!item || !item.contentEditor) continue
                const editor = item.contentEditor
                const plainText = editor.getText(0, editor.length)
                const normalized = plainText.toLowerCase()
                let offset = 0
                while (offset <= normalized.length - root.normalizedSearchText.length) {
                    const matchOffset = normalized.indexOf(root.normalizedSearchText, offset)
                    if (matchOffset < 0) break
                    matches.push({
                        row: row,
                        start: matchOffset,
                        end: matchOffset + root.searchText.length
                    })
                    offset = matchOffset + Math.max(1, root.normalizedSearchText.length)
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
        if (root._selectedSearchEditor) {
            root._selectedSearchEditor.deselect()
            root._selectedSearchEditor = null
        }
        if (root.currentSearchResultIndex < 0
                || root.currentSearchResultIndex >= root._searchMatches.length) return

        const match = root._searchMatches[root.currentSearchResultIndex]
        const item = rowRepeater.itemAt(match.row)
        if (!item || !item.contentEditor) return
        const editor = item.contentEditor
        editor.select(match.start, match.end)
        root._selectedSearchEditor = editor

        // Scroll to the occurrence itself, not merely its containing row. A
        // console response can span many lines, with the match near the end.
        const startRect = editor.positionToRectangle(match.start)
        const endRect = editor.positionToRectangle(Math.max(match.start, match.end - 1))
        const matchTop = item.y + editor.y + startRect.y
        const matchBottom = item.y + editor.y + endRect.y + endRect.height
        if (matchTop < flick.contentY) {
            flick.contentY = Math.max(0, matchTop)
        } else if (matchBottom > flick.contentY + flick.height) {
            flick.contentY = Math.max(0, matchBottom - flick.height)
        }
        flick.returnToBounds()
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

    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: contentColumn.height
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            minimumSize: 0.05
        }

        onContentYChanged: root.scrolled(contentY)

        Column {
            id: contentColumn
            objectName: root.objectName.length > 0 ? root.objectName + "_contentColumn" : ""
            x: root.horizontalPadding
            width: flick.width - (root.horizontalPadding * 2)
            topPadding: root.topPadding
            bottomPadding: root.bottomPadding
            spacing: root.rowSpacing

            Loader {
                width: contentColumn.width
                sourceComponent: root.header
            }

            Repeater {
                id: rowRepeater
                model: root.listModel

                onItemAdded: root.scheduleSearchRefresh(false)
                onItemRemoved: root.scheduleSearchRefresh(false)

                delegate: RowLayout {
                    id: rowRoot

                    // `model` is the Repeater delegate's implicit context
                    // property; marking it required pins the reference so
                    // bracket lookup (model[roleName]) resolves reliably.
                    required property var model
                    required property int index
                    readonly property string rowContent: rowRoot.model[root.contentRole] ?? ""
                    onRowContentChanged: root.scheduleSearchRefresh(false)
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
                    width: contentColumn.width
                    spacing: root.columnSpacing

                    Accessible.role: Accessible.ListItem
                    Accessible.name: rowContent

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
                        Layout.preferredWidth: root.leftColumnWidth > 0 ? root.leftColumnWidth : leftMetrics.width
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
                        persistentSelection: root.normalizedSearchText.length > 0
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
    }

    // Auto-scroll to bottom when content grows. Listening to
    // onContentHeightChanged (rather than Repeater.onItemAdded) ensures the
    // Column has already laid out the new delegate, so contentHeight is
    // accurate and the scroll reaches the true bottom.
    Connections {
        target: flick
        // While searching, navigation owns the viewport position. Otherwise a
        // content relayout can pull the view back to the bottom immediately
        // after applyCurrentSearchMatch() scrolls to the active occurrence.
        enabled: root.autoScrollToBottom && root.normalizedSearchText.length === 0
        function onContentHeightChanged() {
            root.scrollToBottom()
        }
    }
}
