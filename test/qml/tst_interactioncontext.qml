// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
import QtQuick 2.15
import QtQuick.Window 2.15
import QtTest 1.2
import "../../qml/controls"
import "../../qml/components"

TestCase {
    id: testCase
    name: "InteractionContext"
    when: windowShown
    width: 900
    height: 700

    QtObject { id: interactionOwner; property bool visible: true }
    Component {
        id: contextComponent
        InteractionContext {
            property var snapshot: ({ source: "peer" })
            function capture() { return snapshot }
        }
    }
    Component {
        id: unavailableContextComponent
        InteractionContext { function accepts(context) { return false } }
    }
    Component {
        id: menuComponent
        ContextMenu {
            id: menu
            property string item: "original"
            property int calls: 0
            property alias button: action
            interactionContext: InteractionContext { target: menu.item }
            ContextMenuButton { id: action; text: "Action"; onTriggered: menu.calls++ }
        }
    }
    Component {
        id: alertComponent
        AlertPopup {
            id: alert
            property int calls: 0
            interactionContext: InteractionContext { owner: interactionOwner }
            AlertAction {
                text: "Confirm"
                buttonObjectName: "contextAlertConfirm"
                onTriggered: alert.calls++
            }
        }
    }
    function init() { interactionOwner.visible = true }
    function make(component) {
        const item = createTemporaryObject(component, testCase.Window.window.contentItem)
        verify(item !== null)
        return item
    }
    function test_notification_dismisses_menu_and_rejects_old_callback() {
        const menu = make(menuComponent)
        menu.open()
        const guard = menu.interactionContext
        const old = guard.current
        function delayedAction() {
            if (guard.isValid(old)) menu.calls++
        }
        guard.invalidate()
        menu.button.clicked()
        compare(menu.calls, 0)
        tryCompare(menu, "visible", false)
        menu.open()
        delayedAction()
        compare(menu.calls, 0)
        menu.button.clicked()
        compare(menu.calls, 1)
    }
    function test_unavailable_context_refuses_opening() {
        const menu = make(menuComponent)
        menu.interactionContext = make(unavailableContextComponent)
        menu.open()
        menu.button.clicked()
        compare(menu.calls, 0)
        tryCompare(menu, "visible", false)
        menu.interactionContext = make(contextComponent)
        menu.open()
        menu.button.clicked()
        compare(menu.calls, 1)
    }
    function test_context_is_consumed_once() {
        const guard = make(contextComponent)
        const opening = guard.begin()
        compare(guard.consume(), opening)
        verify(!guard.isValid(opening))
        compare(guard.consume(), null)
        const fresh = guard.begin()
        verify(!guard.isValid(opening))
        verify(guard.isValid(fresh))
    }
    function test_owner_hidden_dismisses_alert() {
        const alert = make(alertComponent)
        alert.open()
        tryCompare(alert, "opened", true)
        const button = findChild(alert.contentItem, "contextAlertConfirm")
        verify(button !== null)
        interactionOwner.visible = false
        button.clicked()
        compare(alert.calls, 0)
        tryCompare(alert, "visible", false)
        interactionOwner.visible = true
        alert.open()
        button.clicked()
        compare(alert.calls, 1)
    }
    function test_target_replacement_dismisses_menu() {
        const menu = make(menuComponent)
        menu.open()
        const old = menu.interactionContext.current
        menu.item = "replacement"
        menu.item = "original"
        menu.button.clicked()
        compare(menu.calls, 0)
        verify(!menu.interactionContext.isValid(old))
        tryCompare(menu, "visible", false)
    }
    function test_replacing_guard_invalidates_old_opening() {
        const menu = make(menuComponent)
        menu.open()
        const guard = menu.interactionContext
        const old = guard.current
        menu.interactionContext = make(contextComponent)
        verify(!guard.isValid(old))
        menu.button.clicked()
        compare(menu.calls, 0)
        tryCompare(menu, "visible", false)
        menu.open()
        menu.button.clicked()
        compare(menu.calls, 1)
    }
    function test_destroying_guard_does_not_revive_closing_actions() {
        const menu = make(menuComponent)
        const guard = make(contextComponent)
        menu.interactionContext = guard
        menu.open()
        guard.destroy()
        tryCompare(menu, "interactionContext", null)
        menu.button.clicked()
        compare(menu.calls, 0)
        tryCompare(menu, "visible", false)
        menu.open()
        menu.button.clicked()
        compare(menu.calls, 1)
    }
    function test_closing_rejects_clicks_before_exit_finishes() {
        const menu = make(menuComponent)
        menu.open()
        tryCompare(menu, "opened", true)
        menu.close()
        menu.button.clicked()
        compare(menu.calls, 0)
        tryCompare(menu, "visible", false)
    }
    function test_removing_guard_does_not_revive_closing_actions() {
        const menu = make(menuComponent)
        menu.open()
        const guard = menu.interactionContext
        const old = guard.current
        menu.interactionContext = null
        verify(!guard.isValid(old))
        menu.button.clicked()
        compare(menu.calls, 0)
        tryCompare(menu, "visible", false)
        menu.open()
        menu.button.clicked()
        compare(menu.calls, 1)
    }
}
