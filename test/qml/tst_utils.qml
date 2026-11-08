// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2

import "../../qml/controls/utils.js" as Utils

TestCase {
    function init() {
        testSetup.failOnWarnings()
    }

    name: "Utils"

    // Map a peer count to the matching connection-strength icon. Thresholds
    // mirror the Qt Widgets GUI (connect_0 .. connect_4): 0, 1-3, 4-6, 7-9, 10+.
    function test_nodeConnectionIcon_thresholds_data() {
        return [
            { tag: "zero",     peers: 0,  icon: "image://images/node-0-connections" },
            { tag: "negative", peers: -1, icon: "image://images/node-0-connections" },
            { tag: "one",      peers: 1,  icon: "image://images/node-1-connection" },
            { tag: "three",    peers: 3,  icon: "image://images/node-1-connection" },
            { tag: "four",     peers: 4,  icon: "image://images/node-2-connections" },
            { tag: "six",      peers: 6,  icon: "image://images/node-2-connections" },
            { tag: "seven",    peers: 7,  icon: "image://images/node-3-connections" },
            { tag: "nine",     peers: 9,  icon: "image://images/node-3-connections" },
            { tag: "ten",      peers: 10, icon: "image://images/node-4-connections" },
            { tag: "many",     peers: 99, icon: "image://images/node-4-connections" },
        ]
    }

    function test_nodeConnectionIcon_thresholds(data) {
        compare(Utils.nodeConnectionIcon(data.peers), data.icon)
    }
}
