// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// utils.js

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
