// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/transactionactivity.h>

#include <interfaces/wallet.h>
#include <key_io.h>

#include <algorithm>
#include <utility>

std::optional<TransactionActivity> TransactionActivity::fromWalletTx(const interfaces::WalletTx& wtx)
{
    if (!wtx.tx || wtx.tx->vin.empty() || wtx.tx->vout.empty() ||
        wtx.txin_is_mine.size() != wtx.tx->vin.size() ||
        wtx.txout_is_mine.size() != wtx.tx->vout.size() ||
        wtx.txout_is_change.size() != wtx.tx->vout.size() ||
        wtx.txout_address.size() != wtx.tx->vout.size()) {
        return std::nullopt;
    }

    const auto owned_inputs = wtx.is_coinbase ? 0 : std::count(wtx.txin_is_mine.begin(), wtx.txin_is_mine.end(), true);
    const auto owned_outputs = std::count(wtx.txout_is_mine.begin(), wtx.txout_is_mine.end(), true);
    if (owned_inputs == 0 && owned_outputs == 0) return std::nullopt;

    TransactionActivity activity;
    activity.txid = QString::fromStdString(wtx.tx->GetHash().GetHex());
    activity.timestamp = wtx.time;
    activity.wallet_debit = wtx.debit;
    activity.wallet_credit = wtx.credit;
    if (wtx.is_coinbase) {
        // WalletTx::credit excludes immature rewards. Activity still shows
        // their owned output value while the status explains spendability.
        activity.wallet_credit = 0;
        for (size_t i{0}; i < wtx.tx->vout.size(); ++i) {
            if (wtx.txout_is_mine[i]) activity.wallet_credit += wtx.tx->vout[i].nValue;
        }
    }
    if (const auto it = wtx.value_map.find("replaces_txid"); it != wtx.value_map.end()) {
        activity.replaces_txid = QString::fromStdString(it->second);
    }
    if (const auto it = wtx.value_map.find("replaced_by_txid"); it != wtx.value_map.end()) {
        activity.replaced_by_txid = QString::fromStdString(it->second);
    }

    const bool all_from_wallet = !wtx.is_coinbase && std::cmp_equal(owned_inputs, wtx.tx->vin.size());
    const bool mixed_inputs = owned_inputs > 0 && !all_from_wallet;
    if (all_from_wallet) {
        const CAmount fee = wtx.debit - wtx.tx->GetValueOut();
        if (fee >= 0) activity.fee = fee;
    }

    // Zero-value data outputs do not add a payment action or turn an internal
    // transfer into an external send. Value-bearing burns remain visible.
    const auto is_data_output = [](const CTxOut& output) {
        return output.nValue == 0 && output.scriptPubKey.IsUnspendable();
    };
    bool all_to_wallet{true};
    size_t internal_output_count{0};
    size_t internal_recipient_count{0};
    for (size_t i{0}; i < wtx.tx->vout.size(); ++i) {
        if (is_data_output(wtx.tx->vout[i])) continue;
        if (wtx.txout_is_mine[i]) {
            ++internal_output_count;
            if (!wtx.txout_is_change[i]) ++internal_recipient_count;
        } else {
            all_to_wallet = false;
        }
    }
    const bool internal = all_from_wallet && all_to_wallet && internal_output_count > 0;
    // Reducing the wallet's output count is a consolidation, including when
    // the destination is an explicit receive address. Otherwise a payment to
    // one wallet recipient plus automatic change remains a self-send.
    const bool consolidation = internal && std::cmp_greater(owned_inputs, internal_output_count);
    const bool self_send = internal && !consolidation && internal_recipient_count == 1;

    if (mixed_inputs) {
        TransactionAction contribution;
        contribution.id = activity.txid + QStringLiteral(":wallet-inputs");
        contribution.direction = TransactionAction::Direction::Send;
        contribution.source = TransactionAction::Source::WalletInputs;
        contribution.amount = wtx.debit;
        for (size_t i{0}; i < wtx.tx->vin.size(); ++i) {
            if (wtx.txin_is_mine[i]) contribution.inputs.push_back(wtx.tx->vin[i].prevout);
        }
        activity.actions.push_back(std::move(contribution));
    }

    for (size_t i{0}; i < wtx.tx->vout.size(); ++i) {
        const auto& output = wtx.tx->vout[i];
        if (is_data_output(output)) continue;
        const bool mine = wtx.txout_is_mine[i];
        // With foreign inputs we cannot attribute external outputs to this
        // wallet. Keep all owned receipts, including change, so the gross input
        // contribution and receipts account for the wallet's net impact.
        if (!all_from_wallet && !mine) continue;
        if (all_from_wallet && (!internal || self_send) && mine && wtx.txout_is_change[i]) continue;

        TransactionAction action;
        action.id = activity.txid + QStringLiteral(":output:") + QString::number(i);
        action.output_index = static_cast<int>(i);
        action.amount = output.nValue;
        action.address = QString::fromStdString(EncodeDestination(wtx.txout_address[i]));
        action.direction = all_from_wallet
            ? (mine ? TransactionAction::Direction::Internal : TransactionAction::Direction::Send)
            : TransactionAction::Direction::Receive;
        activity.actions.push_back(std::move(action));
    }

    if (wtx.is_coinbase) {
        activity.type = Type::Mined;
    } else if (internal) {
        if (consolidation) {
            activity.type = Type::Consolidation;
        } else if (!self_send && std::cmp_less(owned_inputs, internal_output_count)) {
            activity.type = Type::Split;
        } else {
            activity.type = Type::InternalTransfer;
        }
    } else if (activity.actions.size() > 1) {
        activity.type = Type::Multiple;
    } else if (activity.actions.size() == 1) {
        activity.type = activity.actions.front().direction == TransactionAction::Direction::Send ? Type::Send : Type::Receive;
    }
    return activity;
}
