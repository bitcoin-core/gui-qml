// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtTest 1.2

TestCase {
    function waitForLayout(item) {
        // The C++ layout-wait API predates QML's Qt 6.5 waitForPolish helper.
        verify(renderTestHelper.waitForLayout(item))
    }
}
