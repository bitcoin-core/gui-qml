// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import org.bitcoincore.qt 1.0

// Virtualized presentation over bounded, background-projected text chunks.
// Search reads immutable model data, including rows with no live delegate.
Item {
    id: root
    Accessible.role: Accessible.List
    Accessible.name: accessibleName
    property var listModel: null
    property string contentRole: "content"
    property string leftColumnRole: ""
    property string rightColumnRole: ""
    property string categoryRole: ""

    // ── Style ────────────────────────────────────────────────────────────

    property int   fontPixelSize:    12
    property string fontFamily:      Theme.text.monoFamily
    property int   fontWeight:       Theme.text.monoWeight
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
    property color selectionColor:    Theme.color.selection
    property color selectedTextColor: Theme.color.selectedText
    property string searchText: ""
    readonly property int searchResultCount: projection.searchResultCount
    readonly property int currentSearchResultIndex: projection.currentSearchResultIndex
    readonly property bool searchBusy: projection.searchBusy
    readonly property bool searchLimited: projection.searchLimited
    property bool _followingTail: true

    // ── Layout metrics ───────────────────────────────────────────────────

    property int horizontalPadding: Theme.space.l
    property int topPadding: Theme.space.l
    property int bottomPadding: Theme.space.l
    property int rowSpacing: Theme.space.xxs
    property int columnSpacing: Theme.space.m
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

    readonly property bool atBottom: outputList.atYEnd
    readonly property bool atTop: outputList.atYBeginning
    readonly property real contentY: outputList.contentY
    readonly property int count: projection.count
    readonly property int instantiatedRowCount: outputList.contentItem.children.length

    function copyAll() { projection.copyAll() }
    signal copyFinished(bool copied)
    function scrollToBottom() { _followingTail = true; outputList.positionViewAtEnd() }
    function scrollToTop() { _followingTail = false; outputList.positionViewAtBeginning() }
    function showNextSearchResult() { projection.nextSearchResult() }
    function showPreviousSearchResult() { projection.previousSearchResult() }
    function revealMatch(top, bottom) {
        if (top < outputList.contentY) outputList.contentY = top
        else if (bottom > outputList.contentY + outputList.height) outputList.contentY = bottom - outputList.height
        outputList.returnToBounds()
    }
    function positionCurrentMatch() {
        if (projection.currentSearchRow < 0) return
        outputList.positionViewAtIndex(projection.currentSearchRow, ListView.Contain)
        const item = outputList.itemAtIndex(projection.currentSearchRow) as MonospaceOutputRow
        if (item) item.scheduleSearchSelection()
    }
    signal scrolled(real y)

    MonospaceOutputModel {
        id: projection
        services: ApplicationModels.presentationServices
        sourceModel: root.listModel
        contentRole: root.contentRole
        leftRole: root.leftColumnRole
        rightRole: root.rightColumnRole
        categoryRole: root.categoryRole
        richText: root.contentTextFormat === Text.RichText
        active: root.visible
        searchText: root.searchText
        onCurrentMatchChanged: root.positionCurrentMatch()
        onCopyFinished: (copied) => root.copyFinished(copied)
    }

    TextMetrics {
        id: leftMetrics
        font.family: root.fontFamily
        font.weight: root.fontWeight
        font.pixelSize: root.fontPixelSize
        text: root.leftColumnSample
    }
    TextMetrics {
        id: rightMetrics
        font.family: root.fontFamily
        font.weight: root.fontWeight
        font.pixelSize: root.fontPixelSize
        text: root.rightColumnSample
    }

    ListView {
        id: outputList
        objectName: root.objectName.length > 0 ? root.objectName + "_list" : ""
        anchors.fill: parent
        clip: true
        model: projection
        reuseItems: true
        cacheBuffer: 0
        topMargin: root.topPadding
        bottomMargin: root.bottomPadding
        spacing: root.rowSpacing
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; minimumSize: 0.05 }
        onContentYChanged: root.scrolled(contentY)
        onMovementEnded: root._followingTail = atYEnd
        onCountChanged: if (root.autoScrollToBottom && root._followingTail && root.searchText.length === 0) positionViewAtEnd()
        onContentHeightChanged: if (root.autoScrollToBottom && root._followingTail && root.searchText.length === 0) positionViewAtEnd()
        header: Column {
            x: root.horizontalPadding
            width: Math.max(0, outputList.width - root.horizontalPadding * 2)
            Loader { width: parent.width; sourceComponent: root.header }
            Text {
                width: parent.width
                visible: projection.truncated || projection.searchLimited || projection.error.length > 0
                text: projection.error.length > 0 ? projection.error
                    : projection.searchLimited ? qsTr("Search reached the viewer match limit. Refine the search to see additional results.")
                    : qsTr("Some output is too long to show here. Use debug.log or bitcoin-cli for the full text.")
                wrapMode: Text.Wrap
                color: root.errorContentColor
                font.family: root.fontFamily
                font.pixelSize: root.fontPixelSize
            }
        }
                delegate: MonospaceOutputRow {
                    x: root.horizontalPadding
                    width: Math.max(0, outputList.width - root.horizontalPadding * 2)
                    viewCategoryRole: root.categoryRole
                    viewColumnSpacing: root.columnSpacing
                    viewContentColor: root.contentColor
                    viewContentRole: root.contentRole
                    viewContentTextFormat: root.contentTextFormat
                    viewErrorCategory: root.errorCategory
                    viewErrorContentColor: root.errorContentColor
                    viewErrorLeftColumnColor: root.errorLeftColumnColor
                    viewFontFamily: root.fontFamily
                    viewFontPixelSize: root.fontPixelSize
                    viewFontWeight: root.fontWeight
                    viewLeftColumnColor: root.leftColumnColor
                    viewLeftColumnRole: root.leftColumnRole
                    viewLeftColumnWidth: root.leftColumnWidth
                    viewObjectName: root.objectName
                    viewReplyCategory: root.replyCategory
                    viewReplyContentColor: root.replyContentColor
                    viewReplyLeftColumnColor: root.replyLeftColumnColor
                    viewRequestCategory: root.requestCategory
                    viewRequestContentColor: root.requestContentColor
                    viewRequestLeftColumnColor: root.requestLeftColumnColor
                    viewRightColumnColor: root.rightColumnColor
                    viewRightColumnRole: root.rightColumnRole
                    viewRightColumnWidth: root.rightColumnWidth
                    viewSearchText: root.searchText
                    viewSelectedTextColor: root.selectedTextColor
                    viewSelectionColor: root.selectionColor
                    viewTextLineHeight: root.textLineHeight
                    viewSearchRow: projection.currentSearchRow
                    viewSearchStart: projection.currentSearchStart
                    viewSearchEnd: projection.currentSearchEnd
                    onMatchPositionReady: (top, bottom) => root.revealMatch(top, bottom)
                    measuredLeftWidth: leftMetrics.width
                    measuredRightWidth: rightMetrics.width
                }
    }
}
