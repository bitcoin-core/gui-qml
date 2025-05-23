// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import "../controls"

Rectangle {
    height: 1
    color: enabled ? Theme.color.neutral5 : Theme.color.neutral4

    Behavior on color {
        ColorAnimation { duration: 150 }
    }
}
