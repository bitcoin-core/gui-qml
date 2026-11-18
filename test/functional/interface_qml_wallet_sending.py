#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Encrypted multi-node sending and PSBT/replacement process happy paths."""

import base64
from decimal import Decimal
from io import BytesIO
import json
import sys
from pathlib import Path
from qml_scenarios import run_scenarios
from qml_smoke_lib import (
    create_wallet,
    save_review_psbt,
    select_wallet,
    stop_gui_checked,
    wait_until,
    wallet_scenario,
    wait_review_action,
)
from qml_wallet_test_lib import rpc_call


def original_review_outputs(gui, expected_count):
    """Observe complete typed rows of this review, not its graph preview."""
    page = "sendTransactionReviewPage"
    gui.wait_for_property(page, "detailsReady", True, timeout_ms=30000)
    revision = gui.get_property(page, "flowDetails.revision")
    assert isinstance(revision, int) and revision > 0
    model = "flowDetails.outputRows"
    guard = {"revision_property": "flowDetails.revision", "revision": revision}
    original = gui.get_model_row(page, model, **guard)
    count = original["rowCount"]
    assert count == expected_count
    entries = [gui.get_model_row(page, model, row, "entry", model_id=original["modelId"], expected_rows=count, **guard)["value"]
               for row in range(count)]
    assert gui.get_property(page, "flowDetails.revision") == revision
    assert gui.get_property(page, "detailsContextValid") is True
    assert gui.get_property(page, "detailsReady") is True
    return entries


def send_receive_smoke(harness, checkpoint):
    """Exercise multi-recipient review, broadcast, receipt and Activity navigation."""
    harness.start_source_node(extra_args=["-listen=1", "-bind=127.0.0.1"])
    rpc_call(harness.source_rpc_port, "createwallet", {"wallet_name": "receiver"})
    addresses = [rpc_call(harness.source_rpc_port, "getnewaddress", wallet="receiver") for _ in range(2)]
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    create_wallet(harness, "sender", "sending password")
    gui.wait_for_property("walletBadge", "text", "sender", timeout_ms=30000)
    mining_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="sender")
    rpc_call(harness.gui_rpc_port, "generatetoaddress", [101, mining_address])
    stop_gui_checked(harness)
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "text", "sender", timeout_ms=30000)
    assert rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet="sender")["unlocked_until"] == 0
    checkpoint("encrypted wallet restarted locked")
    rpc_call(harness.gui_rpc_port, "addnode", [f"127.0.0.1:{harness.source_p2p_port}", "onetry"])
    wait_until(lambda: rpc_call(harness.source_rpc_port, "getblockcount") == 101, "the recipient node to sync")
    checkpoint("sender funded and recipient node connected")

    gui.click("sendTabButton")
    gui.wait_for_page("sendPage")
    for index, (address, amount) in enumerate(zip(addresses, ("0.01", "0.02"))):
        if index:
            gui.click("sendAddRecipientButton")
        gui.set_text("sendAddressInput", address)
        gui.set_text("sendAmountInput", amount)
    gui.wait_for_property("sendReviewButton", "enabled", True, timeout_ms=30000)
    gui.click("sendReviewButton")
    gui.wait_for_property("reviewPassphrasePopup", "opened", True)
    gui.set_text("reviewPassphraseField", "sending password")
    gui.click("reviewPassphraseConfirmButton")
    gui.wait_for_property("transactionReviewPopup", "opened", True)
    gui.wait_for_page("sendTransactionReviewPage")
    checkpoint("two-recipient payment reviewed")
    gui.click("sendTransactionReviewSendButton")
    gui.wait_for_page("sendCompletePage", timeout_ms=30000)
    txid = gui.get_property("sendCompletePage", "txid")
    assert rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet="sender")["unlocked_until"] == 0
    wait_until(lambda: txid in rpc_call(harness.source_rpc_port, "getrawmempool"), "recipient node to receive the transaction")
    for address, expected in zip(addresses, (Decimal("0.01"), Decimal("0.02"))):
        assert Decimal(str(rpc_call(harness.source_rpc_port, "getreceivedbyaddress", [address, 0], wallet="receiver"))) == expected
    checkpoint("both outputs received on the other node")

    receiver_mining_address = rpc_call(harness.source_rpc_port, "getnewaddress", wallet="receiver")
    rpc_call(harness.source_rpc_port, "generatetoaddress", [1, receiver_mining_address])
    wait_until(lambda: rpc_call(harness.gui_rpc_port, "gettransaction", [txid], wallet="sender")["confirmations"] == 1,
               "the payment confirmation to reach the GUI node")
    gui.click("sendResultViewTransactionButton")
    gui.wait_for_page("activityDetailsPage")
    gui.wait_for_property("activityDetailsPage", "detailsLoading", False, timeout_ms=30000)
    assert gui.get_property("activityDetailsPage", "txid") == txid
    checkpoint("confirmed payment opened in Activity")


