// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2

import "../../qml/controls/utils.js" as Utils

TestCase {
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

    function test_remainingSyncTime_data() {
        return [
            { tag: "estimating", milliseconds: 0, text: "Estimating", estimating: true },
            { tag: "second", milliseconds: 1000, text: "~1 second left", estimating: false },
            { tag: "seconds", milliseconds: 59000, text: "~59 seconds left", estimating: false },
            { tag: "minute-seconds", milliseconds: 61000, text: "~1m 1s left", estimating: false },
            { tag: "minutes", milliseconds: 300000, text: "~5 minutes left", estimating: false },
            { tag: "hour-minutes", milliseconds: 3660000, text: "~1h 1m left", estimating: false },
            { tag: "hours", milliseconds: 18000000, text: "~5 hours left", estimating: false },
            { tag: "day", milliseconds: 86400000, text: "~1 day left", estimating: false },
            { tag: "days", milliseconds: 172800000, text: "~2 days left", estimating: false },
            { tag: "week", milliseconds: 604800000, text: "~1 week left", estimating: false },
            { tag: "weeks", milliseconds: 1209600000, text: "~2 weeks left", estimating: false }
        ]
    }

    function test_remainingSyncTime(data) {
        const result = Utils.formatRemainingSyncTime(data.milliseconds)
        compare(result.text, data.text)
        compare(result.estimating, data.estimating)
    }
}
