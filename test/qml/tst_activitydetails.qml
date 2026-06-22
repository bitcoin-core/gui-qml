// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

import QtQuick 2.15
import QtTest 1.2
import "../../qml/pages/wallet"

TestCase {
    name: "ActivityDetails"
    when: windowShown
    width: 900
    height: 700

    Component {
        id: detailsComponent
        ActivityDetails {
            width: 600
            height: 700
        }
    }

    function init() {
        optionsModel.thirdPartyTransactionUrls = ""
    }

    function cleanup() {
        optionsModel.thirdPartyTransactionUrls = ""
    }

    function test_speedUpBanner_exists_when_bumpable() {
        const page = createTemporaryObject(detailsComponent, this, {
            txid: "bbbb",
            canBump: true,
            amount: "-0.00100000 BTC",
            date: "2026-01-02",
            depth: 0,
            status: 0,
            type: 3,
            address: "bcrt1qsendaddress"
        })
        verify(page !== null)
        compare(page.canBump, true)

        const banner = findChild(page, "speedUpBanner")
        verify(banner !== null)
    }

    function test_speedUpOverlay_prompts_for_password_when_bump_requires_unlock() {
        testBumpModel.reset()
        testBumpModel.requireUnlock = true

        const page = createTemporaryObject(detailsComponent, this, {
            txid: "bbbb",
            canBump: true,
            amount: "-0.00100000 BTC",
            date: "2026-01-02",
            depth: 0,
            status: 0,
            type: 3,
            address: "bcrt1qsendaddress"
        })
        verify(page !== null)

        const banner = findChild(page, "speedUpBanner")
        verify(banner !== null)
        const button = findChild(banner, "speedUpBannerPrimaryButton")
        verify(button !== null)
        banner.primaryClicked()

        const overlay = findChild(page, "speedUpOverlay")
        verify(overlay !== null)
        tryCompare(overlay, "opened", true)

        const updateButton = findChild(overlay, "updateTransactionButton")
        verify(updateButton !== null)
        tryCompare(updateButton, "enabled", true)
        mouseClick(updateButton)

        const popup = findChild(overlay, "speedUpPassphrasePopup")
        verify(popup !== null)
        tryCompare(popup, "opened", true)
        compare(findChild(overlay, "speedUpPassphraseErrorText").text, "")
    }

    function test_canBump_false_hides_speedUpBanner() {
        const page = createTemporaryObject(detailsComponent, this, {
            txid: "aaaa",
            canBump: false,
            amount: "+0.01000000 BTC",
            date: "2026-01-01",
            depth: 3,
            status: 2,
            type: 1,
            address: "bcrt1qreceiveaddress"
        })
        verify(page !== null)
        compare(page.canBump, false)

        const banner = findChild(page, "speedUpBanner")
        verify(banner !== null)
        compare(banner.visible, false)
    }

    function test_confirmed_tx_hides_speedUpBanner() {
        const page = createTemporaryObject(detailsComponent, this, {
            txid: "cccc",
            canBump: false,
            amount: "-0.00500000 BTC",
            date: "2026-01-03",
            depth: 6,
            status: 2,
            type: 3,
            address: "bcrt1qconfirmedaddress"
        })
        verify(page !== null)

        const banner = findChild(page, "speedUpBanner")
        verify(banner !== null)
        compare(banner.visible, false)
    }

    function test_replacedByTxid_shows_replacedBanner() {
        const page = createTemporaryObject(detailsComponent, this, {
            txid: "aaaa",
            canBump: false,
            replacedByTxid: "bbbb",
            amount: "-0.00100000 BTC",
            date: "2026-01-02",
            depth: -1,
            status: 0,
            type: 3,
            address: "bcrt1qsendaddress"
        })
        verify(page !== null)
        compare(page.replacedByTxid, "bbbb")

        const banner = findChild(page, "replacedBanner")
        verify(banner !== null)
    }

    function test_no_replacedByTxid_hides_replacedBanner() {
        const page = createTemporaryObject(detailsComponent, this, {
            txid: "bbbb",
            canBump: true,
            replacedByTxid: "",
            amount: "-0.00100000 BTC",
            date: "2026-01-02",
            depth: 0,
            status: 0,
            type: 3,
            address: "bcrt1qsendaddress"
        })
        verify(page !== null)

        const banner = findChild(page, "replacedBanner")
        verify(banner !== null)
        compare(banner.visible, false)
    }

    function test_paymentRequests_show_matching_list() {
        const page = createTemporaryObject(detailsComponent, this, {
            txid: "dddd",
            canBump: false,
            amount: "+0.01000000 BTC",
            date: "2026-01-04",
            depth: 1,
            status: 2,
            type: 1,
            address: "bcrt1qrequestaddress",
            paymentRequests: [
                {
                    requestId: "7",
                    label: "Alice",
                    amountDisplay: "0.00010000",
                    date: "Fri Jan 2 2026"
                },
                {
                    requestId: "8",
                    label: "Alice duplicate",
                    amountDisplay: "",
                    date: "Sat Jan 3 2026"
                }
            ]
        })
        verify(page !== null)
        compare(page.paymentRequestCount, 2)

        const section = findChild(page, "activityDetailsPaymentRequestsSection")
        verify(section !== null)

        tryVerify(function() {
            return findChild(page, "activityDetailsPaymentRequest_0") !== null
        })
        verify(findChild(page, "activityDetailsPaymentRequest_1") !== null)
        compare(findChild(page, "activityDetailsPaymentRequestTitle_0").text, "Alice")
        compare(findChild(page, "activityDetailsPaymentRequestTitle_1").text, "Alice duplicate")
        compare(findChild(page, "activityDetailsPaymentRequestSubtitle_1").text, "No amount - Sat Jan 3 2026")
    }

    function test_no_paymentRequests_hides_section() {
        const page = createTemporaryObject(detailsComponent, this, {
            txid: "eeee",
            canBump: false,
            amount: "+0.01000000 BTC",
            date: "2026-01-05",
            depth: 1,
            status: 2,
            type: 1,
            address: "bcrt1qrequestaddress",
            paymentRequests: []
        })
        verify(page !== null)

        const section = findChild(page, "activityDetailsPaymentRequestsSection")
        verify(section !== null)
        compare(section.visible, false)
    }

    function test_thirdPartyTransactionLinks_show_matching_layout() {
        optionsModel.thirdPartyTransactionUrls = "https://example.com/tx/%s"
        compare(optionsModel.thirdPartyTransactionLinks("ffff").length, 1)
        const page = createTemporaryObject(detailsComponent, this, {
            txid: "ffff",
            canBump: false,
            amount: "+0.01000000 BTC",
            date: "2026-01-06",
            depth: 1,
            status: 2,
            type: 1,
            address: "bcrt1qrequestaddress"
        })
        verify(page !== null)

        const section = findChild(page, "activityDetailsThirdPartyLinks")
        verify(section !== null)
        tryVerify(function() {
            return section.width > 0 && section.height > 0
        })
    }

    // Third-party links must route through the confirmation popup rather than
    // opening the URL directly, so the user sees the destination before
    // leaving the app.
    function test_thirdPartyTransactionLink_routes_through_confirmation_popup() {
        optionsModel.thirdPartyTransactionUrls = "https://example.com/tx/%s"
        const page = createTemporaryObject(detailsComponent, this, {
            txid: "ffff",
            canBump: false,
            amount: "+0.01000000 BTC",
            date: "2026-01-06",
            depth: 1,
            status: 2,
            type: 1,
            address: "bcrt1qrequestaddress"
        })
        verify(page !== null)

        tryVerify(function() {
            return findChild(page, "activityDetailsThirdPartyLink_0") !== null
        })
        const link = findChild(page, "activityDetailsThirdPartyLink_0")
        compare(link.popupObjectName, "activityDetailsThirdPartyLink_0_popup")
        // The link owns its dialog, so assert against the link's own subtree.
        verify(findChild(link, link.popupObjectName) === null)

        link.clicked()

        const popup = findChild(link, link.popupObjectName)
        verify(popup !== null)
        tryCompare(popup, "opened", true)
        compare(popup.link, "https://example.com/tx/ffff")
    }

    function test_no_thirdPartyTransactionLinks_hides_section() {
        const page = createTemporaryObject(detailsComponent, this, {
            txid: "gggg",
            canBump: false,
            amount: "+0.01000000 BTC",
            date: "2026-01-07",
            depth: 1,
            status: 2,
            type: 1,
            address: "bcrt1qrequestaddress"
        })
        verify(page !== null)

        const section = findChild(page, "activityDetailsThirdPartyLinks")
        verify(section !== null)
        compare(optionsModel.thirdPartyTransactionLinks("gggg").length, 0)
        tryVerify(function() {
            return section.height === 0
        })
    }

    function test_dimmed_when_replaced() {
        const page = createTemporaryObject(detailsComponent, this, {
            txid: "aaaa",
            canBump: false,
            replacedByTxid: "bbbb",
            amount: "-0.00100000 BTC",
            date: "2026-01-02",
            depth: -1,
            status: 0,
            type: 3,
            address: "bcrt1qsendaddress"
        })
        verify(page !== null)
        compare(page.replacedByTxid, "bbbb")
        compare(page.opacity !== undefined, true)
    }
}