def psbt_smoke(harness, checkpoint):
    """Exercise file import, review and broadcast for a fully signed foreign PSBT."""
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    for name in ("signer", "recipient", "reviewer"):
        rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": name})
    select_wallet(gui, "reviewer")
    addresses = [rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="recipient") for _ in range(2)]
    mining_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="signer")
    rpc_call(harness.gui_rpc_port, "generatetoaddress", [101, mining_address])
    funded = rpc_call(harness.gui_rpc_port, "walletcreatefundedpsbt", [
        [], [{addresses[0]: 1}, {addresses[1]: 0.5}], 0, {"fee_rate": 2}, True,
    ], wallet="signer")
    signed = rpc_call(harness.gui_rpc_port, "walletprocesspsbt", [funded["psbt"], True, "ALL", True, True], wallet="signer")
    assert signed["complete"]
    finalized = rpc_call(harness.gui_rpc_port, "finalizepsbt", [signed["psbt"]])
    txid = rpc_call(harness.gui_rpc_port, "decoderawtransaction", [finalized["hex"]])["txid"]
    path = Path(harness.tmpdir) / "signed foreign.psbt"
    path.write_bytes(base64.b64decode(signed["psbt"]))
    checkpoint("another wallet signed the two-recipient PSBT")

    gui.click("sendTabButton")
    gui.wait_for_page("sendPage")
    gui.click("sendOptionsButton")
    gui.wait_for_property("sendOptionsPopup", "opened", True)
    gui.set_text("psbtImportPathField", str(path))
    gui.click("sendImportPsbtFromFileButton")
    gui.wait_for_property("transactionReviewPopup", "opened", True, timeout_ms=30000)
    gui.wait_for_property("sendTransactionReviewRecipient1", "visible", True)
    gui.wait_for_property("sendTransactionReviewSendButton", "enabled", True)
    gui.wait_for_property("sendTransactionReviewPage", "canBroadcast", True, timeout_ms=30000)
    checkpoint("signed PSBT imported and reviewed")
    gui.click("sendTransactionReviewSendButton")
    gui.wait_for_page("sendCompletePage", timeout_ms=30000)
    assert gui.get_property("sendCompletePage", "txid") == txid
    assert txid in rpc_call(harness.gui_rpc_port, "getrawmempool")
    for address, amount in zip(addresses, (Decimal("1"), Decimal("0.5"))):
        wait_until(
            lambda: Decimal(str(rpc_call(harness.gui_rpc_port, "getreceivedbyaddress", [address, 0], wallet="recipient"))) == amount,
            f"the recipient wallet to record the PSBT output at {address}",
        )
    gui.click("sendResultDoneButton")
    gui.wait_for_property("transactionReviewPopup", "visible", False)
    gui.wait_for_page("sendPage")
    checkpoint("both PSBT outputs broadcast and returned to Send")


