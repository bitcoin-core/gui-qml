// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PSBTWORKTYPES_H
#define BITCOIN_QML_PSBTWORKTYPES_H

#include <qml/models/psbtfee.h>
#include <qml/psbtpolicy.h>
#include <qml/psbtsignatures.h>
#include <QBitArray>
#include <QString>
#include <QStringList>
#include <memory>

// Immutable request/result data shared by the Domain owner and Native ports.
// The Native effect API stays in psbtwork.h and does not enter the owner.
namespace psbtwork {
enum class Kind { Inspect, Load, Sign, Broadcast, Clipboard };
enum class Error { None, Invalid, Unavailable, NeedsUnlock, IncorrectPassphrase, Changed, Fee, Core, Uncertain, Relock, UnverifiedSignatures };
struct InspectionFacts {
    bool wallet_available{false}, signer_available{false};
    QString requested_status;
};
enum class SignatureState { Unverified, Incomplete, Invalid, Verified };
struct SummarySource {
    enum class Kind { Transaction, MissingTransaction, InvalidFee };
    Kind kind{Kind::Transaction};
    std::shared_ptr<const PartiallySignedTransaction> assessment;
    std::shared_ptr<const psbtfee::Analysis> proof;
    // Ownership is captured on Native with the exact inspected document. A
    // bit per output avoids materializing a GUI-sized list of formatted rows.
    QBitArray owned_outputs;
    QString analysis_error;
    int unsigned_inputs{0};
    int row_count{0};
};
struct SummaryPage {
    quint64 generation{0}, request{0};
    int offset{0}, total{0};
    QStringList rows;
    QString error;
};
struct Snapshot {
    std::shared_ptr<const PartiallySignedTransaction> document;
    // Metadata/finalization belongs to analysis of this exact document. Copy
    // and export retain document; analysis never replaces the imported bytes.
    std::shared_ptr<const PartiallySignedTransaction> assessment;
    std::shared_ptr<const psbtfee::Analysis> proof;
    QString status, error;
    std::shared_ptr<const SummarySource> summary_source;
    SummaryPage initial_summary_page;
    int summary_count{0};
    // complete preserves finalized-field compatibility, excluding known-invalid
    // signatures. Only signature_state establishes verified completion.
    bool can_sign{false}, can_broadcast{false}, complete{false};
    bool metadata_failed{false};
    int unsigned_inputs{0}, could_sign{0};
    InspectionFacts facts;
    SignatureState signature_state{SignatureState::Unverified};
    psbtpolicy::Facts node_facts;
    psbtpolicy::State node_state{psbtpolicy::State::Unknown};
};
struct Result {
    Error code{Error::None};
    QString error, path, txid, clipboard;
    CTransactionRef transaction;
    std::shared_ptr<const Snapshot> snapshot;
    std::shared_ptr<const PartiallySignedTransaction> quarantined_document;
    psbtsignatures::Status signature_status{psbtsignatures::Status::Unknown};
    bool submitted{false}, relock_failed{false};
};
} // namespace psbtwork

#endif // BITCOIN_QML_PSBTWORKTYPES_H
