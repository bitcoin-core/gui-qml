// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import "WidgetMetrics.js" as WidgetMetrics

// Content contract. WidgetFrame supplies these values; placement and saving
// belong to the dashboard. Bind expensive visual work to active when needed.
Item {
    property int columnSpan: 1
    property int rowSpan: 1
    property bool active: true
    // The host supplies the actual cell size. The fallback also supports previews.
    property real cellSize: Math.min((width + 2 * WidgetMetrics.contentPadding - (columnSpan - 1) * 12) / columnSpan,
                                    (height + 2 * WidgetMetrics.contentPadding - (rowSpan - 1) * 12) / rowSpan)
    readonly property real fontScale: Math.max(0.85, cellSize / 160)
    function scaledFont(style) {
        return Qt.font({family: style.family, styleName: style.styleName, pixelSize: scaledPixelSize(style)})
    }
    function scaledPixelSize(style) { return Math.round(style.pixelSize * fontScale) }

    readonly property bool compact: columnSpan === 1 && rowSpan === 1
    readonly property bool expanded: rowSpan > 1 && width >= 340 && height >= 200
}
