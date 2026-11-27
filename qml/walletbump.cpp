// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletbump.h>
#include <qml/walletsend.h>
#include <qml/models/signertransaction.h>
#include <wallet/coincontrol.h>
#include <util/translation.h>
#include <QObject>
#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace walletbump {
namespace {
QString Message(const std::vector<bilingual_str>& errors)
{
    if (errors.empty()) return {};
    return QString::fromStdString(errors.front().translated.empty() ? errors.front().original : errors.front().translated).left(4096);
}
std::vector<COutPoint> Inputs(const CMutableTransaction& transaction)
{
    std::vector<COutPoint> inputs;
    inputs.reserve(transaction.vin.size());
    for (const auto& input : transaction.vin) inputs.push_back(input.prevout);
    return inputs;
}
std::vector<COutPoint> Inputs(const CTransaction& transaction)
{
    std::vector<COutPoint> inputs;
    inputs.reserve(transaction.vin.size());
    for (const auto& input : transaction.vin) inputs.push_back(input.prevout);
    return inputs;
}
Error CheckApprovedScope(interfaces::Wallet& wallet, const Prepared& review)
{
    if (!wallet.transactionCanBeBumped(review.original)) return Error::Ineligible;
    interfaces::WalletTxStatus status{};
    int blocks{0}; int64_t block_time{0};
    // A busy wallet is not a missing transaction. Waiting is safe on Core.
    if (!wallet.tryGetTxStatus(review.original, status, blocks, block_time)) {
        std::vector<std::string> messages, payment_requests;
        bool in_mempool{false};
        if (!wallet.getWalletTxDetails(review.original, status, messages, payment_requests, in_mempool, blocks).tx) return Error::Unavailable;
    }
    if (status.is_abandoned || status.depth_in_main_chain != 0) return Error::Ineligible;
    const auto original = wallet.getTx(review.original);
    if (!original || original->GetHash() != review.original
        || Inputs(*original) != review.original_inputs) return Error::Ineligible;

    const auto coins = wallet.getCoins(review.added_inputs);
    if (coins.size() != review.added_inputs.size()) return Error::Uncertain;
    for (const auto& coin : coins) {
        // Original inputs are intentionally already spent by the transaction
        // being replaced; only the newly introduced inputs are checked here.
        // Core's bump builder requires new inputs to be confirmed, so preserve
        // that public policy while rechecking spendability at commit time.
        if (coin.is_spent || coin.depth_in_main_chain < 1) return Error::Ineligible;
    }
    return Error::None;
}
void Observe(interfaces::Wallet& wallet, Commit& result, bool exact_witness = false)
{
    try {
        interfaces::WalletTxStatus status{}; bool in_mempool{false}; int num_blocks{0};
        std::vector<std::string> messages, payment_requests;
        const auto record = wallet.getWalletTxDetails(result.txid, status, messages, payment_requests, in_mempool, num_blocks);
        if (!record.tx || record.tx->GetHash() != result.txid) {
            result.record = Record::AbsentAtObservation;
            return;
        }
        if (exact_witness && record.tx->GetWitnessHash() != result.candidate_wtxid) return;
        result.record = Record::Recorded;
        if (status.depth_in_main_chain > 0) result.submission = Submission::Confirmed;
        else if (status.depth_in_main_chain < 0) result.submission = Submission::Conflicted;
        else if (status.is_abandoned) result.submission = Submission::Abandoned;
        else result.submission = in_mempool ? Submission::Mempool : Submission::NotInMempool;
    } catch (...) {
        result.record = Record::Unknown;
        result.submission = Submission::Unknown;
    }
}
} // namespace

Preparation Prepare(interfaces::Wallet& wallet, const QString& txid, unsigned int target,
                    std::shared_ptr<Prepared> review)
{
    Preparation result;
    try {
        const auto parsed = txid.size() == 64 ? uint256::FromHex(txid.toStdString()) : std::nullopt;
        if (!parsed) { result.error = Error::InvalidId; return result; }
        if (target < 1 || target > 1008) { result.error = Error::Invalid; return result; }
        const auto original = Txid::FromUint256(*parsed);
        if (!wallet.transactionCanBeBumped(original)) { result.error = Error::Ineligible; return result; }
        if (!review) { result.error = Error::Uncertain; return result; }
        review->original = original;
        review->target_blocks = target;
        const auto original_transaction = wallet.getTx(original);
        if (!original_transaction || original_transaction->GetHash().ToUint256() != *parsed) {
            result.error = Error::Ineligible; return result;
        }
        review->original_inputs = Inputs(*original_transaction);
        wallet::CCoinControl control;
        control.m_signal_bip125_rbf = true;
        control.m_confirm_target = target;
        // Preserve the original transaction's absolute and relative timing
        // through the public builder controls, before capturing a new review.
        control.m_version = original_transaction->version;
        control.m_locktime = original_transaction->nLockTime;
        for (const auto& input : original_transaction->vin)
            control.Select(input.prevout).SetSequence(input.nSequence);
        std::vector<bilingual_str> errors;
        if (!wallet.createBumpTransaction(original, control, errors, review->old_fee, review->new_fee, review->transaction)) {
            result.error = Error::Create; result.message = Message(errors); return result;
        }
        review->approved_inputs = Inputs(review->transaction);
        const std::set<COutPoint> original_inputs(review->original_inputs.begin(), review->original_inputs.end());
        const std::set<COutPoint> approved_inputs(review->approved_inputs.begin(), review->approved_inputs.end());
        if (original_inputs.size() != review->original_inputs.size()
            || approved_inputs.size() != review->approved_inputs.size()
            || !std::includes(approved_inputs.begin(), approved_inputs.end(), original_inputs.begin(), original_inputs.end())) {
            result.error = Error::Changed; return result;
        }
        for (const auto& input : review->approved_inputs) {
            if (!original_inputs.contains(input)) review->added_inputs.push_back(input);
        }
        if (!MoneyRange(review->old_fee) || !MoneyRange(review->new_fee) || review->new_fee < review->old_fee) {
            result.error = Error::Invalid; return result;
        }
        if (const auto scope_error = CheckApprovedScope(wallet, *review); scope_error != Error::None) {
            result.error = scope_error; return result;
        }
        review->old_txid = QString::fromStdString(original.GetHex());
        result.review = std::move(review);
    } catch (...) { result.error = Error::Uncertain; }
    return result;
}

