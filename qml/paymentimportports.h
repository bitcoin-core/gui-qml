// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PAYMENTIMPORTPORTS_H
#define BITCOIN_QML_PAYMENTIMPORTPORTS_H

#include <qml/execution.h>
#include <qml/paymentimporttarget.h>
#include <qml/psbtworktypes.h>

#include <exception>
#include <atomic>
#include <functional>
#include <memory>
#include <optional>

namespace psbtwork { struct ImportResult; }
namespace paymentimport {
struct Inspection {
    Kind kind{Kind::Unknown};
    senddraft::PaymentRequest payment;
    SourceField field{SourceField::WholeRequest};
    std::shared_ptr<const psbtwork::Snapshot> psbt;
    QString error, source_identity;
    QString payment_identity;
    // Source view lifetime only. Revocation never changes an admitted child
    // effect or its retained outcome.
    std::shared_ptr<std::atomic_bool> presentation_allowed{};
};
struct ChildReply {
    Receipt receipt;
    std::optional<Target> current_target;
    std::shared_ptr<const ApplyReceipt> applied;
    std::shared_ptr<const psbtwork::ImportResult> review;
    std::shared_ptr<std::atomic_bool> presentation_allowed{};
};

/** An immutable route to one original opening/draft/recipient. Implementations
 * schedule on that target's Domain/Native lanes. They never resolve a selected
 * wallet, consult a QObject, or repeat a mutation while answering a query. */
struct TargetPort {
    walletopening::Opening opening;
    uint64_t draft{0}, recipient{0};
    using Failed = std::function<void(std::exception_ptr)>;
    using Observed = std::function<void(Target)>;
    using Completed = std::function<void(ChildReply)>;
    std::function<execution::SubmitResult(std::shared_ptr<const Inspection>, Observed, Failed)> observe;
    std::function<execution::SubmitResult(CommandKey, Child, std::shared_ptr<const Inspection>, Completed, Failed)> dispatch;
    std::function<execution::SubmitResult(CommandKey, Child, Completed, Failed)> query;
    std::function<execution::SubmitResult(CommandKey, Child)> acknowledge;
    // Revoke unused presentation only; an accepted local edit is not rolled back.
    std::function<void(CommandKey, Child)> retire;
};
} // namespace paymentimport
#endif // BITCOIN_QML_PAYMENTIMPORTPORTS_H
