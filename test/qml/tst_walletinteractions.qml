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
    name: "WalletInteractions"
    when: windowShown
    width: 900
    height: 700
    Component {
        id: menuComponent
        ContextMenu {
            id: menu
            interactionContext: WalletContext {
                wallet: walletController.selectedWallet
            }
            property int calls: 0
            property alias button: action
            ContextMenuButton { id: action; text: "Action"; onTriggered: menu.calls++ }
        }
    }
    Component {
        id: alertComponent
        AlertPopup {
            id: alert
            interactionContext: WalletContext {
                wallet: walletController.selectedWallet
            }
            property int calls: 0
            AlertAction {
                text: "Confirm"
                buttonObjectName: "walletAlertConfirm"
                onTriggered: alert.calls++
            }
        }
    }
    Component {
        id: genericMenuComponent
        ContextMenu {
            id: menu
            property int calls: 0
            property alias button: action
            ContextMenuButton { id: action; text: "Action"; onTriggered: menu.calls++ }
        }
    }
    Component {
        id: genericAlertComponent
        AlertPopup {
            id: alert
            property int calls: 0
            AlertAction {
                text: "Confirm"
                buttonObjectName: "genericAlertConfirm"
                onTriggered: alert.calls++
            }
        }
    }
    Component {
        id: passwordComponent
        WalletPassphrasePopup {
            id: popup
            interactionContext: WalletContext {
                wallet: walletController.selectedWallet
                target: targetName
            }
            property string targetName: "original"
            property int calls: 0
            onSubmitted: calls++
        }
    }
    Component { id: coinComponent; SendCoinSelection { wallet: walletController.selectedWallet } }
    function init() { walletController.setSelectedWalletObject(testWalletModel) }
    function cleanup() { walletController.setSelectedWalletObject(testWalletModel) }
    function make(component) {
        const item = createTemporaryObject(component, testCase.Window.window.contentItem)
        verify(item !== null)
        return item
    }
    function test_menu_rejects_stale_click_and_accepts_fresh_click() {
        const menu = make(menuComponent)
        menu.open()
        // Switch before its opening animation completes.
        walletController.setSelectedWalletObject(null)
        walletController.setSelectedWalletObject(testWalletModel)
        menu.button.clicked()
        compare(menu.calls, 0)
        tryCompare(menu, "visible", false)
        menu.open()
        menu.button.clicked()
        compare(menu.calls, 1)
    }
    function test_menu_rejects_action_when_no_wallet_remains() {
        const menu = make(menuComponent)
        menu.open()
        walletController.setSelectedWalletObject(null)
        menu.button.clicked()
        compare(menu.calls, 0)
        tryCompare(menu, "visible", false)
    }
    function test_alert_rejects_stale_click_and_accepts_fresh_click() {
        const alert = make(alertComponent)
        alert.open()
        tryCompare(alert, "opened", true)
        const button = findChild(alert.contentItem, "walletAlertConfirm")
        verify(button !== null)
        walletController.setSelectedWalletObject(null)
        walletController.setSelectedWalletObject(testWalletModel)
        button.clicked()
        compare(alert.calls, 0)
        tryCompare(alert, "visible", false)
        alert.open()
        button.clicked()
        compare(alert.calls, 1)
    }
    function test_generic_menu_ignores_wallet_changes() {
        const menu = make(genericMenuComponent)
        menu.open()
        tryCompare(menu, "opened", true)
        walletController.setSelectedWalletObject(null)
        verify(menu.opened)
        menu.button.clicked()
        compare(menu.calls, 1)
    }
    function test_generic_alert_ignores_wallet_changes() {
        const alert = make(genericAlertComponent)
        alert.open()
        tryCompare(alert, "opened", true)
        const button = findChild(alert.contentItem, "genericAlertConfirm")
        verify(button !== null)
        walletController.setSelectedWalletObject(null)
        verify(alert.opened)
        button.clicked()
        compare(alert.calls, 1)
    }
    function test_replaced_transaction_cancels_password_submission() {
        const popup = make(passwordComponent)
        popup.open()
        verify(popup.contextIsValid())
        popup.targetName = "replacement"
        popup.submitPassphrase()
        compare(popup.calls, 0)
        tryCompare(popup, "visible", false)
        popup.open()
        popup.submitPassphrase()
        compare(popup.calls, 1)
    }
    function test_coin_picker_closes_child_menu_with_parent() {
        const picker = make(coinComponent)
        picker.open()
        tryCompare(picker, "opened", true)
        const menu = findChild(picker, "coinBrowserSortMenu")
        verify(menu !== null)
        menu.open()
        picker.close()
        verify(!menu.contextIsValid())
        tryCompare(menu, "visible", false)
    }
    function test_wallet_change_during_coin_picker_opening() {
        const picker = make(coinComponent)
        picker.open()
        walletController.setSelectedWalletObject(null)
        tryCompare(picker, "visible", false)
    }
}
