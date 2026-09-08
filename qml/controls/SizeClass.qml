// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

pragma Singleton
import QtQuick 2.15

QtObject {
    readonly property int compact: 0
    readonly property int regular: 1

    property real compactWidthMax: 600
    property real compactHeightMax: 500

    function widthClassFor(w)  { return w  <= compactWidthMax  ? compact : regular }
    function heightClassFor(h) { return h <= compactHeightMax ? compact : regular }
}
