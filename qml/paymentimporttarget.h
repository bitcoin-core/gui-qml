// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PAYMENTIMPORTTARGET_H
#define BITCOIN_QML_PAYMENTIMPORTTARGET_H

#include <qml/paymentimportbatch.h>
#include <qml/paymentimportflow.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <utility>

namespace paymentimport {
/** The target retains an acknowledgment until its original import owner has
 * accounted for it. A query never repeats a mutation. */
struct CommandKey {
    uint64_t owner{0}, child{0};
    auto operator<=>(const CommandKey&) const = default;
};
struct ApplyReceipt {
    Receipt receipt;
    std::optional<senddraft::Recipient> projection;
    bool amount_changed{false}, private_note_changed{false};
};

/** Domain-confined local target ledger. The adapter serializes this with draft
 * input commands, guards the original opening and reserves Native cleanup
 * before dispatch. Receipts are never evicted while their owner might still
 * need to reconcile a lost acknowledgment. */
class DraftTarget
{
public:
    std::optional<ApplyReceipt> query(CommandKey key) const {
        const auto found = m_results.find(key);
        return found == m_results.end() ? std::nullopt : std::optional{found->second};
    }

    std::optional<ApplyReceipt> apply(CommandKey key, Child child, PaymentBatch batch,
                                      const Target& current, senddraft::Draft& draft) {
        if (const auto previous = query(key)) return previous;
        if (!key.owner || key.child != child.id || !child.id || !child.source)
            return {};
        ApplyReceipt result{Receipt{child, Outcome::Rejected, 0, current.revision}, {}};
        if (child.kind != ChildKind::Apply || !current.available || !current.compatible ||
            child.target.opening != current.opening || child.target.draft != current.draft ||
            child.target.recipient != current.recipient || batch.recipient != current.recipient)
            return remember(key, std::move(result));
        if (child.target.revision != current.revision) {
            result.receipt.outcome = Outcome::Stale;
            return remember(key, std::move(result));
        }
        const auto found = draft.recipients().find(current.recipient);
        if (found == draft.recipients().end() || current.revision == std::numeric_limits<uint64_t>::max())
            return remember(key, std::move(result));

        // Validation can settle without changing intent. Use the current
        // tuple only after checking the entire draft's semantic revision.
        batch.expected_revision = found->second.revision;
        batch.expected_amount_intent = found->second.amount_intent_revision;
        batch.replace_existing = child.replace_existing;
        auto prepared = PreparePaymentBatch(found->second, batch);
        if (prepared.status != BatchStatus::Prepared || !prepared.recipient)
            return remember(key, std::move(result));
        if (prepared.amount_changed && prepared.recipient->amount_intent_revision == std::numeric_limits<uint64_t>::max())
            return remember(key, std::move(result));

        // Stage all changes and the receipt before touching the live draft.
        // Allocation or validation failure cannot install half a URI.
        auto replacement = draft;
        auto value = std::move(*prepared.recipient);
        if (prepared.amount_changed) ++value.amount_intent_revision;
        if (!replacement.update(value)) return remember(key, std::move(result));
        if (prepared.amount_changed && !replacement.choose(value.token, senddraft::AmountMode::Fixed,
                                                           value.amount_intent_revision, {}))
            return remember(key, std::move(result));
        result.projection = replacement.recipients().at(value.token);
        result.amount_changed = prepared.amount_changed;
        result.private_note_changed = prepared.private_note_changed;
        result.receipt.outcome = Outcome::Applied;
        result.receipt.result = current.revision + 1;
        result.receipt.revision = current.revision + 1;
        auto receipt = remember(key, std::move(result));
        draft = std::move(replacement);
        return receipt;
    }

    bool acknowledge(CommandKey key, const Child& child) {
        const auto found = m_results.find(key);
        if (found == m_results.end() || found->second.receipt.child != child) return false;
        m_results.erase(found);
        return true;
    }
    size_t retained() const { return m_results.size(); }

private:
    ApplyReceipt remember(CommandKey key, ApplyReceipt result) {
        return m_results.emplace(key, std::move(result)).first->second;
    }
    std::map<CommandKey, ApplyReceipt> m_results;
};
} // namespace paymentimport
#endif // BITCOIN_QML_PAYMENTIMPORTTARGET_H
