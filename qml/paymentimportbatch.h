// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PAYMENTIMPORTBATCH_H
#define BITCOIN_QML_PAYMENTIMPORTBATCH_H

#include <qml/senddraft.h>
#include <key_io.h>

#include <cstdint>
#include <optional>
#include <utility>

namespace paymentimport {
enum class SourceField { WholeRequest, Address, Amount, PrivateNote };
enum class BatchStatus { Prepared, Invalid, Missing, Stale, ReplacementRequired };
struct PaymentBatch {
    uint64_t recipient{0}, expected_revision{0}, expected_amount_intent{0};
    senddraft::PaymentRequest payment;
    SourceField source_field{SourceField::WholeRequest};
    bool replace_existing{false};
};
struct BatchResult {
    BatchStatus status{BatchStatus::Invalid};
    std::optional<senddraft::Recipient> recipient;
    bool amount_changed{false}, private_note_changed{false};
};

/** Public request equality is independent of a user's private note and URI
 * formatting. Missing label/message clear that metadata when applied; an
 * omitted amount preserves the existing explicit Fixed/Maximum intent. */
inline bool MatchesPaymentRequest(const senddraft::Recipient& current, const senddraft::PaymentRequest& request)
{
    const auto destination = DecodeDestination(request.address.toStdString());
    return IsValidDestination(destination) && destination == DecodeDestination(current.address.toStdString()) &&
        (!request.amount || (current.mode == senddraft::AmountMode::Fixed && current.amount_set && current.amount == *request.amount)) &&
        current.request_label == request.label && current.message == request.message;
}

/** Prepare the complete revisioned input batch on Domain. The target actor
 * guards its opening/draft revision and commits this value as one ledger entry;
 * it then runs ordinary recipient validation and applies the existing explicit
 * Fixed/Maximum command where amount_changed is true. GUI projects the result.
 * No absent URI field can silently replace a private note or Maximum intent. */
inline BatchResult PreparePaymentBatch(const senddraft::Recipient& current, const PaymentBatch& batch)
{
    if (!batch.recipient || current.token != batch.recipient) return {BatchStatus::Missing, {}, false, false};
    if (current.revision != batch.expected_revision || current.amount_intent_revision != batch.expected_amount_intent)
        return {BatchStatus::Stale, {}, false, false};
    const auto& request = batch.payment;
    if (request.address.isEmpty() || request.address.size() > 1024 ||
        request.label.size() > senddraft::Limits::note || request.message.size() > senddraft::Limits::message ||
        request.private_note.size() > senddraft::Limits::note || (request.amount && !MoneyRange(*request.amount)) ||
        !IsValidDestination(DecodeDestination(request.address.toStdString())))
        return {BatchStatus::Invalid, {}, false, false};
    const bool maximum_replaced = request.amount && current.mode == senddraft::AmountMode::Maximum;
    const bool conflicts = maximum_replaced ||
        (batch.source_field != SourceField::Address && !current.address.isEmpty() && current.address != request.address) ||
        (batch.source_field != SourceField::Amount && request.amount && current.amount_set && current.amount != *request.amount) ||
        (batch.source_field != SourceField::PrivateNote && !current.label.isEmpty() &&
            !request.private_note.isEmpty() && current.label != request.private_note);
    if (conflicts && !batch.replace_existing) return {BatchStatus::ReplacementRequired, {}, false, false};

    auto value = current;
    value.address = request.address; value.request_address = request.address;
    value.request_label = request.label; value.message = request.message; value.payment_request = true;
    const bool note_changed = !request.private_note.isEmpty() && (current.label.isEmpty() ||
        (batch.replace_existing && request.replace_private_note));
    if (note_changed) value.label = request.private_note;
    if (request.amount) {
        value.amount = *request.amount; value.amount_set = true; value.clear_amount = false;
        value.mode = senddraft::AmountMode::Fixed;
    }
    // The old validation belongs to another input tuple. Preserve omitted raw
    // amount intent, but never inherit its permission to prepare or submit.
    value.valid = false; value.address_valid = false; value.validation_pending = true;
    value.destination = CNoDestination{}; value.address_error.clear(); value.amount_error.clear();
    return {BatchStatus::Prepared, std::move(value), bool(request.amount), note_changed};
}
} // namespace paymentimport
#endif // BITCOIN_QML_PAYMENTIMPORTBATCH_H
