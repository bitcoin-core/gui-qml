// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// utils.js

// Map a live peer/connection count to one of the five node-connection icon
// tiers (image provider aliases node-0-connections .. node-4-connections),
// each showing one more filled node than the last. Thresholds mirror the Qt
// Widgets GUI's connection-strength icon (connect_0 .. connect_4).
function nodeConnectionIcon(numPeers) {
    if (numPeers <= 0) return "image://images/node-0-connections"
    if (numPeers <= 3) return "image://images/node-1-connection"
    if (numPeers <= 6) return "image://images/node-2-connections"
    if (numPeers <= 9) return "image://images/node-3-connections"
    return "image://images/node-4-connections"
}

function formatRelativeTime(isoString) {
    if (!isoString) return ""
    var then = new Date(isoString)
    var now = new Date()
    var diffSec = Math.floor((now - then) / 1000)
    if (diffSec < 60) return qsTr("just now")
    var m = Math.floor(diffSec / 60)
    if (diffSec < 3600) return m === 1 ? qsTr("1 minute ago") : qsTr("%1 minutes ago").arg(m)
    var h = Math.floor(diffSec / 3600)
    if (diffSec < 86400) return h === 1 ? qsTr("1 hour ago") : qsTr("%1 hours ago").arg(h)
    var d = Math.floor(diffSec / 86400)
    return d === 1 ? qsTr("1 day ago") : qsTr("%1 days ago").arg(d)
}

function formatRemainingSyncTime(milliseconds) {
    var minutes = Math.floor(milliseconds / 60000);
    var seconds = Math.floor((milliseconds % 60000) / 1000);
    var weeks = Math.floor(minutes / 10080);
    minutes %= 10080;
    var days = Math.floor(minutes / 1440);
    minutes %= 1440;
    var hours = Math.floor(minutes / 60);
    minutes %= 60;
    var result = "";
    var estimatingStatus = false;

    if (weeks > 0) {
        return {
            text: "~" + weeks + (weeks === 1 ? " week" : " weeks") + " left",
            estimating: false
        };
    }
    if (days > 0) {
        return {
            text: "~" + days + (days === 1 ? " day" : " days") + " left",
            estimating: false
        };
    }
    if (hours >= 5) {
        return {
            text: "~" + hours + (hours === 1 ? " hour" : " hours") + " left",
            estimating: false
        };
    }
    if (hours > 0) {
        return {
            text: "~" + hours + "h " + minutes + "m" + " left",
            estimating: false
        };
    }
    if (minutes >= 5) {
        return {
            text: "~" + minutes + (minutes === 1 ? " minute" : " minutes") + " left",
            estimating: false
        };
    }
    if (minutes > 0) {
        return {
            text: "~" + minutes + "m " + seconds + "s" + " left",
            estimating: false
        };
    }
    if (seconds > 0) {
        return {
            text: "~" + seconds + (seconds === 1 ? " second" : " seconds") + " left",
            estimating: false
        };
    } else {
        return {
            text: "Estimating",
            estimating: true
        };
    }
}
