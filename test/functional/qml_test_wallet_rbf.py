#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""End-to-end GUI test for RBF (Replace-By-Fee) speed up flow."""

import sys
import time
from decimal import Decimal

from qml_test_harness import dump_qml_tree
from qml_wallet_test_lib import WalletFlowHarness, rpc_call


GUI_WALLET_NAME = "rbf_wallet"
RECEIVER_WALLET_NAME = "rbf_receiver"
SEND_AMOUNT = "1.00000000"


def wait_until(predicate, *, timeout=20, interval=0.25, description="condition"):
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        try:
            if predicate():
                return
        except Exception as err:
            last_error = err
        time.sleep(interval)
    if last_error is not None:
        raise AssertionError(f"Timed out waiting for {description}: {last_error}") from last_error
    raise AssertionError(f"Timed out waiting for {description}")


def wait_for_wallet_balance(port, wallet_name, *, minimum_balance):
    def has_balance():
        balance = Decimal(str(rpc_call(port, "getbalance", wallet=wallet_name)))
        return balance >= minimum_balance

    wait_until(has_balance, timeout=30, description=f"{wallet_name} balance >= {minimum_balance}")


def wait_for_mempool_change(port, *, exclude_txid, timeout=20):
    result = [None]

    def has_replacement():
        txids = rpc_call(port, "getrawmempool")
        for t in txids:
            if t != exclude_txid:
                result[0] = t
                return True
        return False

    wait_until(has_replacement, timeout=timeout, description="replacement tx in mempool")
    return result[0]


def wait_for_wallet_tx(port, wallet_name, txid, *, timeout=20):
    result = {}

    def has_transaction():
        nonlocal result
        result = rpc_call(port, "gettransaction", [txid], wallet=wallet_name)
        return result.get("txid") == txid

    wait_until(
        has_transaction,
        timeout=timeout,
        description=f"{wallet_name} transaction {txid} visible over RPC",
    )
    return result


def wait_for_gui_object(gui, object_name, *, timeout=20, interval=0.25):
    def exists():
        return any(
            obj.get("objectName") == object_name
            for obj in gui.list_objects()
        )

    wait_until(
        exists,
        timeout=timeout,
        interval=interval,
        description=f"{object_name} exists in QML tree",
    )


def ensure_activity_tab_selected(gui, *, timeout=10, attempts=3):
    for attempt in range(1, attempts + 1):
        if gui.get_property("desktopWalletsActivityTab", "checked"):
            return

        gui.click("desktopWalletsActivityTab")
        try:
            gui.wait_for_property("desktopWalletsActivityTab", "checked", True, timeout_ms=int(timeout * 1000))
            return
        except Exception:
            if attempt == attempts:
                raise
            gui.settle()

    raise AssertionError("Failed to select the Activity tab")


def wait_for_speedup_overlay_ready(gui, *, timeout=10, interval=0.25):
    error_text = [""]

    def ready_or_failed():
        if gui.get_property("updateTransactionButton", "enabled"):
            return True

        try:
            failure_visible = gui.get_property("speedUpErrorText", "visible")
        except Exception:
            failure_visible = False
        if failure_visible:
            error_text[0] = gui.get_text("speedUpErrorText").strip()
            return True

        return False

    wait_until(
        ready_or_failed,
        timeout=timeout,
        interval=interval,
        description="speed up overlay ready or failed",
    )

    if gui.get_property("updateTransactionButton", "enabled"):
        return

    raise AssertionError(f"Speed up overlay failed: {error_text[0] or 'No error text exposed.'}")


