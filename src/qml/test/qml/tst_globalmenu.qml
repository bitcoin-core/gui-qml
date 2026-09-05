// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/components"

TestCase {
    id: testCase
    name: "GlobalMenu"
    when: windowShown
    visible: true
    width: 500
    height: 500

    QtObject {
        id: testRouter
        property bool shuttingDown: false
        property bool canGoBack: false
        property string destination: ""
        function navigate(route) { destination = route }
        function back() { destination = "back" }
    }
    QtObject {
        id: testLifecycle
        property bool requested: false
        function requestShutdown() { requested = true }
    }
    ListModel {
        id: destinations
        ListElement { routeId: "node"; label: "Node"; selected: true; available: true }
        ListElement { routeId: "console"; label: "Console"; selected: false; available: true }
    }
    Component {
        id: component
        GlobalMenu {
            router: testRouter
            navigation: destinations
            lifecycle: testLifecycle
        }
    }

    function init() {
        testRouter.shuttingDown = false
        testRouter.destination = ""
        testLifecycle.requested = false
    }
    function test_menuProjectsSelectionAndRoutesActions() {
        const control = createTemporaryObject(component, testCase, {router: testRouter, lifecycle: testLifecycle})
        verify(control !== null)
        waitForRendering(control)
        mouseClick(control)
        const consoleAction = findChild(control, "navigate_console")
        verify(consoleAction !== null)
        compare(consoleAction.checked, false)
        compare(findChild(control, "navigate_node").checked, true)
        verify(findChild(control, "navigate_wallet") === null)
        mouseClick(consoleAction)
        compare(testRouter.destination, "console")
    }
    function test_quitUsesLifecycleAction() {
        const control = createTemporaryObject(component, testCase, {router: testRouter, lifecycle: testLifecycle})
        waitForRendering(control)
        mouseClick(control)
        mouseClick(findChild(control, "globalMenuQuit"))
        verify(testLifecycle.requested)
        testRouter.shuttingDown = true
        compare(control.enabled, false)
    }
}
