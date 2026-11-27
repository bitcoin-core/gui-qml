// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PSBTFEESERVICES_H
#define BITCOIN_QML_MODELS_PSBTFEESERVICES_H

#include <qml/execution.h>
#include <qml/models/psbtfee.h>
#include <qml/psbtpolicy.h>
#include <qml/retainedvalue.h>
#include <qml/snapshotquery.h>

#include <functional>
#include <memory>

namespace execution { class SerialQueue; }
namespace interfaces { class Node; }
namespace psbtwork { struct Snapshot; }
namespace walletopening { class Session; }
namespace psbtfee {
using Lookup = std::function<std::optional<CTxOut>(const COutPoint&)>;
struct Services {
    Lookup lookup;
    std::function<void(std::function<void()>)> post;
    psbtpolicy::Read policy{};
    std::shared_ptr<execution::SerialQueue> core{}, utility{};
    execution::ValueCleanup value_cleanup{};
    psbtpolicy::Read wallet_policy{};
};
struct Resolution {
    std::shared_ptr<const Analysis> proof;
    bool complete{false};
    bool failed{false};
    std::shared_ptr<const psbtwork::Snapshot> analysis{};
    CTransactionRef transaction{};
    QString node_policy_status{};
};
using FeeRequest = execution::OwnedCommand<Resolution>;

/** Node must retain its application lifetime until the owning queues drain. */
Lookup NodeLookup(interfaces::Node* node);
Resolution Resolve(const PartiallySignedTransaction& document, const Analysis& seed, const Lookup& lookup,
                   const psbtpolicy::Read& policy = {});
execution::SubmitResult Request(const Services& services, std::shared_ptr<const PartiallySignedTransaction> document,
                             std::shared_ptr<const Analysis> seed, const std::shared_ptr<FeeRequest>& request,
                             const std::shared_ptr<walletopening::Session>& opening = {});
} // namespace psbtfee
#endif // BITCOIN_QML_MODELS_PSBTFEESERVICES_H
