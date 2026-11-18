// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

RowLayout {
    id: rowRoot
    signal matchPositionReady(real top, real bottom)
    required property string viewCategoryRole
    required property int viewColumnSpacing
    required property color viewContentColor
    required property string viewContentRole
    required property int viewContentTextFormat
    required property int viewErrorCategory
    required property color viewErrorContentColor
    required property color viewErrorLeftColumnColor
    required property string viewFontFamily
    required property int viewFontPixelSize
    required property int viewFontWeight
    required property color viewLeftColumnColor
    required property string viewLeftColumnRole
    required property int viewLeftColumnWidth
    required property string viewObjectName
    required property int viewReplyCategory
    required property color viewReplyContentColor
    required property color viewReplyLeftColumnColor
    required property int viewRequestCategory
    required property color viewRequestContentColor
    required property color viewRequestLeftColumnColor
    required property color viewRightColumnColor
    required property string viewRightColumnRole
    required property int viewRightColumnWidth
    required property string viewSearchText
    required property color viewSelectedTextColor
    required property color viewSelectionColor
    required property int viewTextLineHeight
    required property real measuredLeftWidth
    required property real measuredRightWidth

    required property string content
    required property string plainContent
    required property string leftText
    required property string rightText
    required property int category
    required property int index
    required property int viewSearchRow
    required property int viewSearchStart
    required property int viewSearchEnd
    readonly property string rowContent: content
    readonly property int rowCategory: category
    function scheduleSearchSelection() { selectionTimer.restart() }
    function applySearchSelection() {
        if (index !== viewSearchRow || viewSearchStart < 0) { contentTextEditor.deselect(); return }
        contentTextEditor.select(viewSearchStart, viewSearchEnd)
        const startRect = contentTextEditor.positionToRectangle(viewSearchStart)
        const endRect = contentTextEditor.positionToRectangle(Math.max(viewSearchStart, viewSearchEnd - 1))
        matchPositionReady(y + contentTextEditor.y + startRect.y,
                           y + contentTextEditor.y + endRect.y + endRect.height)
    }
    onViewSearchRowChanged: scheduleSearchSelection()
    onViewSearchStartChanged: scheduleSearchSelection()
    onViewSearchEndChanged: scheduleSearchSelection()
    onIndexChanged: scheduleSearchSelection()
    Component.onCompleted: scheduleSearchSelection()
    ListView.onReused: scheduleSearchSelection()
    Timer { id: selectionTimer; interval: 0; repeat: false; onTriggered: rowRoot.applySearchSelection() }
    readonly property bool useCategoryColors: rowRoot.viewCategoryRole !== ""
    readonly property color effectiveContentColor: !useCategoryColors
                                                  ? rowRoot.viewContentColor
                                                  : rowCategory === rowRoot.viewRequestCategory
                                                    ? rowRoot.viewRequestContentColor
                                                    : rowCategory === rowRoot.viewErrorCategory
                                                      ? rowRoot.viewErrorContentColor
                                                      : rowCategory === rowRoot.viewReplyCategory
                                                        ? rowRoot.viewReplyContentColor
                                                        : rowRoot.viewContentColor
    readonly property color effectiveLeftColumnColor: !useCategoryColors
                                                    ? rowRoot.viewLeftColumnColor
                                                    : rowCategory === rowRoot.viewRequestCategory
                                                      ? rowRoot.viewRequestLeftColumnColor
                                                      : rowCategory === rowRoot.viewErrorCategory
                                                        ? rowRoot.viewErrorLeftColumnColor
                                                        : rowCategory === rowRoot.viewReplyCategory
                                                          ? rowRoot.viewReplyLeftColumnColor
                                                          : rowRoot.viewLeftColumnColor
    property alias contentEditor: contentTextEditor

    objectName: rowRoot.viewObjectName.length > 0 ? rowRoot.viewObjectName + "_row_" + index : ""
    spacing: rowRoot.viewColumnSpacing

    Accessible.role: Accessible.ListItem
    Accessible.name: plainContent

    // Left column (optional)
    Text {
        objectName: rowRoot.viewObjectName.length > 0 ? rowRoot.viewObjectName + "_left_" + rowRoot.index : ""
        visible: rowRoot.viewLeftColumnRole !== ""
        text: rowRoot.leftText
        font.family: rowRoot.viewFontFamily
        font.weight: rowRoot.viewFontWeight
        font.pixelSize: rowRoot.viewFontPixelSize
        lineHeight: rowRoot.viewTextLineHeight > 0 ? rowRoot.viewTextLineHeight : 1.0
        lineHeightMode: rowRoot.viewTextLineHeight > 0 ? Text.FixedHeight : Text.ProportionalHeight
        color: rowRoot.effectiveLeftColumnColor
        Layout.minimumWidth: Math.ceil(Math.max(rowRoot.measuredLeftWidth, implicitWidth))
        Layout.preferredWidth: Math.max(rowRoot.viewLeftColumnWidth, Layout.minimumWidth)
        Layout.alignment: Qt.AlignTop
        wrapMode: Text.NoWrap
    }

    // Main content column: TextEdit for per-row select + copy.
    TextEdit {
        id: contentTextEditor
        objectName: rowRoot.viewObjectName.length > 0 ? rowRoot.viewObjectName + "_content_" + rowRoot.index : ""
        text: rowRoot.rowContent
        onTextChanged: rowRoot.scheduleSearchSelection()
        onWidthChanged: if (rowRoot.index === rowRoot.viewSearchRow) rowRoot.scheduleSearchSelection()
        readOnly: true
        selectByMouse: true
        persistentSelection: rowRoot.viewSearchText.length > 0
        textFormat: rowRoot.viewContentTextFormat
        wrapMode: Text.WrapAnywhere
        font.family: rowRoot.viewFontFamily
        font.weight: rowRoot.viewFontWeight
        font.pixelSize: rowRoot.viewFontPixelSize
        color: rowRoot.effectiveContentColor
        selectionColor: rowRoot.viewSelectionColor
        selectedTextColor: rowRoot.viewSelectedTextColor
        activeFocusOnPress: true

        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
    }

    // Right column (optional)
    Text {
        objectName: rowRoot.viewObjectName.length > 0 ? rowRoot.viewObjectName + "_right_" + rowRoot.index : ""
        visible: rowRoot.viewRightColumnRole !== ""
        text: rowRoot.rightText
        font.family: rowRoot.viewFontFamily
        font.weight: rowRoot.viewFontWeight
        font.pixelSize: rowRoot.viewFontPixelSize
        lineHeight: rowRoot.viewTextLineHeight > 0 ? rowRoot.viewTextLineHeight : 1.0
        lineHeightMode: rowRoot.viewTextLineHeight > 0 ? Text.FixedHeight : Text.ProportionalHeight
        color: rowRoot.viewRightColumnColor
        Layout.preferredWidth: rowRoot.viewRightColumnWidth > 0 ? rowRoot.viewRightColumnWidth : rowRoot.measuredRightWidth
        Layout.alignment: Qt.AlignTop
        horizontalAlignment: Text.AlignRight
        wrapMode: Text.NoWrap
    }
}
