// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

Image {
    id: root

    property string code: ""
    property color backgroundColor: "black"
    property color foregroundColor: "white"

    fillMode: Image.PreserveAspectFit
    smooth: false
    // All legal QR payloads fit this bound. Check length before the
    // presentation binding percent-encodes a potentially large legacy value.
    source: code.length === 0 || code.length > 32768 ? "" : `image://qr/${encodeURIComponent(root.code)}?&fg=${encodeURIComponent(root.foregroundColor)}&bg=${encodeURIComponent(root.backgroundColor)}`
}
