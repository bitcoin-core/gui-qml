// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    property string text: ""
    property var shortcut: ""
    property bool enabled: true
    property bool visible: true

    signal triggered()

    function trigger() {
        if (visible && enabled) {
            triggered()
        }
    }
}
