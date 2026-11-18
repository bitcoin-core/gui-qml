// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/psbtdocument.h>
#include <qml/psbtsignatures.h>

#include <QCoreApplication>

namespace {
QString Tr(const char* text, const char* comment = nullptr, int n = -1)
{
    return QCoreApplication::translate("PsbtQmlModel", text, comment, n);
}
} // namespace

void psbtdocument::Qualify(psbtwork::Snapshot& snapshot)
{
    const auto transaction = snapshot.document ? snapshot.document->GetUnsignedTx() : std::nullopt;
    const bool structure = transaction && !transaction->vin.empty() && !transaction->vout.empty()
        && snapshot.document->inputs.size() == transaction->vin.size()
        && snapshot.document->outputs.size() == transaction->vout.size();
    const auto& assessed = snapshot.assessment ? snapshot.assessment : snapshot.document;
    const bool amounts_verified = structure && snapshot.proof
        && psbtfee::MatchesReview(*snapshot.document, *snapshot.proof, snapshot.proof->amount)
        && assessed && psbtfee::MatchesReview(*assessed, *snapshot.proof, snapshot.proof->amount);
    if (amounts_verified && !psbtsignatures::VerifyFinalizedInputs(*assessed, snapshot.complete)) {
        snapshot.complete = false;
        snapshot.signature_state = psbtwork::SignatureState::Invalid;
        snapshot.metadata_failed = true;
        snapshot.error = Tr("The PSBT contains invalid signatures.");
    } else snapshot.signature_state = !amounts_verified ? psbtwork::SignatureState::Unverified
        : snapshot.complete ? psbtwork::SignatureState::Verified : psbtwork::SignatureState::Incomplete;
    const bool authorized = amounts_verified && !snapshot.metadata_failed;
    snapshot.can_sign = authorized && !snapshot.complete && snapshot.facts.signer_available && snapshot.could_sign > 0;
    const auto node = psbtpolicy::Evaluate(snapshot.node_facts);
    snapshot.node_state = node.state;
    snapshot.can_broadcast = authorized && snapshot.signature_state == psbtwork::SignatureState::Verified
        && node.state != psbtpolicy::State::Blocked;
    if (!authorized) {
        if (!amounts_verified && snapshot.error.isEmpty()) snapshot.error = Tr("The transaction fee is missing or invalid.");
        snapshot.status = snapshot.error;
    } else if (!snapshot.facts.requested_status.isEmpty()) snapshot.status = snapshot.facts.requested_status;
    else if (snapshot.complete) snapshot.status = node.explanation;
    else if (!snapshot.facts.wallet_available) snapshot.status = Tr("No wallet is loaded. You can inspect, copy, or save this PSBT.");
    else if (!snapshot.facts.signer_available) snapshot.status = Tr("This wallet cannot sign transactions because private keys are disabled.");
    else if (snapshot.could_sign) snapshot.status = Tr("This wallet can sign %n input(s).", nullptr, snapshot.could_sign);
    else snapshot.status = Tr("This wallet does not have the right keys to sign this PSBT.");
}

psbtwork::Result psbtdocument::Owner::consume(std::uint64_t operation, psbtwork::Kind kind, psbtwork::Result result)
{
    if (m_withdrawn.load() || operation <= m_snapshot.operation) {
        // A completed effect keeps its scalar receipt. Structural data from a
        // retired generation is released here, without entering GUI delivery.
        result.snapshot.reset();
        result.quarantined_document.reset();
        return result;
    }
    m_snapshot.operation = operation;
    ++m_snapshot.sequence;
    if (result.snapshot) {
        auto analysis = std::make_shared<psbtwork::Snapshot>(*result.snapshot);
        Qualify(*analysis);
        m_snapshot.analysis = analysis;
        result.snapshot = std::move(analysis);
    } else if (kind == psbtwork::Kind::Load) {
        m_snapshot.analysis.reset();
    }
    m_snapshot.phase = result.code != psbtwork::Error::None ? Phase::Failed
        : m_snapshot.analysis ? Phase::Ready : Phase::Empty;
    return result;
}