def replacement_smoke(harness, checkpoint):
    """Replace an encrypted wallet's payment through Activity and confirm it."""
    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": "receiver"})
    address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="receiver")
    rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": "sender", "passphrase": "replacement password"})
    # RPC loading publishes a catalog entry; selection is a separate user intent.
    select_wallet(gui, "sender")
    mining_address = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="sender")
    rpc_call(harness.gui_rpc_port, "generatetoaddress", [101, mining_address])
    rpc_call(harness.gui_rpc_port, "walletpassphrase", ["replacement password", 60], wallet="sender")
    txid = rpc_call(harness.gui_rpc_port, "sendtoaddress", [address, 1], wallet="sender")
    rpc_call(harness.gui_rpc_port, "walletlock", wallet="sender")
    gui.click("activityTabButton")
    gui.wait_for_property("activityTabButton", "checked", True)
    gui.set_text("activitySearchField", txid)
    gui.wait_for_property("activityFilterProxyModel", "count", 1, timeout_ms=30000)
    gui.click_list_item("activityListView", 0, "activityRowOpenButton")
    gui.wait_for_page("activityDetailsPage")
    gui.wait_for_property("activityDetailsPage", "detailsLoading", False, timeout_ms=30000)
    gui.click("speedUpBannerPrimaryButton")
    gui.wait_for_property("speedUpOverlay", "opened", True)
    gui.wait_for_property("updateTransactionButton", "enabled", True, timeout_ms=30000)
    gui.click("updateTransactionButton")
    gui.wait_for_property("speedUpPassphrasePopup", "opened", True)
    gui.set_text("speedUpPassphraseField", "wrong password")
    gui.click("speedUpPassphraseConfirmButton")
    gui.wait_for_property("speedUpPassphraseErrorText", "text", "The wallet password you entered was incorrect.")
    assert rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet="sender")["unlocked_until"] == 0
    gui.set_text("speedUpPassphraseField", "replacement password")
    gui.click("speedUpPassphraseConfirmButton")
    gui.wait_for_property("speedUpPassphrasePopup", "visible", False, timeout_ms=30000)
    wait_until(lambda: txid not in rpc_call(harness.gui_rpc_port, "getrawmempool"), "the replacement to enter the mempool")
    pool = rpc_call(harness.gui_rpc_port, "getrawmempool")
    assert len(pool) == 1 and pool[0] != txid
    replacement = pool[0]
    assert replacement in rpc_call(harness.gui_rpc_port, "gettransaction", [txid], wallet="sender")["walletconflicts"]
    assert rpc_call(harness.gui_rpc_port, "getwalletinfo", wallet="sender")["unlocked_until"] == 0
    checkpoint("encrypted replacement broadcast and wallet relocked")
    rpc_call(harness.gui_rpc_port, "generatetoaddress", [1, mining_address])
    wait_until(lambda: rpc_call(harness.gui_rpc_port, "gettransaction", [replacement], wallet="sender")["confirmations"] >= 1,
               "the replacement to confirm")
    checkpoint("replacement confirmed")


