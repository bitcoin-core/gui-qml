// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtQuick.Controls 2.15
import QtTest 1.2
import org.bitcoincore.qt 1.0
import "../../qml/controls"
import "../../qml/pages/settings"

TestCase {
    name: "DisplaySettings"
    when: windowShown
    width: 600
    height: 800

    function cleanup() {
        AppMode.adaptiveSidebarLayout = false
        AppMode.adaptiveSidebarLayoutAvailable = true
    }

    Component {
        id: layoutPicker
        SettingsLayout {}
    }

    Component {
        id: displaySettingsPage
        SettingsDisplay { showBackButton: false }
    }

    function test_layoutPicker_is_available_from_display_developer_section() {
        const page = createTemporaryObject(displaySettingsPage, this)
        verify(page !== null)

        const layoutSetting = findChild(page, "gotoApplicationLayout")
        verify(layoutSetting !== null)
        layoutSetting.clicked()
        tryVerify(function() { return findChild(page, "settingsLayoutPage") !== null })
    }

    function test_layoutPicker_is_hidden_when_adaptive_shell_is_not_built() {
        AppMode.adaptiveSidebarLayoutAvailable = false
        const page = createTemporaryObject(displaySettingsPage, this)
        verify(page !== null)

        compare(findChild(page, "displayLayoutDeveloperSectionLabel").visible, false)
        compare(findChild(page, "displayLayoutDeveloperSectionSeparator").visible, false)
        compare(findChild(page, "gotoApplicationLayout").visible, false)
    }

    function test_layoutPicker_selects_default_layout() {
        AppMode.adaptiveSidebarLayout = false
        const page = createTemporaryObject(layoutPicker, this)
        verify(page !== null)

        const defaultButton = findChild(page, "layoutDefault")
        const adaptiveButton = findChild(page, "layoutAdaptiveSidebar")
        verify(defaultButton !== null)
        verify(adaptiveButton !== null)
        compare(defaultButton.checked, true)
        compare(adaptiveButton.checked, false)
    }

    function test_layoutPicker_selects_adaptive_sidebar_layout() {
        AppMode.adaptiveSidebarLayout = false
        const page = createTemporaryObject(layoutPicker, this)
        verify(page !== null)

        const adaptiveButton = findChild(page, "layoutAdaptiveSidebar")
        adaptiveButton.clicked()
        compare(AppMode.adaptiveSidebarLayout, true)
        compare(adaptiveButton.checked, true)
        compare(findChild(page, "layoutDefault").checked, false)
    }

    // Minimal component exercising display-unit OptionButton binding logic.
    // Uses OptionButton directly (no NavButton / org.bitcoincore.qt dependency)
    // to test the optionsModel.displayUnit binding in isolation.
    // ButtonGroup is intentionally omitted: the declarative 'checked:' bindings
    // already model mutual exclusion through optionsModel, and ButtonGroup's
    // managed-checked behavior conflicts with declarative bindings in tests.
    Component {
        id: displayUnitButtons
        Column {
            OptionButton {
                objectName: "displayUnitBTC"
                text: "BTC"
                checked: optionsModel.displayUnit === 0
                onClicked: optionsModel.displayUnit = 0
            }
            OptionButton {
                objectName: "displayUnitMBTC"
                text: "mBTC"
                checked: optionsModel.displayUnit === 1
                onClicked: optionsModel.displayUnit = 1
            }
            OptionButton {
                objectName: "displayUnitUBTC"
                text: "bits"
                checked: optionsModel.displayUnit === 2
                onClicked: optionsModel.displayUnit = 2
            }
            OptionButton {
                objectName: "displayUnitSAT"
                text: "sat"
                checked: optionsModel.displayUnit === 3
                onClicked: optionsModel.displayUnit = 3
            }
        }
    }

    function test_displayUnit_BTC_button_checked_by_default() {
        optionsModel.displayUnit = 0
        const obj = createTemporaryObject(displayUnitButtons, this)
        verify(obj !== null)

        const btcBtn = findChild(obj, "displayUnitBTC")
        verify(btcBtn !== null)
        compare(btcBtn.checked, true)

        const satBtn = findChild(obj, "displayUnitSAT")
        verify(satBtn !== null)
        compare(satBtn.checked, false)
    }

    function test_displayUnit_SAT_button_updates_on_model_change() {
        optionsModel.displayUnit = 3
        const obj = createTemporaryObject(displayUnitButtons, this)
        verify(obj !== null)

        const satBtn = findChild(obj, "displayUnitSAT")
        verify(satBtn !== null)
        compare(satBtn.checked, true)

        const btcBtn = findChild(obj, "displayUnitBTC")
        verify(btcBtn !== null)
        compare(btcBtn.checked, false)

        // Reset
        optionsModel.displayUnit = 0
    }

    function test_displayUnit_clicking_SAT_updates_model() {
        optionsModel.displayUnit = 0
        const obj = createTemporaryObject(displayUnitButtons, this)
        verify(obj !== null)

        const satBtn = findChild(obj, "displayUnitSAT")
        verify(satBtn !== null)

        // Invoke the onClicked handler directly to simulate a user press.
        satBtn.clicked()
        compare(optionsModel.displayUnit, 3)

        // Reset
        optionsModel.displayUnit = 0
    }

    function test_displayUnit_clicking_BTC_after_SAT_resets_model() {
        optionsModel.displayUnit = 3
        const obj = createTemporaryObject(displayUnitButtons, this)
        verify(obj !== null)

        const btcBtn = findChild(obj, "displayUnitBTC")
        verify(btcBtn !== null)
        compare(btcBtn.checked, false)

        btcBtn.clicked()
        compare(optionsModel.displayUnit, 0)
    }

    function test_displayUnit_clicking_mBTC_updates_model() {
        optionsModel.displayUnit = 0
        const obj = createTemporaryObject(displayUnitButtons, this)
        verify(obj !== null)

        const mbtcBtn = findChild(obj, "displayUnitMBTC")
        verify(mbtcBtn !== null)

        mbtcBtn.clicked()
        compare(optionsModel.displayUnit, 1)

        optionsModel.displayUnit = 0
    }

    function test_displayUnit_clicking_bits_updates_model() {
        optionsModel.displayUnit = 0
        const obj = createTemporaryObject(displayUnitButtons, this)
        verify(obj !== null)

        const ubtcBtn = findChild(obj, "displayUnitUBTC")
        verify(ubtcBtn !== null)

        ubtcBtn.clicked()
        compare(optionsModel.displayUnit, 2)

        optionsModel.displayUnit = 0
    }

    // Mirrors the balance suffix expression in WalletBadge.qml.
    // balanceSatoshi=1000 → plural "sats"; balanceSatoshi=1 → singular "sat".
    Component {
        id: balanceSuffixComponent
        Text {
            property string balance: "1 000"
            property var balanceSatoshi: 1000
            text: balance + " " + optionsModel.displayUnitLabelForAmount(balanceSatoshi)
        }
    }

    function test_walletBadge_suffix_is_sats_in_sat_mode() {
        optionsModel.displayUnit = 3
        const obj = createTemporaryObject(balanceSuffixComponent, this)
        verify(obj !== null)
        compare(obj.text, "1 000 sats")
        optionsModel.displayUnit = 0
    }

    function test_walletBadge_suffix_is_sat_singular_in_sat_mode() {
        optionsModel.displayUnit = 3
        const obj = createTemporaryObject(balanceSuffixComponent, this)
        verify(obj !== null)
        obj.balanceSatoshi = 1
        compare(obj.text, "1 000 sat")
        optionsModel.displayUnit = 0
    }

    function test_walletBadge_suffix_is_btc_symbol_in_btc_mode() {
        optionsModel.displayUnit = 0
        const obj = createTemporaryObject(balanceSuffixComponent, this)
        verify(obj !== null)
        compare(obj.text, "1 000 ₿")
    }

    // Tests displayUnitLabelForAmount pluralization logic.
    function test_displayUnitLabelForAmount_singular_in_sat_mode() {
        optionsModel.displayUnit = 3
        compare(optionsModel.displayUnitLabelForAmount(1), "sat")
        compare(optionsModel.displayUnitLabelForAmount(-1), "sat")
        optionsModel.displayUnit = 0
    }

    function test_displayUnitLabelForAmount_plural_in_sat_mode() {
        optionsModel.displayUnit = 3
        compare(optionsModel.displayUnitLabelForAmount(0), "sats")
        compare(optionsModel.displayUnitLabelForAmount(2), "sats")
        compare(optionsModel.displayUnitLabelForAmount(1000), "sats")
        optionsModel.displayUnit = 0
    }

    function test_displayUnitLabelForAmount_btc_symbol_in_btc_mode() {
        optionsModel.displayUnit = 0
        compare(optionsModel.displayUnitLabelForAmount(1), "₿")
        compare(optionsModel.displayUnitLabelForAmount(1000), "₿")
    }

    function test_displayUnitLabelForAmount_mbtc_and_bits() {
        optionsModel.displayUnit = 1
        compare(optionsModel.displayUnitLabelForAmount(1000), "mBTC")
        optionsModel.displayUnit = 2
        compare(optionsModel.displayUnitLabelForAmount(1000), "bits")
        optionsModel.displayUnit = 0
    }
}
