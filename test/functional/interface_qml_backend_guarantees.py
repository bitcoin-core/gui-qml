#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Public wallet balance observations against independent HTTP RPC effects."""

from decimal import Decimal

from qml_scenarios import run_scenarios
from qml_smoke_lib import select_wallet, wallet_scenario
from qml_wallet_test_lib import rpc_call


def trusted_satoshi(harness, name):
    balance = rpc_call(harness.gui_rpc_port, "getbalances", wallet=name)["mine"]["trusted"]
    satoshi = Decimal(str(balance)) * 100_000_000
    assert satoshi == satoshi.to_integral_value()
    return int(satoshi)


def public_balance_observation(harness, checkpoint):
    harness.start_gui()
    gui = harness.driver
    name = "balance_observation"
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": name})
    select_wallet(gui, name)
    gui.wait_for_property("walletBadge", "balanceKnown", True, timeout_ms=30000)
    gui.wait_for_property("walletBadge", "balanceSatoshi", 0)
    checkpoint("public grouped observation distinguishes a known empty wallet")

    address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet=name)
    rpc_call(harness.gui_rpc_port, "generatetoaddress", [101, address])
    funded = trusted_satoshi(harness, name)
    assert funded > 0
    gui.wait_for_property("walletBadge", "balanceSatoshi", funded, timeout_ms=30000)
    checkpoint("independent HTTP mining is reflected by the cached trusted-balance consumer")

    rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": "balance_recipient"})
    destination = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="balance_recipient")
    # Select the original opening through the existing wallet picker.
    gui.click("walletBadge")
    gui.wait_for_property("walletSelectPopup", "opened", True)
    gui.click(f"walletSelectItem_{name}")
    gui.wait_for_property("walletBadge", "text", name, timeout_ms=30000)
    txid = rpc_call(harness.gui_rpc_port, "sendtoaddress", [destination, 1], wallet=name)
    observed = trusted_satoshi(harness, name)
    assert 0 < observed < funded
    gui.wait_for_property("walletBadge", "balanceSatoshi", observed, timeout_ms=30000)
    assert rpc_call(harness.gui_rpc_port, "gettransaction", [txid], wallet=name)["amount"] < 0
    checkpoint("an independent HTTP spend updates the original wallet without getter-side Core reads")


if __name__ == "__main__":
    raise SystemExit(run_scenarios("Backend public-interface observations", [
        wallet_scenario("public-balance-observation", 4, public_balance_observation),
    ]))