def run_test():
    harness = WalletFlowHarness("qml_test_wallet_rbf", port_offset=70)
    gui = None
    try:
        print("[rbf] starting source node")
        harness.start_source_node()
        rpc_call(harness.source_rpc_port, "createwallet", {"wallet_name": RECEIVER_WALLET_NAME})
        receiver_address = rpc_call(harness.source_rpc_port, "getnewaddress", wallet=RECEIVER_WALLET_NAME)

        print("[rbf] starting gui")
        harness.start_gui()
        gui = harness.driver
        gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
        gui.wait_for_property("walletBadge", "visible", True, timeout_ms=10000)

        rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": GUI_WALLET_NAME})
        gui.wait_for_property("walletBadge", "text", GUI_WALLET_NAME, timeout_ms=20000)
        gui.wait_for_property("walletBadge", "noWalletLoaded", False, timeout_ms=10000)

        print("[rbf] funding wallet")
        mining_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=GUI_WALLET_NAME)
        rpc_call(harness.gui_rpc_port, "generatetoaddress", [101, mining_address])
        wait_for_wallet_balance(harness.gui_rpc_port, GUI_WALLET_NAME, minimum_balance=Decimal("50"))

        print("[rbf] sending initial transaction")
        txid = rpc_call(
            harness.gui_rpc_port,
            "sendtoaddress",
            [receiver_address, SEND_AMOUNT],
            wallet=GUI_WALLET_NAME,
        )
        print(f"[rbf] sent txid: {txid}")
        wait_for_wallet_tx(harness.gui_rpc_port, GUI_WALLET_NAME, txid)

        print("[rbf] navigating to activity tab")
        ensure_activity_tab_selected(gui)
        gui.settle()

        print("[rbf] waiting for unconfirmed tx in activity list")
        activity_item_name = f"activityItem_{txid}"
        try:
            wait_for_gui_object(gui, activity_item_name, timeout=15)
            gui.wait_for_property(activity_item_name, "visible", True, timeout_ms=5000)
        except Exception:
            print("[rbf] activity item not found, listing all named objects:")
            for obj in gui.list_objects():
                name = obj.get("objectName", "")
                if name:
                    print(f"  {name} ({obj.get('className', '')})")
            raise
        gui.click(activity_item_name)
        gui.settle()

        print("[rbf] verifying speed up banner")
        wait_for_gui_object(gui, "speedUpBanner", timeout=10)
        wait_for_gui_object(gui, "speedUpBannerPrimaryButton", timeout=10)

        print("[rbf] opening speed up overlay")
        gui.click("speedUpBannerPrimaryButton")
        gui.wait_for_property("speedUpOverlay", "opened", True, timeout_ms=10000)
        gui.settle()
        wait_for_speedup_overlay_ready(gui, timeout=10)

        print("[rbf] confirming bump")
        gui.click("updateTransactionButton")
        gui.settle()

        print("[rbf] waiting for replacement tx")
        new_txid = wait_for_mempool_change(harness.gui_rpc_port, exclude_txid=txid)
        assert new_txid != txid, f"Expected different txid, got same: {txid}"
        print(f"[rbf] replacement txid: {new_txid}")

        print("[rbf] verifying original tx conflict state")
        original_tx = rpc_call(harness.gui_rpc_port, "gettransaction", [txid], wallet=GUI_WALLET_NAME)
        wallet_conflicts = original_tx.get("walletconflicts", [])
        assert new_txid in wallet_conflicts, (
            f"Expected {new_txid} in walletconflicts, got {wallet_conflicts}"
        )

        print("[rbf] mining replacement tx")
        rpc_call(harness.gui_rpc_port, "generatetoaddress", [1, mining_address])
        wait_until(
            lambda: rpc_call(
                harness.gui_rpc_port, "gettransaction", [new_txid], wallet=GUI_WALLET_NAME
            )["confirmations"] >= 1,
            timeout=20,
            description="replacement tx confirmed",
        )

        print("[rbf] verifying confirmed tx is not bumpable")
        try:
            rpc_call(harness.gui_rpc_port, "bumpfee", [new_txid], wallet=GUI_WALLET_NAME)
            assert False, "bumpfee on confirmed tx should have raised an error"
        except RuntimeError as rpc_err:
            message = str(rpc_err).lower()
            accepted_reasons = ("confirmed", "cannot bump", "already spent")
            assert any(reason in message for reason in accepted_reasons), f"Unexpected error: {rpc_err}"
            print(f"[rbf] confirmed tx correctly rejected: {rpc_err}")

        print("[rbf] ALL TESTS PASSED")
        return 0

    except Exception as err:
        print(f"\nFAILED: {err}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if gui is not None:
            try:
                dump_qml_tree(gui)
            except Exception:
                pass
        gui_output = harness.process_output(harness.gui_process)
        if gui_output:
            print("\n--- GUI process output ---", file=sys.stderr)
            print(gui_output, file=sys.stderr)
        return 1
    finally:
        harness.stop()


if __name__ == "__main__":
    sys.exit(run_test())
