// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "ImportWalletOptions"
    when: windowShown
    width: 520
    height: 720

    Item {
        id: pageContainer
        width: 460
        height: 680
    }

    Component {
        id: importWalletOptionsComponent

        ImportWalletOptions {
            width: 460
            height: 680
        }
    }

    function init() {
        walletController.reset()
    }

    function test_hero_width_without_a_parent() {
        const page = createTemporaryObject(importWalletOptionsComponent, null)
        verify(page !== null)
        compare(page.parent, null)
        compare(page.heroWidth, 420)

        const errorView = findChild(page, "importWalletErrorView")
        verify(errorView !== null)
        compare(errorView.width, 420)
    }

    function test_hero_width_follows_the_page_width() {
        const page = createTemporaryObject(importWalletOptionsComponent, pageContainer)
        verify(page !== null)
        compare(page.heroWidth, 420)

        page.width = 300
        compare(page.heroWidth, 260)

        page.width = 900
        compare(page.heroWidth, 520)
    }

    function test_error_view_reports_the_wallet_load_error() {
        const page = createTemporaryObject(importWalletOptionsComponent, pageContainer)
        verify(page !== null)
        verify(!page.hasImportError)

        walletController.walletLoadError = "Corrupted wallet file."

        verify(page.hasImportError)
        const errorView = findChild(page, "importWalletErrorView")
        verify(errorView !== null)
        compare(errorView.width, page.heroWidth)
        const errorDescription = findChild(page, "importWalletErrorDescription")
        verify(errorDescription !== null)
        compare(errorDescription.text, "Corrupted wallet file.")
    }
}
