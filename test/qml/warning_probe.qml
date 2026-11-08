// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2

TestCase {
    id: testCase
    name: "WarningProbe"
    when: windowShown

    property int startupValue: Qt.application.arguments.indexOf("WarningProbe::test_startup") >= 0
        ? missingStartupWarningProbe.value : 0
    property QtObject teardownObject: null

    Component.onCompleted: {
        if (Qt.application.arguments.indexOf("WarningProbe::test_console_startup") >= 0)
            console.warn("startup-console-warning-probe")
        if (Qt.application.arguments.indexOf("WarningProbe::test_log_startup") >= 0)
            console.log("startup-console-log-probe")
    }

    Component.onDestruction: {
        if (Qt.application.arguments.indexOf("WarningProbe::test_console_teardown") >= 0)
            console.warn("teardown-console-warning-probe")
    }

    function init() {
        testSetup.failOnWarnings()
    }

    function test_clean() {
        verify(true)
    }

    function test_console() {
        console.warn("console-warning-probe")
        verify(true)
    }

    function test_binding() {
        const item = createTemporaryQmlObject(
            'import QtQml 2.15; QtObject { property int value: missingBindingWarningProbe.value }', this)
        verify(item !== null)
    }

    function test_startup() {
        verify(true)
    }

    function test_console_startup() { verify(true) }
    function test_console_teardown() { verify(true) }
    function test_log_startup() { verify(true) }

    function test_teardown() {
        teardownObject = Qt.createQmlObject(
            'import QtQml 2.15; QtObject { Component.onDestruction: missingTeardownWarningProbe.value }', testCase)
        verify(teardownObject !== null)
    }
}
