// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

// Matching property names alone must not satisfy the content contract.
Item {
    property int columnSpan: 1
    property int rowSpan: 1
    property bool active: true
}
