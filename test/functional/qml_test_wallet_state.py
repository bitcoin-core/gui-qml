#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Refresh an open wallet after descriptor imports and coinbase maturity."""

import os
import sys

from qml_test_harness import dump_qml_tree
from qml_test_receive import _address_from_bip21, _create_request, _import_wallet_backup, _open_receive, _request_qr_payload, wait_until
from qml_wallet_test_lib import WalletFlowHarness, rpc_call


WALLET_NAME = "wallet_state"


def run_test():
    harness = WalletFlowHarness("qml_wallet_state", port_offset=1110)
    try:
        harness.start_source_node()
        port = harness.source_rpc_port
        rpc_call(port, "createwallet", {"wallet_name": "descriptor_source"})
        descriptors = rpc_call(port, "listdescriptors", [True], wallet="descriptor_source")["descriptors"]
        rpc_call(port, "createwallet", {"wallet_name": "miner"})
        outside_address = rpc_call(port, "getnewaddress", wallet="miner")
        rpc_call(port, "createwallet", {"wallet_name": WALLET_NAME, "blank": True})
        backup = os.path.join(harness.tmpdir, f"{WALLET_NAME}.bak")
        rpc_call(port, "backupwallet", [backup], wallet=WALLET_NAME)
        harness.stop_source_node()

        harness.start_gui()
        gui = harness.driver
        _import_wallet_backup(gui, backup, WALLET_NAME)
        _open_receive(gui)
        assert gui.get_property("receiveAddressTypePicker", "model") == []

        print("[qml_wallet_state] importing descriptors into the open blank wallet")
        imported = rpc_call(harness.gui_rpc_port, "importdescriptors", [descriptors], wallet=WALLET_NAME)
        assert all(result["success"] for result in imported), imported
        wait_until(lambda: bool(gui.get_property("receiveAddressTypePicker", "model")),
                   description="Receive address types after descriptor import")
        gui.click("receiveAddressSettingsButton")
        gui.wait_for_property("receiveAddressTypeMenu", "opened", True)
        gui.click("receiveAddressType_bech32")
        gui.wait_for_property("receiveAddressTypeMenu", "visible", False)
        _create_request(gui, "", "", "")
        address = _address_from_bip21(_request_qr_payload(gui))
        assert address.startswith("bcrt1q")
        gui.click("paymentRequestModalClose")
        gui.wait_for_property("paymentRequestModal", "visible", False)

        # Only the first coinbase involves this wallet. The following blocks
        # mature it without sending a wallet transaction notification.
        rpc_call(harness.gui_rpc_port, "generatetoaddress", [1, address])
        gui.click("sendTabButton")
        gui.wait_for_page("sendPage")
        gui.set_text("sendAddressInput", outside_address)
        gui.set_text("sendAmountInput", "1")
        gui.wait_for_property("sendReviewButton", "enabled", False)
        gui.wait_for_property("walletBadge", "balanceSatoshi", 0)

        print("[qml_wallet_state] maturing funds without another wallet transaction")
        rpc_call(harness.gui_rpc_port, "generatetoaddress", [100, outside_address])
        wait_until(lambda: rpc_call(harness.gui_rpc_port, "getbalances", wallet=WALLET_NAME)["mine"]["trusted"] == 50,
                   description="wallet processing the maturity block")
        gui.wait_for_property("walletBadge", "balanceSatoshi", 50 * 100_000_000, timeout_ms=20000)
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        gui.set_text("sendAmountInput", "")
        gui.set_text("sendAmountInput", "1")
        gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=20000)
        print("[qml_wallet_state] PASSED")
        return True
    except Exception as error:
        print(f"[qml_wallet_state] FAILED: {error}")
        if harness.driver:
            dump_qml_tree(harness.driver)
        return False
    finally:
        harness.stop()


if __name__ == "__main__":
    sys.exit(0 if run_test() else 1)
