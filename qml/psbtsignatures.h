// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PSBTSIGNATURES_H
#define BITCOIN_QML_PSBTSIGNATURES_H

class PartiallySignedTransaction;
namespace psbtsignatures {
enum class Status { Unknown, Valid, Invalid };
// Verify against supplied prevouts. Callers establish their authenticity before
// treating these signatures as authority for an immutable transaction review.
bool VerifyFinalizedInputs(const PartiallySignedTransaction& document, bool require_all);
// Unknown means required script/execution context is unavailable. It never
// grants permission to install signatures or submit a transaction.
Status VerifyPartialInputs(const PartiallySignedTransaction& document);
// Existing imported fields are preserved without acquiring new authority.
// Validate additions/replacements while always checking finalized scripts.
Status VerifyContributions(const PartiallySignedTransaction& original, const PartiallySignedTransaction& returned);
} // namespace psbtsignatures

#endif // BITCOIN_QML_PSBTSIGNATURES_H
