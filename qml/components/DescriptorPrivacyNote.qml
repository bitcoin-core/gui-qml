// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Layouts 1.15

import "../controls"

CoreText {
    text: qsTr("Anyone with these descriptors can see all of this wallet's addresses and transactions, but cannot spend from it.")
    textStyle: Theme.text.description
    color: Theme.color.neutral7
    horizontalAlignment: Text.AlignLeft
}
