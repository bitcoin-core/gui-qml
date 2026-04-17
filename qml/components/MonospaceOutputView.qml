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

    // ── Style ────────────────────────────────────────────────────────────

    property int   fontPixelSize:    12
    property int   contentTextFormat: Text.PlainText
    property color contentColor:     Theme.color.neutral9
    property color leftColumnColor:  Theme.color.neutral5
    property color rightColumnColor: Theme.color.neutral5
    property color selectionColor:    Theme.color.orange
    property color selectedTextColor: Theme.color.white

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

    // ── Signal ───────────────────────────────────────────────────────────

    signal scrolled(real y)

    // ── Font metrics (shared across all delegates) ───────────────────────

    TextMetrics {
        id: leftMetrics
        font.family: "monospace"
        font.pixelSize: root.fontPixelSize
        text: root.leftColumnSample
    }
    TextMetrics {
        id: rightMetrics
        font.family: "monospace"
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
            x: 16
            width: flick.width - 32
            topPadding: 16
            bottomPadding: 16
            spacing: 2

            Loader {
                width: contentColumn.width
                sourceComponent: root.header
            }

            Repeater {
                id: rowRepeater
                model: root.listModel

                delegate: RowLayout {
                    id: rowRoot

                    // `model` is the Repeater delegate's implicit context
                    // property; marking it required pins the reference so
                    // bracket lookup (model[roleName]) resolves reliably.
                    required property var model

                    width: contentColumn.width
                    spacing: 8

                    Accessible.role: Accessible.ListItem
                    Accessible.name: rowRoot.model[root.contentRole] ?? ""

                    // Left column (optional)
                    Text {
                        visible: root.leftColumnRole !== ""
                        text: root.leftColumnRole !== ""
                              ? (rowRoot.model[root.leftColumnRole] ?? "") : ""
                        font.family: "monospace"
                        font.pixelSize: root.fontPixelSize
                        color: root.leftColumnColor
                        Layout.preferredWidth: leftMetrics.width
                        Layout.alignment: Qt.AlignTop
                        wrapMode: Text.NoWrap
                    }

                    // Main content column: TextEdit for per-row select + copy.
                    TextEdit {
                        text: rowRoot.model[root.contentRole] ?? ""
                        readOnly: true
                        selectByMouse: true
                        persistentSelection: false
                        textFormat: root.contentTextFormat
                        wrapMode: Text.WrapAnywhere
                        font.family: "monospace"
                        font.pixelSize: root.fontPixelSize
                        color: root.contentColor
                        selectionColor: root.selectionColor
                        selectedTextColor: root.selectedTextColor
                        activeFocusOnPress: true

                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                    }

                    // Right column (optional)
                    Text {
                        visible: root.rightColumnRole !== ""
                        text: root.rightColumnRole !== ""
                              ? (rowRoot.model[root.rightColumnRole] ?? "") : ""
                        font.family: "monospace"
                        font.pixelSize: root.fontPixelSize
                        color: root.rightColumnColor
                        Layout.preferredWidth: rightMetrics.width
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
        enabled: root.autoScrollToBottom
        function onContentHeightChanged() {
            root.scrollToBottom()
        }
    }
}
