// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETBUMP_H
#define BITCOIN_QML_WALLETBUMP_H

#include <interfaces/wallet.h>
#include <qml/secretinput.h>
#include <qml/psbtpolicy.h>
#include <primitives/transaction.h>
#include <support/allocators/secure.h>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

namespace walletbump {
struct Prepared {
    Txid original;
    std::vector<COutPoint> original_inputs;
    CMutableTransaction transaction;
    std::vector<COutPoint> approved_inputs;
    std::vector<COutPoint> added_inputs;
    CAmount old_fee{0}, new_fee{0};
    unsigned int target_blocks{0};
    QString old_txid;
};
enum class Error { None, Invalid, InvalidId, Ineligible, Unavailable, Create, NeedsUnlock, IncorrectPassphrase, Sign, Changed, InvalidSignature, Policy, RelockFailed, Commit, Uncertain };
enum class Record { Unknown, AbsentAtObservation, Recorded };
enum class Submission { Unknown, Mempool, Confirmed, NotInMempool, Conflicted, Abandoned };
enum class Relay { Unknown };
struct Preparation { Error error{Error::None}; QString message; std::shared_ptr<const Prepared> review; };
struct Commit {
    Error error{Error::None};
    QString message;
    bool committed{false}, relock_failed{false};
    Record record{Record::Unknown};
    Submission submission{Submission::Unknown};
    Relay relay{Relay::Unknown};
    Txid original, txid;
    // Captured before invoking commit. A false return or exception can follow
    // partial effects; rechecking these identities never repeats the write.
    Txid candidate_txid;
    Wtxid candidate_wtxid;
    bool commit_attempted{false};
};
struct Services {
    // Advisory exact-candidate admission observation. Unknown does not replace
    // the authoritative wallet commit check.
    psbtpolicy::Read admission;
};
Preparation Prepare(interfaces::Wallet& wallet, const QString& txid, unsigned int target,
                    std::shared_ptr<Prepared> review);
Commit Execute(interfaces::Wallet& wallet, const Prepared& review, std::optional<SecureString> passphrase,
               secretinput::Captured captured = {}, const Services& services = {});
Commit ObserveOutcome(interfaces::Wallet& wallet, const Commit& original);
} // namespace walletbump

#endif // BITCOIN_QML_WALLETBUMP_H
