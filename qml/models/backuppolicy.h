// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BACKUPPOLICY_H
#define BITCOIN_QML_MODELS_BACKUPPOLICY_H

#include <cstdint>
#include <string>

namespace backup {
enum class Phase { Queued, Inspecting, AwaitingConfirmation, Ready, Writing, Succeeded, Failed, Declined, Retired };
enum class Error { None, InvalidPath, Inspection, UnsupportedTarget, Staging, CoreBackup, Changed, Install, Unknown, Transport };
enum class Event { Start, Inspected, Approve, Refuse, AdmitWrite, Written, Retire, Fault };
enum class Effect { None, Inspect, Write };

struct Proposal {
    std::string path;
    std::string version;
    bool exists{false};
    bool renamed{false};
    Error error{Error::None};
};
struct WriteResult {
    Error error{Error::None};
};
struct Snapshot {
    uint64_t sequence{0};
    Phase phase{Phase::Queued};
    Proposal target;
    Error error{Error::None};
    bool destination_approved{false};
};
struct Decision {
    Snapshot snapshot;
    Effect effect{Effect::None};
    bool accepted{false};
};
bool Pending(Phase phase);
Decision Advance(const Snapshot& state, Event event, const Proposal& proposal = {}, WriteResult result = {});
} // namespace backup

#endif // BITCOIN_QML_MODELS_BACKUPPOLICY_H
