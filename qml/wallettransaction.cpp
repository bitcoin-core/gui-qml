// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallettransaction.h>

#include <util/translation.h>
#include <wallet/wallet.h> // Public CRecipient value required by interfaces::Wallet.

#include <set>

namespace wallettransaction {
namespace {
std::optional<CAmount> SelectedValue(interfaces::Wallet& wallet, const std::vector<COutPoint>& inputs)
{
    // The pinned public getCoins implementation indexes known transactions
    // without checking vout bounds. Preflight immutable transactions through
    // the public interface; missing/stale identifiers remain unknown.
    for (const auto& input : inputs) {
        const auto previous = wallet.getTx(input.hash);
        if (!previous || input.n >= previous->vout.size()) return {};
    }
    const auto outputs = wallet.getCoins(inputs);
    if (outputs.size() != inputs.size()) return {};
    CAmount total{0};
    for (const auto& output : outputs) {
        if (output.is_spent || output.depth_in_main_chain < 0 || !MoneyRange(output.txout.nValue) ||
            !wallet.txoutIsMine(output.txout) || !MoneyRange(total + output.txout.nValue)) return {};
        total += output.txout.nValue;
    }
    return total;
}
} // namespace

util::Result<wallet::CreatedTransactionResult> Create(interfaces::Wallet& wallet,
    const std::vector<wallet::CRecipient>& requested, const wallet::CCoinControl& original,
    int remainder, bool sign)
{
    if (remainder < 0) return wallet.createTransaction(requested, original, sign, std::nullopt);
    if (static_cast<size_t>(remainder) >= requested.size()) return util::Error{Untranslated("The maximum recipient is missing.")};
    // Preserve the public wallet's candidate policy. Turning listCoins rows
    // into presets would bypass automatic safety/maturity/reuse eligibility.
    if (original.HasSelected() && !SelectedValue(wallet, original.ListSelected()))
        return util::Error{Untranslated("The selected inputs could not be confirmed. Refresh the wallet and try again.")};
    const auto total = wallet.getAvailableBalance(original);
    if (!MoneyRange(total) || total <= 0) return util::Error{Untranslated("No available inputs to use maximum.")};
    auto recipients = requested;
    CAmount fixed{0};
    std::set<CTxDestination> destinations;
    for (size_t i = 0; i < recipients.size(); ++i) {
        if (!destinations.insert(recipients[i].dest).second) return util::Error{Untranslated("Each recipient must have a different address when using maximum.")};
        if (static_cast<int>(i) != remainder) {
            if (!MoneyRange(recipients[i].nAmount) || !MoneyRange(fixed + recipients[i].nAmount)) return util::Error{Untranslated("Invalid recipient amount.")};
            fixed += recipients[i].nAmount;
        }
        recipients[i].fSubtractFeeFromAmount = static_cast<int>(i) == remainder;
    }
    if (fixed >= total) return util::Error{Untranslated("Selected inputs do not cover the amount plus fee")};
    recipients[remainder].nAmount = total - fixed;
    // The full public gross balance is allocated before fee subtraction.
    // Core chooses automatic inputs with the ORIGINAL control, or honors the
    // user's explicit presets. It still owns fees, policy, dust and signing.
    auto result = wallet.createTransaction(recipients, original, sign, std::nullopt);
    if (result && result->change_pos) return util::Error{Untranslated("The maximum transaction unexpectedly returned change.")};
    return result;
}

std::optional<bool> Sweeps(interfaces::Wallet& wallet, const CTransaction& transaction, const wallet::CCoinControl& control)
{
    try {
        for (const auto& output : transaction.vout) if (wallet.txoutIsMine(output)) return false;
        if (transaction.vin.empty()) return {};
        std::set<COutPoint> unique;
        for (const auto& input : transaction.vin) if (!unique.insert(input.prevout).second) return {};
        const std::vector<COutPoint> spent(unique.begin(), unique.end());
        const auto selected = SelectedValue(wallet, spent);
        if (!selected || *selected <= 0) return {};
        auto probe = control;
        probe.UnSelectAll();
        for (const auto& input : spent) probe.Select(input);
        // The public balance getter swallows FetchSelectedInputs failures. Verify
        // the observed presets first, so a persistent preset failure plus an equal
        // leftover balance cannot masquerade as a full sweep.
        probe.m_allow_other_inputs = false;
        if (wallet.getAvailableBalance(probe) != *selected) return {};
        probe.m_allow_other_inputs = true;
        const auto available = wallet.getAvailableBalance(probe);
        // AvailableCoins excludes presets and supplies the same original policy
        // for other coins. Only no remaining positive eligible value is a sweep.
        if (!MoneyRange(available) || available < *selected) return {};
        return available == *selected;
    } catch (...) {
        return {}; // A failed public observation is unknown, never a safe negative.
    }
}

std::optional<wallet::CreatedTransactionResult> InsufficientFeeProbe(interfaces::Wallet& wallet,
    const std::vector<wallet::CRecipient>& requested, const wallet::CCoinControl& control)
{
    if (!control.HasSelected() || control.m_allow_other_inputs || requested.empty()) return {};
    const auto total = SelectedValue(wallet, control.ListSelected());
    if (!total || *total <= 0) return {};
    CAmount wanted{0};
    for (const auto& recipient : requested) {
        if (!MoneyRange(recipient.nAmount) || !MoneyRange(wanted + recipient.nAmount)) return {};
        wanted += recipient.nAmount;
    }
    // Probe the same scripts/input set with affordable output values. Values
    // occupy fixed-width fields; Core still supplies the actual fee. If this
    // document cannot be constructed, the estimate stays explicitly unknown.
    auto recipients = requested;
    const CAmount each = *total / recipients.size();
    if (each <= 0) return {};
    for (auto& recipient : recipients) { recipient.nAmount = each; recipient.fSubtractFeeFromAmount = false; }
    recipients.front().nAmount += *total - each * recipients.size();
    recipients.front().fSubtractFeeFromAmount = true;
    const auto result = wallet.createTransaction(recipients, control, false, std::nullopt);
    if (!result || result->change_pos || !MoneyRange(result->fee) ||
        (wanted <= *total && result->fee <= *total - wanted)) return {};
    return *result;
}
} // namespace wallettransaction
