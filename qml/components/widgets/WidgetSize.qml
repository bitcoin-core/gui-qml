// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQml 2.15

QtObject {
    required property int columns
    readonly property string label: columns === 1 && rows === 1 ? qsTr("Small")
        : columns === 3 ? qsTr("Large") : qsTr("Medium")
    required property int rows
}
