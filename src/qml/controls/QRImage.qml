// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

Image {
    id: root

    property string code: ""
    property color backgroundColor: Qt.black
    property color foregroundColor: Qt.white

    fillMode: Image.PreserveAspectFit
    smooth: false
    source: `image://qr/${encodeURIComponent(root.code)}?&fg=${encodeURIComponent(root.foregroundColor)}&bg=${encodeURIComponent(root.backgroundColor)}`
}
