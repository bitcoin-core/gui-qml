// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

pragma Singleton
import QtQuick 2.15

QtObject {
    readonly property int compact: 0
    readonly property int regular: 1

    // Widths below compactWidthMax are compact: pages switch to compact
    // padding and labels there, and split views show one column.
    property real compactWidthMax: 640
    property real compactHeightMax: 500
    // From here on, layouts use their wide arrangement (extra page padding, more columns).
    property real wideWidthMin: 900
    // Two-column button and value grids narrower than this stack into one column.
    property real stackColumnsMax: 480
    // The design system page's Mobile preview lowers the main window's minimum
    // width to a phone's, so compact layouts can be checked on the desktop.
    property bool mobilePreview: false
    readonly property real windowMinimumWidth: mobilePreview ? 360 : 800

    function isCompact(width) {
        return width < compactWidthMax
    }

    function widthClassFor(width) {
        return isCompact(width) ? compact : regular
    }

    function heightClassFor(height) {
        return height <= compactHeightMax ? compact : regular
    }
}
