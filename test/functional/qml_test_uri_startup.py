#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Functional test for payment URIs passed on the command line.

A desktop URI handler starts the application with the bitcoin: URI as an
argument. The request is captured during init, queued while the shell and the
wallet are still being built, and handed to the Send flow once both exist.

Requires:
  - bitcoin-core-app built with -DENABLE_TEST_AUTOMATION=ON
  - bitcoind built with -DBUILD_DAEMON=ON
"""

import sys
import time

from qml_wallet_test_lib import WalletFlowHarness, rpc_call, wait_for_rpc

WALLET_NAME = "testwallet"

# Valid on mainnet, rejected by the address decoder under regtest.
MAINNET_ADDRESS = "bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4"


REVIEW_POPUP = "sendPaymentRequestReviewPopup"


def wait_for_review(gui, timeout_ms=20000):
    gui.wait_for_property(REVIEW_POPUP, "opened", True, timeout_ms=timeout_ms)


def apply_review(gui):
    """Accept the review. The popup can reopen at once for the next request,
    so callers assert the effect rather than the popup closing."""
    wait_for_review(gui)
    gui.click("paymentRequestReviewApplyButton")


def discard_review(gui):
    wait_for_review(gui)
    gui.click("paymentRequestReviewDiscardButton")


def wait_for_wallet(gui):
    """Wait until the wallet badge reports the test wallet as loaded."""
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    gui.wait_for_property("walletBadge", "text", WALLET_NAME, timeout_ms=30000)
    gui.wait_for_property("walletBadge", "noWalletLoaded", False, timeout_ms=10000)


def run_tests():
    harness = WalletFlowHarness("qml_test_uri_startup", port_offset=510)
    try:
        harness.start_gui()
        gui = harness.driver

        gui.wait_for_property("walletBadge", "loading", False, timeout_ms=20000)
        gui.wait_for_property("walletBadge", "visible", True, timeout_ms=10000)
        rpc_call(
            harness.gui_rpc_port, "createwallet",
            {"wallet_name": WALLET_NAME, "load_on_startup": True},
        )
        wait_for_wallet(gui)

        first_address = rpc_call(
            harness.gui_rpc_port, "getnewaddress", ["uri-startup", "bech32"],
            wallet=WALLET_NAME,
        )
        second_address = rpc_call(
            harness.gui_rpc_port, "getnewaddress", ["uri-startup-2", "bech32"],
            wallet=WALLET_NAME,
        )
        print(f"Test addresses: {first_address}, {second_address}")

        # ----------------------------------------------------------------
        # Test 1: a valid URI on the command line reaches the Send form.
        # The amount carries a '=' inside the query string, which the
        # argument parser splits on; the URI must survive intact.
        # ----------------------------------------------------------------
        uri = (
            f"bitcoin:{first_address}"
            f"?amount=0.01234567&label=startup-label&message=startup-note"
        )
        harness.restart_gui(extra_args=[uri])
        wait_for_rpc(harness.gui_rpc_port)
        gui = harness.driver
        wait_for_wallet(gui)
        gui.wait_for_page("sendPage", timeout_ms=20000)
        wait_for_review(gui)
        assert first_address in str(gui.get_property("paymentRequestReviewAddress", "text"))
        assert "0.01234567" in str(gui.get_property("paymentRequestReviewAmount", "text"))
        assert "startup-label" in str(gui.get_property("paymentRequestReviewLabel", "text"))
        assert "startup-note" in str(gui.get_property("paymentRequestReviewMessage", "text"))
        assert gui.get_property(REVIEW_POPUP, "replacesValues") is False
        apply_review(gui)
        gui.wait_for_property(
            "sendPaymentRequestStatusText", "text",
            lambda v: "command line" in str(v), timeout_ms=20000,
        )
        gui.wait_for_property(
            "sendPaymentRequestPayToValue", "text",
            lambda v: "startup-label" in str(v), timeout_ms=10000,
        )
        gui.wait_for_property(
            "sendPaymentRequestMessageTextValue", "text",
            lambda v: "startup-note" in str(v), timeout_ms=10000,
        )
        print("Test 1 PASSED: request reviewed, then applied to the Send form.")

        # ----------------------------------------------------------------
        # Test 2: a URI with an uppercase scheme is accepted. Desktop
        # handlers are not required to preserve the case of the scheme.
        # ----------------------------------------------------------------
        harness.restart_gui(extra_args=[f"BITCOIN:{first_address}?label=upper-label"])
        wait_for_rpc(harness.gui_rpc_port)
        gui = harness.driver
        wait_for_wallet(gui)
        gui.wait_for_page("sendPage", timeout_ms=20000)
        apply_review(gui)
        gui.wait_for_property(
            "sendPaymentRequestPayToValue", "text",
            lambda v: "upper-label" in str(v), timeout_ms=20000,
        )
        print("Test 2 PASSED: uppercase scheme accepted.")

        # ----------------------------------------------------------------
        # Test 3: a malformed URI is reported without filling the form.
        # ----------------------------------------------------------------
        harness.restart_gui(extra_args=[f"bitcoin://{first_address}?amount=0.1"])
        wait_for_rpc(harness.gui_rpc_port)
        gui = harness.driver
        wait_for_wallet(gui)
        gui.wait_for_page("sendPage", timeout_ms=20000)
        gui.wait_for_property(
            "sendPaymentRequestStatusText", "text",
            lambda v: "bitcoin://" in str(v), timeout_ms=20000,
        )
        assert gui.get_property("sendPaymentRequestPayToValue", "text") == "", (
            "A malformed URI must not populate the recipient label"
        )
        print("Test 3 PASSED: malformed URI reported, form untouched.")

        # ----------------------------------------------------------------
        # Test 4: several URIs are delivered in order. The first fills the
        # empty form; the second reaches the same form but cannot replace a
        # populated recipient without confirmation.
        # ----------------------------------------------------------------
        harness.restart_gui(extra_args=[
            f"bitcoin:{first_address}?label=first-label",
            f"bitcoin:{second_address}?label=second-label",
        ])
        wait_for_rpc(harness.gui_rpc_port)
        gui = harness.driver
        wait_for_wallet(gui)
        gui.wait_for_page("sendPage", timeout_ms=20000)
        apply_review(gui)
        gui.wait_for_property(
            "sendPaymentRequestPayToValue", "text",
            lambda v: "first-label" in str(v), timeout_ms=20000,
        )
        gui.wait_for_property(
            REVIEW_POPUP, "replacesValues", True, timeout_ms=20000,
        )
        discard_review(gui)
        gui.wait_for_property(REVIEW_POPUP, "opened", False, timeout_ms=10000)
        assert "first-label" in str(gui.get_property("sendPaymentRequestPayToValue", "text")), (
            "Discarding the review must leave the first request in the form"
        )
        print("Test 4 PASSED: second request reviewed, discarding keeps the first.")

        # ----------------------------------------------------------------
        # Test 5: with no wallet loaded the request is kept, not consumed,
        # and reaches the form once a wallet becomes available.
        # ----------------------------------------------------------------
        rpc_call(
            harness.gui_rpc_port, "unloadwallet",
            {"wallet_name": WALLET_NAME, "load_on_startup": False},
        )
        harness.restart_gui(extra_args=[f"bitcoin:{first_address}?label=late-label"])
        wait_for_rpc(harness.gui_rpc_port)
        gui = harness.driver
        gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
        gui.wait_for_property("walletBadge", "noWalletLoaded", True, timeout_ms=20000)
        gui.wait_for_property(
            "paymentUriNoWalletPopup", "opened", True, timeout_ms=20000,
        )
        assert gui.get_property("sendPaymentRequestPayToValue", "text") == "", (
            "The request was applied to the empty wallet model"
        )
        gui.click("paymentUriNoWalletOpenButton")
        rpc_call(
            harness.gui_rpc_port, "loadwallet",
            {"filename": WALLET_NAME, "load_on_startup": True},
        )
        wait_for_wallet(gui)
        gui.wait_for_page("sendPage", timeout_ms=20000)
        apply_review(gui)
        gui.wait_for_property(
            "sendPaymentRequestPayToValue", "text",
            lambda v: "late-label" in str(v), timeout_ms=30000,
        )
        print("Test 5 PASSED: no-wallet offer raised, request kept until a wallet loaded.")

        # ----------------------------------------------------------------
        # Test 6: a rejected request does not hold back the request
        # behind it.
        # ----------------------------------------------------------------
        harness.restart_gui(extra_args=[
            f"bitcoin://{first_address}?amount=0.1",
            f"bitcoin:{second_address}?label=after-error-label",
        ])
        wait_for_rpc(harness.gui_rpc_port)
        gui = harness.driver
        wait_for_wallet(gui)
        gui.wait_for_page("sendPage", timeout_ms=20000)
        gui.wait_for_property(
            "sendPaymentRequestStatusText", "text",
            lambda v: "bitcoin://" in str(v), timeout_ms=20000,
        )
        # The rejection stays visible behind the review of the next request.
        wait_for_review(gui)
        assert gui.get_property("sendPaymentRequestPayToValue", "text") == "", (
            "The next request was applied before it was reviewed"
        )
        apply_review(gui)
        gui.wait_for_property(
            "sendPaymentRequestPayToValue", "text",
            lambda v: "after-error-label" in str(v), timeout_ms=20000,
        )
        print("Test 6 PASSED: rejection does not block the next request.")

        # ----------------------------------------------------------------
        # Test 7: a confirmation interrupted by a wallet switch keeps its
        # request queued instead of dropping it with the popup.
        # ----------------------------------------------------------------
        harness.restart_gui(extra_args=[
            f"bitcoin:{first_address}?label=before-switch-label",
            f"bitcoin:{second_address}?label=after-switch-label",
        ])
        wait_for_rpc(harness.gui_rpc_port)
        gui = harness.driver
        wait_for_wallet(gui)
        gui.wait_for_page("sendPage", timeout_ms=20000)
        apply_review(gui)
        gui.wait_for_property(
            REVIEW_POPUP, "replacesValues", True, timeout_ms=20000,
        )
        rpc_call(
            harness.gui_rpc_port, "unloadwallet",
            {"wallet_name": WALLET_NAME, "load_on_startup": True},
        )
        gui.wait_for_property(REVIEW_POPUP, "opened", False, timeout_ms=20000)
        rpc_call(
            harness.gui_rpc_port, "loadwallet",
            {"filename": WALLET_NAME, "load_on_startup": True},
        )
        wait_for_wallet(gui)
        apply_review(gui)
        gui.wait_for_property(
            "sendPaymentRequestPayToValue", "text",
            lambda v: "after-switch-label" in str(v), timeout_ms=30000,
        )
        print("Test 7 PASSED: interrupted confirmation kept its request.")

        # ----------------------------------------------------------------
        # Test 8: the request behind a rejection replaces its error once
        # it is applied.
        # ----------------------------------------------------------------
        harness.restart_gui(extra_args=[
            f"bitcoin://{first_address}?amount=0.1",
            f"bitcoin:{second_address}?label=queued-label",
        ])
        wait_for_rpc(harness.gui_rpc_port)
        gui = harness.driver
        wait_for_wallet(gui)
        gui.wait_for_page("sendPage", timeout_ms=20000)
        apply_review(gui)
        gui.wait_for_property(
            "sendPaymentRequestPayToValue", "text",
            lambda v: "queued-label" in str(v), timeout_ms=20000,
        )
        gui.wait_for_property(
            "sendPaymentRequestStatusText", "visible", False, timeout_ms=10000,
        )
        print("Test 8 PASSED: the next request replaced the rejection error.")

        # ----------------------------------------------------------------
        # Test 9: an address from another network is reported like any other
        # rejection. Parsing happens at delivery time, so the request is
        # checked against the chain the node actually runs.
        # ----------------------------------------------------------------
        harness.restart_gui(extra_args=[f"bitcoin:{MAINNET_ADDRESS}?amount=0.1"])
        wait_for_rpc(harness.gui_rpc_port)
        gui = harness.driver
        wait_for_wallet(gui)
        gui.wait_for_page("sendPage", timeout_ms=20000)
        gui.wait_for_property(
            "sendPaymentRequestStatusText", "text",
            lambda v: "valid Bitcoin address" in str(v), timeout_ms=20000,
        )
        assert gui.get_property("sendPaymentRequestPayToValue", "text") == "", (
            "A request for another network must not populate the form"
        )
        print("Test 9 PASSED: address from another network rejected.")

        # ----------------------------------------------------------------
        # Test 10: discarding a request that had nowhere to go lets the next
        # one through instead of stalling the queue behind it.
        # ----------------------------------------------------------------
        rpc_call(
            harness.gui_rpc_port, "unloadwallet",
            {"wallet_name": WALLET_NAME, "load_on_startup": False},
        )
        harness.restart_gui(extra_args=[
            f"bitcoin:{first_address}?label=discarded-label",
            f"bitcoin:{second_address}?label=survivor-label",
        ])
        wait_for_rpc(harness.gui_rpc_port)
        gui = harness.driver
        gui.wait_for_property("walletBadge", "noWalletLoaded", True, timeout_ms=30000)
        gui.wait_for_property(
            "paymentUriNoWalletPopup", "opened", True, timeout_ms=20000,
        )
        gui.click("paymentUriNoWalletDiscardButton")
        # Nothing else happens in between: the notice must come back on its own
        # for the second request. Sleep past the close transition so a popup
        # still animating out is not read as one that reopened.
        time.sleep(1.5)
        assert gui.get_property("paymentUriNoWalletPopup", "opened") is True, (
            "Discarding the first request left the second one unannounced"
        )
        rpc_call(
            harness.gui_rpc_port, "loadwallet",
            {"filename": WALLET_NAME, "load_on_startup": True},
        )
        wait_for_wallet(gui)
        apply_review(gui)
        gui.wait_for_property(
            "sendPaymentRequestPayToValue", "text",
            lambda v: "survivor-label" in str(v), timeout_ms=30000,
        )
        gui.wait_for_property(
            "paymentUriNoWalletPopup", "opened", False, timeout_ms=5000,
        )
        print("Test 10 PASSED: discarding one request released the next.")

        # ----------------------------------------------------------------
        # Test 11: a wallet that appears while wallet creation covers the shell
        # still gets the request, which is delivered once it closes.
        # ----------------------------------------------------------------
        rpc_call(
            harness.gui_rpc_port, "unloadwallet",
            {"wallet_name": WALLET_NAME, "load_on_startup": False},
        )
        harness.restart_gui(extra_args=[f"bitcoin:{first_address}?label=wizard-label"])
        wait_for_rpc(harness.gui_rpc_port)
        gui = harness.driver
        gui.wait_for_property("walletBadge", "noWalletLoaded", True, timeout_ms=30000)
        gui.wait_for_property(
            "paymentUriNoWalletPopup", "opened", True, timeout_ms=20000,
        )
        gui.click("paymentUriNoWalletCreateButton")
        gui.wait_for_page("walletCreationTypePage", timeout_ms=10000)
        rpc_call(
            harness.gui_rpc_port, "loadwallet",
            {"filename": WALLET_NAME, "load_on_startup": True},
        )
        wait_for_wallet(gui)
        assert not gui.get_property("sendPaymentRequestReviewPopup", "opened")
        gui.click("onboardingCloseButton")
        gui.wait_for_property("walletCreationModal", "visible", False, timeout_ms=5000)
        apply_review(gui)
        gui.wait_for_property(
            "sendPaymentRequestPayToValue", "text",
            lambda v: "wizard-label" in str(v), timeout_ms=30000,
        )
        print("Test 11 PASSED: request delivered after wallet creation closed.")

        # ----------------------------------------------------------------
        # Test 12: started without wallet support there is nowhere to send
        # the request, so it is reported instead of waiting forever. Runs
        # last because it leaves the application without a wallet.
        # ----------------------------------------------------------------
        harness.restart_gui(extra_args=[
            "-disablewallet",
            f"bitcoin:{first_address}?label=disabled-label",
        ])
        wait_for_rpc(harness.gui_rpc_port)
        gui = harness.driver
        gui.wait_for_property(
            "paymentUriWalletDisabledPopup", "opened", True, timeout_ms=30000,
        )
        gui.click("paymentUriWalletDisabledOkButton")
        gui.wait_for_property(
            "paymentUriWalletDisabledPopup", "opened", False, timeout_ms=10000,
        )
        print("Test 12 PASSED: request reported in node-only mode.")

        print("\n" + "=" * 50)
        print("All tests PASSED")
        print("=" * 50)

    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        harness.stop()


if __name__ == '__main__':
    run_tests()
