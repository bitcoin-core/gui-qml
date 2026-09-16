// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

pragma Singleton
import QtQuick 2.15

QtObject {
    property string currentText: ""

    signal dataChanged()

    function text() { return currentText }
    function setText(value) {
        currentText = value
        dataChanged()
    }
}
