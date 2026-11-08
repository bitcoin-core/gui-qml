// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

RowLayout {
    id: rowRoot
    signal refreshRequested()
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
    required property string viewFontStyleName
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

    // `model` is the Repeater delegate's implicit context
    // property; marking it required pins the reference so
    // bracket lookup (model[roleName]) resolves reliably.
    required property var model
    required property int index
    readonly property string rowContent: rowRoot.model[rowRoot.viewContentRole] ?? ""
    onRowContentChanged: rowRoot.refreshRequested()
    readonly property int rowCategory: rowRoot.viewCategoryRole !== ""
                                       ? Number(rowRoot.model[rowRoot.viewCategoryRole] ?? -1)
                                       : -1
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
    Accessible.name: rowContent

    // Left column (optional)
    Text {
        objectName: rowRoot.viewObjectName.length > 0 ? rowRoot.viewObjectName + "_left_" + rowRoot.index : ""
        visible: rowRoot.viewLeftColumnRole !== ""
        text: rowRoot.viewLeftColumnRole !== ""
              ? (rowRoot.model[rowRoot.viewLeftColumnRole] ?? "") : ""
        font.family: rowRoot.viewFontFamily
        font.styleName: rowRoot.viewFontStyleName
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
        readOnly: true
        selectByMouse: true
        persistentSelection: rowRoot.viewSearchText.length > 0
        textFormat: rowRoot.viewContentTextFormat
        wrapMode: Text.WrapAnywhere
        font.family: rowRoot.viewFontFamily
        font.styleName: rowRoot.viewFontStyleName
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
        text: rowRoot.viewRightColumnRole !== ""
              ? (rowRoot.model[rowRoot.viewRightColumnRole] ?? "") : ""
        font.family: rowRoot.viewFontFamily
        font.styleName: rowRoot.viewFontStyleName
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
