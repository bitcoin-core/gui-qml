// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtTest 1.2
import "../../qml/components"
import "../../qml/controls"

TestCase {
    id: testCase
    name: "DialogPopup"
    when: windowShown
    width: 640
    height: 480

    Button {
        id: trigger
        objectName: "dialogTrigger"
        text: "Open"
        focusPolicy: Qt.StrongFocus
    }

    Component {
        id: dialogComponent
        DialogPopup {
            title: "Feature"
            implicitWidth: 400
            contentItem: Column {
                Button { objectName: "dialogFirstButton"; text: "First" }
            }
        }
    }

    Component {
        id: alertComponent
        AlertPopup {
            title: "Remove item?"
            message: "The item is removed."
            AlertAction { text: "Cancel"; role: AlertAction.Cancel; onTriggered: testCase.cancelCount += 1 }
            AlertAction { text: "Remove"; role: AlertAction.Destructive; buttonObjectName: "alertRemoveButton" }
        }
    }

    Component {
        id: extendedAlertComponent
        AlertPopup {
            title: "Open payment request"
            message: "Paste a request."
            errorText: "Not a payment request."
            initialFocusItem: field
            content: TextField { id: field; objectName: "alertContentField"; Layout.fillWidth: true }
            AlertAction { id: cancelAction; text: "Cancel"; role: AlertAction.Cancel; buttonObjectName: "alertCancelButton"; onTriggered: testCase.cancelCount += 1 }
            AlertAction { text: "Apply"; buttonObjectName: "alertApplyButton"; enabled: field.text.length > 0; closesPopup: false; onTriggered: testCase.applyCount += 1 }
        }
    }

    Component {
        id: dataAlertComponent
        AlertPopup {
            property AlertAction yes: AlertAction { text: "Yes"; buttonObjectName: "alertYesButton" }
            property AlertAction no: AlertAction { text: "No"; role: AlertAction.Cancel; buttonObjectName: "alertNoButton" }
            title: "Question"
            tone: AlertPopup.Success
            actionList: [no, yes]
        }
    }

    property int cancelCount: 0
    property int applyCount: 0

    function init() {
        cancelCount = 0
        applyCount = 0
        trigger.forceActiveFocus()
        compare(trigger.activeFocus, true)
    }

    function openDialog(component, properties) {
        const popup = createTemporaryObject(component, testCase, properties || {})
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)
        return popup
    }

    Component {
        id: lazyDialogComponent
        DialogPopup {
            id: lazyPopup
            implicitWidth: 400
            readonly property var loadedContent: contentItem && contentItem.item ? contentItem.item : null
            contentItem: Loader {
                active: lazyPopup.visible
                sourceComponent: Button { text: "Loaded content" }
            }
        }
    }

    function test_lazy_content_follows_popup_visibility() {
        const popup = createTemporaryObject(lazyDialogComponent, testCase)
        compare(popup.loadedContent, null)
        popup.open()
        tryCompare(popup, "opened", true)
        verify(popup.loadedContent !== null)
        compare(popup.loadedContent, popup.contentItem.item)
        popup.close()
        tryCompare(popup, "loadedContent", null)
    }

    function test_registers_as_dialog_named_by_title() {
        const popup = openDialog(dialogComponent)
        compare(popup.popupItem.Accessible.role, Accessible.Dialog)
        compare(popup.popupItem.Accessible.name, "Feature")
        popup.title = "Renamed"
        compare(popup.popupItem.Accessible.name, "Renamed")
    }

    function test_uses_shared_surface_and_overlay() {
        const popup = openDialog(dialogComponent)
        compare(popup.background.color, Theme.color.surface1)
        compare(popup.background.radius, Theme.radius.popup)
        popup.sheet = true
        compare(popup.background.radius, Theme.radius.sheet)
        compare(popup.dim, true)
        compare(popup.modal, true)
        // Tests run with motion off, so there is nothing to animate.
        compare(popup.enter, null)
        compare(popup.exit, null)
        compare(popup.opacity, 1)
    }

    function test_height_is_capped_by_the_container() {
        const popup = openDialog(dialogComponent, { implicitHeight: 2000 })
        compare(popup.height, testCase.height - 2 * popup.edgeMargin)
        verify(popup.y >= 0)
    }

    function test_focus_moves_in_and_returns_on_escape() {
        trigger.focus = false
        trigger.forceActiveFocus(Qt.TabFocusReason)
        compare(trigger.visualFocus, true)
        const popup = openDialog(dialogComponent)
        tryCompare(popup.contentItem, "activeFocus", true)
        compare(trigger.activeFocus, false)

        keyClick(Qt.Key_Escape)
        tryCompare(popup, "opened", false)
        tryCompare(trigger, "activeFocus", true)
        compare(trigger.visualFocus, true)
    }

    function test_focus_falls_back_when_opener_disappears() {
        const popup = openDialog(dialogComponent)
        trigger.visible = false
        const fallback = Qt.createQmlObject('import QtQuick.Controls 2.15; Button { text: "Next" }', testCase)
        try {
            popup.close()
            tryCompare(fallback, "activeFocus", true)
            compare(fallback.visualFocus, true)
        } finally {
            trigger.visible = true
            fallback.destroy()
        }
    }

    function test_initial_focus_item() {
        const popup = createTemporaryObject(dialogComponent, testCase)
        popup.initialFocusItem = findChild(popup.contentItem, "dialogFirstButton")
        popup.open()
        tryCompare(popup, "opened", true)
        tryCompare(popup.initialFocusItem, "activeFocus", true)
        popup.close()
        tryCompare(trigger, "activeFocus", true)
    }

    function test_escape_and_outside_press_follow_dismissal_api_data() {
        return [
            { tag: "feature modal", props: {}, escape: true, outside: true },
            { tag: "input dialog", props: { closesOnPressOutside: false }, escape: true, outside: false },
            { tag: "not dismissible", props: { dismissible: false }, escape: false, outside: false },
            { tag: "busy", props: { busy: true }, escape: false, outside: false }
        ]
    }

    function test_escape_and_outside_press_follow_dismissal_api(data) {
        let popup = openDialog(dialogComponent, data.props)
        // An instant open leaves the first press to the next frame.
        waitForRendering(testCase, 1000)
        mouseClick(testCase, 5, 5)
        compare(popup.opened, !data.outside)

        popup.close()
        tryCompare(popup, "opened", false)
        popup.open()
        tryCompare(popup, "opened", true)
        tryCompare(popup.contentItem, "activeFocus", true)
        keyClick(Qt.Key_Escape)
        compare(popup.opened, !data.escape)
    }

    function test_alert_escape_answers_with_cancel_and_returns_focus() {
        const popup = openDialog(alertComponent)
        tryCompare(popup.contentItem, "activeFocus", true)
        keyClick(Qt.Key_Escape)
        tryCompare(popup, "opened", false)
        compare(cancelCount, 1)
        tryCompare(trigger, "activeFocus", true)
    }

    function test_alert_ignores_outside_press() {
        const popup = openDialog(alertComponent)
        waitForRendering(testCase, 1000)
        mouseClick(testCase, 5, 5)
        compare(popup.opened, true)
        compare(cancelCount, 0)
    }

    function test_focus_stays_in_a_popup_opened_while_closing() {
        const first = openDialog(dialogComponent)
        const second = createTemporaryObject(dialogComponent, testCase)
        first.close()
        second.open()
        tryCompare(second, "opened", true)
        tryCompare(second.contentItem, "activeFocus", true)
        second.close()
        tryCompare(trigger, "activeFocus", true)
    }

    function test_alert_long_message_scrolls_inside_the_height_cap() {
        const popup = openDialog(alertComponent, { message: Array(200).join("A long message. ") })
        compare(popup.height, popup.maximumHeight)
        const area = findChild(popup.contentItem, "alertMessageArea")
        verify(area.height < area.contentHeight)
        verify(area.interactive)
        const remove = findChild(popup.contentItem, "alertRemoveButton")
        verify(remove.mapToItem(testCase, 0, remove.height).y <= testCase.height)
    }

    function test_alert_content_slot_error_and_disabled_action() {
        const popup = openDialog(extendedAlertComponent)
        const field = findChild(popup.contentItem, "alertContentField")
        tryCompare(field, "activeFocus", true)
        compare(findChild(popup.contentItem, "alertErrorText").visible, true)

        const apply = findChild(popup.contentItem, "alertApplyButton")
        compare(apply.enabled, false)
        field.text = "bitcoin:"
        compare(apply.enabled, true)
        apply.clicked()
        compare(popup.opened, true)

        popup.busy = true
        keyClick(Qt.Key_Escape)
        compare(popup.opened, true)
        popup.busy = false
        keyClick(Qt.Key_Escape)
        tryCompare(popup, "opened", false)
        compare(cancelCount, 1)
    }

    function test_alert_return_answers_with_the_only_normal_action() {
        const popup = openDialog(extendedAlertComponent)
        const field = findChild(popup.contentItem, "alertContentField")
        tryCompare(field, "activeFocus", true)
        keyClick(Qt.Key_Return)
        compare(applyCount, 0)
        field.text = "bitcoin:"
        field.forceActiveFocus()
        keyClick(Qt.Key_Return)
        compare(applyCount, 1)
        field.forceActiveFocus()
        keyClick(Qt.Key_Enter)
        compare(applyCount, 2)
        popup.busy = true
        field.forceActiveFocus()
        keyClick(Qt.Key_Return)
        compare(applyCount, 2)
        popup.busy = false
    }

    function test_alert_return_on_a_focused_button_activates_that_button() {
        const popup = openDialog(extendedAlertComponent)
        const field = findChild(popup.contentItem, "alertContentField")
        tryCompare(field, "activeFocus", true)
        field.text = "bitcoin:"
        findChild(popup.contentItem, "alertCancelButton").forceActiveFocus(Qt.TabFocusReason)
        keyClick(Qt.Key_Return)
        tryCompare(popup, "opened", false)
        compare(cancelCount, 1)
        compare(applyCount, 0)
    }

    function test_alert_return_never_confirms_a_destructive_action() {
        const popup = openDialog(alertComponent)
        tryCompare(popup.contentItem, "activeFocus", true)
        keyClick(Qt.Key_Return)
        compare(popup.opened, true)
        compare(cancelCount, 0)
    }

    function test_alert_actions_from_data_and_success_icon() {
        const popup = openDialog(dataAlertComponent)
        compare(popup.visibleActions.length, 2)
        verify(findChild(popup.contentItem, "alertYesButton") !== null)
        compare(findChild(popup.contentItem, "alertIconBadge").visible, true)
        compare(findChild(popup.contentItem, "alertIconBadge").color, Theme.color.success)
        compare(popup.cancelAction(), popup.no)
        popup.actionList = [popup.yes]
        compare(popup.visibleActions.length, 1)
        compare(findChild(popup.contentItem, "alertNoButton"), null)
    }
}
