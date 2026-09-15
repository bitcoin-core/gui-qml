// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "WalletSelect"
    when: windowShown
    width: 360
    height: 480

    Component {
        id: walletSelectComponent

        WalletSelect {}
    }

    function init() {
        walletController.reset()
        walletListModel.reset()
    }

    function openWalletSelect() {
        const popup = createTemporaryObject(walletSelectComponent, this)
        verify(popup !== null)
        popup.open()
        tryCompare(popup, "opened", true)
        const list = findChild(popup, "walletSelectList")
        verify(list !== null)
        tryVerify(function() {
            return list.itemAtIndex(0) !== null && list.itemAtIndex(1) !== null
        })
        return popup
    }

    Component {
        id: typedWalletModel
        ListModel {}
    }

    function test_closed_wallet_uses_generic_icon_data() {
        return [
            { tag: "single-key", kind: 0, icon: "key" },
            { tag: "multi-key", kind: 2, icon: "two-keys" },
            { tag: "watch-only", kind: 1, icon: "visible" },
            { tag: "unknown-fallback", kind: -1, icon: "key" }
        ]
    }

    function test_closed_wallet_uses_generic_icon(data) {
        const model = createTemporaryObject(typedWalletModel, this)
        model.append({ name: "typedwallet", displayName: "Typed wallet",
            format: "sqlite", loadState: 1, errorMessage: "",
            balance: "1.23", keySchemeKind: data.kind, walletSection: "open" })
        const popup = createTemporaryObject(walletSelectComponent, this, { model: model })
        popup.open()
        tryCompare(popup, "opened", true)
        const list = findChild(popup, "walletSelectList")
        tryVerify(function() { return list.itemAtIndex(0) !== null })
        compare(list.itemAtIndex(0).iconSource, "image://images/" + data.icon)
        model.setProperty(0, "loadState", 0)
        model.setProperty(0, "walletSection", "closed")
        compare(list.itemAtIndex(0).iconSource, "image://images/wallet")
        popup.close()
        popup.open()
        tryCompare(popup, "opened", true)
        compare(list.itemAtIndex(0).iconSource, "image://images/wallet")
    }

    function test_sections_only_appear_when_populated_data() {
        return [
            { tag: "both", groups: ["open", "closed"] },
            { tag: "only-open", groups: ["open"] },
            { tag: "only-closed", groups: ["closed"] },
            { tag: "empty", groups: [] }
        ]
    }

    function test_sections_only_appear_when_populated(data) {
        const model = createTemporaryObject(typedWalletModel, this)
        for (let i = 0; i < data.groups.length; ++i) {
            model.append({ name: "wallet" + i, displayName: "Wallet " + i,
                format: "sqlite", loadState: data.groups[i] === "open" ? 1 : 0,
                errorMessage: "", balance: "1.23", keySchemeKind: 0,
                walletSection: data.groups[i] })
        }
        const popup = createTemporaryObject(walletSelectComponent, this, { model: model })
        popup.open()
        tryCompare(popup, "opened", true)
        const list = findChild(popup, "walletSelectList")
        for (const group of ["open", "closed"]) {
            const heading = findChild(list, "walletSelectSection_" + group)
            compare(heading !== null && heading.visible, data.groups.indexOf(group) !== -1)
            if (heading !== null) compare(heading.text, group === "open" ? "Open wallets" : "Closed wallets")
        }
    }

    function test_closed_wallet_menu_opens_wallet() {
        const popup = openWalletSelect()
        const row = findChild(popup, "walletSelectList").itemAtIndex(1)
        compare(row.statusText, "")
        findChild(row, "walletSelectActions_secondarywallet").clicked()
        const open = findChild(row, "walletSelectOpen_secondarywallet")
        tryCompare(open, "visible", true)
        compare(open.text, "Open wallet")
        compare(findChild(row, "walletSelectSettings_secondarywallet").visible, false)
        compare(findChild(row, "walletSelectClose_secondarywallet").visible, false)
        open.clicked()
        compare(walletController.lastSelectedWalletName, "secondarywallet")
    }

    function test_ellipsis_toggles_context_menu_data() {
        return [
            { tag: "open-wallet", rowIndex: 0, name: "testwallet" },
            { tag: "closed-wallet", rowIndex: 1, name: "secondarywallet" }
        ]
    }

    function test_ellipsis_toggles_context_menu(data) {
        const popup = openWalletSelect()
        const list = findChild(popup, "walletSelectList")
        const row = list.itemAtIndex(data.rowIndex)
        compare(list.interactive, true)
        const button = findChild(row, "walletSelectActions_" + data.name)
        const menu = findChild(row, "walletSelectActionsMenu_" + data.name)
        mouseClick(button, button.width / 2, button.height / 2)
        tryCompare(menu, "opened", true)
        compare(list.interactive, false)
        mouseClick(button, button.width / 2, button.height / 2)
        tryCompare(menu, "visible", false)
        compare(list.interactive, true)
        compare(popup.opened, true)
        mouseClick(button, button.width / 2, button.height / 2)
        tryCompare(menu, "opened", true)
        compare(list.interactive, false)
        mouseClick(popup.contentItem, 10, 10)
        tryCompare(menu, "visible", false)
        compare(list.interactive, true)
        compare(popup.opened, true)
    }

    function test_actions_button_tracks_load_state() {
        const popup = openWalletSelect()
        const list = findChild(popup, "walletSelectList")
        verify(list !== null)

        const openWalletClose = findChild(list.itemAtIndex(0), "walletSelectActions_testwallet")
        const closedWalletClose = findChild(list.itemAtIndex(1), "walletSelectActions_secondarywallet")
        verify(openWalletClose !== null)
        verify(closedWalletClose !== null)

        compare(openWalletClose.visible, true)
        compare(openWalletClose.enabled, true)
        compare(closedWalletClose.visible, true)
        compare(closedWalletClose.enabled, true)

        walletListModel.setWalletLoadState("testwallet", 0)
        tryCompare(openWalletClose, "visible", true)
        compare(openWalletClose.enabled, true)

        walletListModel.setWalletLoadState("secondarywallet", 1)
        tryCompare(closedWalletClose, "visible", true)
        compare(closedWalletClose.enabled, true)
    }

    function test_close_button_emits_close_wallet_requested() {
        const popup = openWalletSelect()
        const list = findChild(popup, "walletSelectList")
        verify(list !== null)
        const openWalletClose = findChild(list.itemAtIndex(0), "walletSelectActions_testwallet")
        verify(openWalletClose !== null)
        verify(openWalletClose.visible)

        const spy = signalSpyComponent.createObject(this, {
            target: popup,
            signalName: "closeWalletRequested",
        })

        openWalletClose.clicked()
        const unload = findChild(list.itemAtIndex(0), "walletSelectClose_testwallet")
        verify(unload !== null)
        tryCompare(unload, "visible", true)
        compare(spy.count, 0)
        unload.clicked()

        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], "testwallet")
        // The selector closes itself before the confirmation popup appears.
        tryCompare(popup, "opened", false)
        // No direct controller call — that happens after the confirmation popup.
        compare(walletController.closeWalletCalls, 0)
    }

    function test_loading_and_error_do_not_show_loaded_actions() {
        const popup = openWalletSelect()
        const list = findChild(popup, "walletSelectList")
        const row = list.itemAtIndex(1)
        const actions = findChild(row, "walletSelectActions_secondarywallet")
        const load = findChild(row, "walletSelectOpen_secondarywallet")
        walletListModel.setWalletLoadState("secondarywallet", 2)
        compare(actions.visible, false)
        compare(load.visible, false)
        compare(row.statusText, "Loading…")
        walletListModel.setWalletLoadState("secondarywallet", 3)
        compare(actions.visible, true)
        actions.clicked()
        tryCompare(load, "visible", true)
        compare(load.text, "Open wallet")
        compare(row.statusText, "Failed to open wallet")
    }

    function test_settings_action_targets_its_wallet() {
        const popup = openWalletSelect()
        const row = findChild(popup, "walletSelectList").itemAtIndex(0)
        const spy = signalSpyComponent.createObject(this, {
            target: popup, signalName: "walletSettingsRequested"
        })
        findChild(row, "walletSelectActions_testwallet").clicked()
        const settings = findChild(row, "walletSelectSettings_testwallet")
        tryCompare(settings, "visible", true)
        settings.clicked()
        compare(spy.count, 1)
        compare(spy.signalArguments[0][0], "testwallet")
        compare(spy.signalArguments[0][1], "sqlite")
        tryCompare(popup, "opened", false)
    }

    Component {
        id: signalSpyComponent
        SignalSpy {}
    }
}