Commit Execute(interfaces::Wallet& wallet, const Prepared& review, std::optional<SecureString> passphrase,
               secretinput::Captured captured, const Services& services)
{
    Commit result;
    result.original = review.original;
    bool relock_required{false};
    const auto restoreWalletLock = [&] {
        if (!std::exchange(relock_required, false)) return;
        try {
            result.relock_failed = !wallet.lock();
        } catch (...) {
            result.relock_failed = true;
        }
        if (result.relock_failed && !result.committed) result.error = Error::RelockFailed;
    };
    const auto finishAfterRelock = [&] {
        restoreWalletLock();
        passphrase.reset();
        return result;
    };
    try {
        if (captured) passphrase = secretinput::Materialize(captured);
        captured.reset();
        if (const auto scope_error = CheckApprovedScope(wallet, review); scope_error != Error::None) {
            result.error = scope_error; return finishAfterRelock();
        }
        if (!wallet.privateKeysDisabled() && wallet.isCrypted() && wallet.isLocked()) {
            if (!passphrase) { result.error = Error::NeedsUnlock; return finishAfterRelock(); }
            relock_required = true;
            if (!wallet.unlock(*passphrase)) { relock_required = false; result.error = Error::IncorrectPassphrase; return finishAfterRelock(); }
        }
        CMutableTransaction signed_tx{review.transaction};
        // An external signer helper failure throws before any commit. It is a
        // definite signing failure, not an uncertain replacement outcome.
        bool signed_replacement{false};
        try { signed_replacement = wallet.signBumpTransaction(signed_tx); } catch (...) {}
        if (!signed_replacement) { result.error = Error::Sign; return finishAfterRelock(); }
        if (!MatchesUnsignedTransaction(review.transaction, signed_tx)) { result.error = Error::Changed; return finishAfterRelock(); }
        if (wallet.hasExternalSigner() && !walletsend::VerifyExternalTransaction(wallet, CTransaction{signed_tx})) {
            result.error = Error::InvalidSignature; return finishAfterRelock();
        }
        if (const auto scope_error = CheckApprovedScope(wallet, review); scope_error != Error::None) {
            result.error = scope_error; return finishAfterRelock();
        }
        // No further key operation is needed. Restore authorization before
        // commit, and never publish a successful spend with failed restoration.
        restoreWalletLock(); passphrase.reset();
        if (result.relock_failed) return result;
        if (services.admission) {
            const auto exact = MakeTransactionRef(signed_tx);
            const auto decision = psbtpolicy::Evaluate(services.admission(exact, review.new_fee));
            if (decision.state == psbtpolicy::State::Blocked) {
                result.error = Error::Policy;
                result.message = decision.explanation;
                return result;
            }
        }
        std::vector<bilingual_str> errors;
        Txid txid;
        const CTransaction exact{signed_tx};
        result.candidate_txid = exact.GetHash();
        result.candidate_wtxid = exact.GetWitnessHash();
        result.commit_attempted = true;
        if (!wallet.commitBumpTransaction(review.original, std::move(signed_tx), errors, txid)) {
            result.error = Error::Commit; result.message = Message(errors); return result;
        }
        result.txid = txid;
        result.committed = true;
        Observe(wallet, result);
    } catch (const secretinput::Failure&) {
        result.error = Error::Invalid; result.message = QObject::tr("The wallet password could not be prepared. No fee bump was submitted.");
    } catch (...) { result.error = Error::Uncertain; }
    return finishAfterRelock();
}
Commit ObserveOutcome(interfaces::Wallet& wallet, const Commit& original)
{
    auto result = original;
    result.record = Record::Unknown;
    result.submission = Submission::Unknown;
    if (!original.commit_attempted || original.candidate_txid == Txid{}) return result;
    result.txid = original.candidate_txid;
    Observe(wallet, result, true);
    // Observing a record cannot manufacture the original call's missing
    // persistence/replacement-metadata receipt. Keep its uncertainty explicit.
    return result;
}
} // namespace walletbump