def legacy_fee_matrix(harness, checkpoint, version, confirmed=True):
    """A real legacy signature survives forged amounts; the GUI must reject them."""
    # Reuse the pinned Core PSBT parser/serializer without changing its sources.
    sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "bitcoin/test/functional"))
    from test_framework.messages import CTransaction, from_binary
    from test_framework.psbt import PSBT, PSBT_IN_NON_WITNESS_UTXO, PSBT_IN_WITNESS_UTXO

    harness.start_gui()
    gui = harness.driver
    gui.wait_for_property("walletBadge", "loading", False, timeout_ms=30000)
    for name in ("fee-miner", "fee-foreign", "fee-reviewer"):
        rpc_call(harness.gui_rpc_port, "createwallet", {"wallet_name": name})
    select_wallet(gui, "fee-reviewer")
    mining = rpc_call(harness.gui_rpc_port, "getnewaddress", wallet="fee-miner")
    rpc_call(harness.gui_rpc_port, "generatetoaddress", [101, mining])
    checked = 0
    for owned in (0, 1, 2):
        for forged in (False, True):
            inputs = []
            for index in range(2):
                wallet = "fee-reviewer" if index < owned else "fee-foreign"
                address = rpc_call(harness.gui_rpc_port, "getnewaddress", ["", "legacy"], wallet=wallet)
                funding = rpc_call(harness.gui_rpc_port, "sendtoaddress", [address, 0.01], wallet="fee-miner")
                raw = rpc_call(harness.gui_rpc_port, "gettransaction", [funding], wallet="fee-miner")["hex"]
                decoded = rpc_call(harness.gui_rpc_port, "decoderawtransaction", [raw])
                output = next(item for item in decoded["vout"] if item["scriptPubKey"].get("address") == address)
                inputs.append({"txid": funding, "vout": output["n"]})
            if confirmed:
                rpc_call(harness.gui_rpc_port, "generatetoaddress", [1, mining])
            actual_inputs = sum(Decimal(str(rpc_call(harness.gui_rpc_port, "gettxout", [item["txid"], item["vout"]])["value"])) for item in inputs)
            actual_fee = actual_inputs - Decimal("0.015")
            assert actual_fee == Decimal("0.005")
            outputs = {mining: 0.015}
            if owned == 2:
                change = rpc_call(harness.gui_rpc_port, "getrawchangeaddress", wallet="fee-reviewer")
                outputs = {mining: 0.013, change: 0.002}
            doc = rpc_call(harness.gui_rpc_port, "createpsbt", [inputs, outputs, 0, False, 2, version])
            for wallet in ("fee-reviewer", "fee-foreign"):
                doc = rpc_call(harness.gui_rpc_port, "walletprocesspsbt", [doc, True, "ALL", True, True], wallet=wallet)["psbt"]
            signed = rpc_call(harness.gui_rpc_port, "finalizepsbt", [doc])
            assert signed["complete"]
            control_hex = signed["hex"]
            decoded_control = rpc_call(harness.gui_rpc_port, "decoderawtransaction", [control_hex])
            expected_txid = decoded_control["txid"]
            candidate = PSBT.from_base64(doc)
            assert candidate.version == version
            if forged or not confirmed:
                for index, item in enumerate(candidate.i):
                    if confirmed and index != 1:
                        continue
                    metadata = item.map
                    previous = from_binary(CTransaction, BytesIO(metadata[PSBT_IN_NON_WITNESS_UTXO]))
                    utxo = previous.vout[inputs[index]["vout"]]
                    if forged and index == 1:
                        utxo.nValue -= 499_999
                    metadata[PSBT_IN_WITNESS_UTXO] = utxo.serialize()
                    del metadata[PSBT_IN_NON_WITNESS_UTXO]
                doc = candidate.to_base64()
            extracted = rpc_call(harness.gui_rpc_port, "finalizepsbt", [doc])
            assert extracted["complete"] and extracted["hex"] == control_hex
            acceptance = rpc_call(harness.gui_rpc_port, "testmempoolaccept", [[control_hex]])[0]
            assert acceptance["allowed"] and Decimal(str(acceptance["fees"]["base"])) == actual_fee
            # The counterfeit is a valid payment, rather than a malformed PSBT.
            path = Path(harness.tmpdir) / f"legacy-v{version}-owned{owned}-{forged}.psbt"
            path.write_bytes(base64.b64decode(doc))
            gui.click("sendTabButton")
            gui.wait_for_page("sendPage")
            gui.set_text("psbtImportPathField", str(path))
            gui.click("sendOptionsButton")
            gui.wait_for_property("sendOptionsPopup", "opened", True)
            gui.click("sendImportPsbtFromFileButton")
            gui.wait_for_property("transactionReviewPopup", "opened", True, timeout_ms=30000)
            gui.wait_for_page("sendTransactionReviewPage")
            gui.wait_for_property("sendTransactionReviewPage", "importedPsbt", True, timeout_ms=30000)
            gui.wait_for_property("sendTransactionReviewPage", "feePending", False, timeout_ms=30000)
            full_outputs = original_review_outputs(gui, len(outputs))
            graph_outputs = gui.get_property("sendTransactionReviewFlow", "outputEntries")
            fee_indices = [index for index, entry in enumerate(graph_outputs) if entry.get("kind") == "fee"]
            assert len(fee_indices) == 1, graph_outputs
            fee_known = gui.get_property("sendTransactionReviewPage", "wallet.currentTransaction.feeKnown")
            fee_sat = gui.get_property("sendTransactionReviewPage", "wallet.currentTransaction.feeAmount.satoshi") if fee_known else None
            fee_entry = graph_outputs[fee_indices[0]]
            assert fee_entry["amountKnown"] == fee_known
            assert fee_entry["amountSat"] == fee_sat
            fee_node = f"transactionFlowOutput_{fee_indices[0]}"
            gui.wait_for_property(fee_node, "title", "Network fee")
            gui.wait_for_property(
                fee_node, "amountText",
                "Unknown amount" if forged else "0.00500000 BTC",
            )
            print("[fee-observation] " + json.dumps({"version": version, "owned_inputs": owned, "forged": forged,
                  "confirmed": confirmed, "actual_fee_sat": 500000, "summary": gui.get_property("sendTransactionReviewFee", "value"),
                  "flow_fee_known": fee_known, "flow_fee_sat": fee_sat,
                  "can_send": gui.get_property("sendTransactionReviewPage", "canSend"),
                  "can_broadcast": gui.get_property("sendTransactionReviewPage", "canBroadcast")}), flush=True)
            assert gui.get_property("sendTransactionReviewPage", "wallet.recipients.count") == 1
            # R21 imported reviews bind every output and deliberately leave
            # change intent unknown. Retain the independent Core classification
            # oracle without inferring intent from wallet ownership in the DTO.
            assert {output["index"] for output in full_outputs} == set(range(len(outputs)))
            core_change_count = 0
            for output in full_outputs:
                index = output["index"]
                authoritative = decoded_control["vout"][index]
                assert output["id"] == f"output:{index}", output
                assert output["address"] == authoritative["scriptPubKey"]["address"], output
                assert output["amountKnown"] is True, output
                assert output["amountSat"] == int(Decimal(str(authoritative["value"])) * 100_000_000), output
                info = rpc_call(harness.gui_rpc_port, "getaddressinfo", [output["address"]], wallet="fee-reviewer")
                assert (output["ownership"] == "wallet") == info["ismine"], (output, info)
                core_change_count += bool(info.get("ischange", False))
                assert output["changeKnown"] is False, output
                assert output["isChange"] is False, output
            assert core_change_count == (1 if owned == 2 else 0), full_outputs
            if forged:
                assert not fee_known and fee_sat is None
                assert not gui.get_property("sendTransactionReviewPage", "canSend")
                assert not gui.get_property("sendTransactionReviewPage", "canBroadcast")
                assert gui.get_property("sendTransactionReviewFee", "value") == "Unknown"
                assert gui.get_property("sendTransactionReviewWarning", "visible")
                send_ticket = gui.invoke_property_object("sendTransactionReviewPage", "wallet", "requestSendTransaction")
                wait_review_action(gui, send_ticket, send=True, terminal="SendFailed")
                assert gui.get_property("sendTransactionReviewPage", "wallet.transactionError")
                assert gui.get_property("sendTransactionReviewPage", "wallet.sendResultTxid") in (None, "")
                broadcast_ticket = gui.invoke_property_object("sendTransactionReviewPage", "wallet", "requestBroadcastCurrentTransaction")
                wait_review_action(gui, broadcast_ticket, terminal="PsbtFailed")
                assert gui.get_property("sendTransactionReviewPage", "wallet.psbtActionError")
                assert gui.get_property("sendTransactionReviewPage", "wallet.psbtSubmittedTxid") in (None, "")
                assert expected_txid not in rpc_call(harness.gui_rpc_port, "getrawmempool")
                # Each ownership row retains its own rejected document. Reusing
                # a destination would require a separate overwrite approval.
                exported = Path(harness.tmpdir) / f"rejected-v{version}-owned{owned}.psbt"
                save_review_psbt(gui, exported)
                retained = base64.b64encode(exported.read_bytes()).decode("ascii")
                assert rpc_call(harness.gui_rpc_port, "finalizepsbt", [retained])["hex"] == control_hex
                gui.click("sendTransactionReviewCloseButton")
                gui.wait_for_property("transactionReviewPopup", "visible", False)
                if not confirmed:
                    rpc_call(harness.gui_rpc_port, "generatetoaddress", [1, mining])
            else:
                assert fee_known and fee_sat == 500000
                assert gui.get_property("sendTransactionReviewPage", "canBroadcast")
                assert gui.get_property("sendTransactionReviewFee", "value") == "0.00500000 BTC"
                gui.click("sendTransactionReviewSendButton")
                gui.wait_for_page("sendCompletePage", timeout_ms=30000)
                wait_until(lambda: expected_txid in rpc_call(harness.gui_rpc_port, "getrawmempool"), "authenticated payment")
                paid = rpc_call(harness.gui_rpc_port, "getmempoolentry", [expected_txid])["fees"]["base"]
                assert Decimal(str(paid)) == actual_fee
                rpc_call(harness.gui_rpc_port, "generatetoaddress", [1, mining])
                gui.click("sendResultDoneButton")
                gui.wait_for_property("transactionReviewPopup", "visible", False)
            checked += 1
            print("[fee-oracle] " + json.dumps({"version": version, "owned_inputs": owned, "forged": forged,
                  "actual_fee_sat": int(actual_fee * 100_000_000), "raw_transaction_unchanged": True,
                  "protected_actions_blocked": forged, "checked": checked}), flush=True)
    assert checked == 6
    checkpoint(f"v{version}: all six ownership/metadata combinations verified with independent UTXO and mempool fees")


if __name__ == "__main__":
    raise SystemExit(run_scenarios('Sending, PSBT and replacement process journeys', [
        wallet_scenario("encrypted-multinode-send", 6, send_receive_smoke),
        wallet_scenario("signed-foreign-psbt", 7, psbt_smoke),
        wallet_scenario("legacy-fee-v0", 7, lambda h, c: legacy_fee_matrix(h, c, 0)),
        wallet_scenario("legacy-fee-v2", 7, lambda h, c: legacy_fee_matrix(h, c, 2)),
        wallet_scenario("legacy-fee-unconfirmed-v0", 7, lambda h, c: legacy_fee_matrix(h, c, 0, confirmed=False)),
        wallet_scenario("legacy-fee-unconfirmed-v2", 7, lambda h, c: legacy_fee_matrix(h, c, 2, confirmed=False)),
        wallet_scenario("encrypted-replacement", 7, replacement_smoke),
    ]))
