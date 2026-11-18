// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PSBTFEESERVICES_H
#define BITCOIN_QML_MODELS_PSBTFEESERVICES_H

#include <qml/execution.h>
#include <qml/models/psbtfee.h>
#include <qml/psbtpolicy.h>

#include <functional>
#include <memory>

namespace interfaces { class Node; }
namespace psbtwork { struct Snapshot; }
namespace psbtfee {
struct RequestIdentity {};
using Lookup = std::function<std::optional<CTxOut>(const COutPoint&)>;
struct Services {
    std::shared_ptr<execution::Scheduler> scheduler;
    Lookup lookup;
    std::function<void(std::function<void()>)> post;
    psbtpolicy::Read policy{};
};
struct Resolution {
    std::shared_ptr<const Analysis> proof;
    bool complete{false};
    bool failed{false};
    std::shared_ptr<const psbtwork::Snapshot> analysis{};
    CTransactionRef transaction{};
    QString node_policy_status{};
};
/** Node must retain its application lifetime until the owning scheduler drains. */
Lookup NodeLookup(interfaces::Node* node);
Resolution Resolve(const PartiallySignedTransaction& document, const Analysis& seed, const Lookup& lookup,
                   const psbtpolicy::Read& policy = {});
execution::Admission Request(const Services& services, std::shared_ptr<const PartiallySignedTransaction> document,
                             std::shared_ptr<const Analysis> seed, std::function<void(Resolution)> publish);
} // namespace psbtfee
#endif // BITCOIN_QML_MODELS_PSBTFEESERVICES_H
