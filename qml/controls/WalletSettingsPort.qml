// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

// Shared settings retain their attachment point without resolving wallet pages.
Item {
    property QtObject wallet: null
    signal selectWalletRequested()
    signal resetRequested()
}
