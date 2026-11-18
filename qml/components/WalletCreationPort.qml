// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15

// The popup implementation is URL-loaded only when wallet presentation is
// enabled. This bounded presentation port never constructs a wallet model.
Item {
    property bool modalVisible: false
    signal openRequested()
    signal onboardingOpenRequested()
    signal finished(bool openActivity, QtObject wallet)
}
