// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_EFFECTIVESETTINGS_H
#define BITCOIN_QML_EFFECTIVESETTINGS_H

#include <QString>
#include <cstdint>
#include <map>
#include <optional>
#include <variant>
#include <vector>

class ArgsManager;
namespace interfaces { class Node; }

/** Worker/domain values. No QObject, settings handle or persistent write is
 * exposed to QML. A SaveAttempt supplies the original snapshot and whole batch. */
namespace effectivesettings {
enum class Key { Listen, Natpmp, Server, Prune, Proxy, Onion, Dbcache, Threads, Mempool, Signer };
enum class Kind { Boolean, Integer, Text };
enum class Provenance { Default, Configuration, Persistent, CommandLine, Startup };
enum class Applicability { Restart, Live };
enum class Intent { ExplicitOverride, RemoveOverride };
using Value = std::variant<bool, int64_t, QString>;
struct Field {
    Key key;
    Kind kind;
    Value committed, effective, next_start;
    Provenance provenance{Provenance::Default};
    Applicability applicability{Applicability::Restart};
    bool editable{true}, has_override{false};
    QString information{};
    std::optional<Value> remembered{};
};
struct Snapshot {
    QString revision;
    std::vector<Field> fields;
    bool ready{false};
    QString error;
    bool persistence_available{true};
};
struct Edit { Key key; Value value; Intent intent{Intent::ExplicitOverride}; std::optional<Value> remembered{}; };
struct Batch { QString base_revision; std::vector<Edit> edits; bool accept_redownload{false}; };
struct Constraints {
    bool binds{false}, listen_onion_explicit{false}, only_onion{false}, inbound_onion{false};
    bool indexes{false}, reindex_chainstate{false}, pruned_chain{false}, dns{true};
    int64_t minimum_mempool_mb{5};
    // Documented -proxy list grammar is validated on Native, before the
    // scalar-only editor is projected. Preserve per-network ordering.
    bool structured_proxy{false},proxy_onion_route{false};
    QString proxy_error;
};
struct Validation {
    std::map<Key, QString> errors;
    bool requires_redownload{false};
    bool valid() const { return errors.empty(); }
};
enum class Outcome { Applied, Rejected, Conflict, Failed, Uncertain };
struct Result {
    Outcome outcome{Outcome::Rejected};
    Snapshot snapshot;
    Validation validation;
    QString error;
    bool write_completed{false};
    bool persisted{false}, runtime_confirmed{false};
    std::map<Key, std::optional<Value>> persisted_values{};
    std::map<Key, std::optional<Value>> remembered_values{};
};
QString Name(Key key);
Kind Type(Key key);
const Field* Find(const Snapshot& snapshot, Key key);
Validation Validate(const Snapshot& candidate, const Batch& batch, const Constraints& constraints);
/** Read/commit are Native-only adapters. They capture complete current settings,
 * use Core's own soft parameter interactions on a private ArgsManager, and
 * validate before the first persistent mutation. */
struct Check { bool valid{false}; Snapshot snapshot; Validation validation; QString error; };
Snapshot Read(ArgsManager& args);
Check ReviewCurrent(ArgsManager& args);
// Onboarding validates its private staged settings before the replacement
// exists. No disk-currency claim and no process-wide network selection.
Check ReviewStaged(ArgsManager& args, const Batch& batch);
Check Review(ArgsManager& args, const Snapshot& original, const Batch& batch);
Result Commit(interfaces::Node& node, ArgsManager& args, const Snapshot& original, const Batch& batch);
} // namespace effectivesettings
#endif // BITCOIN_QML_EFFECTIVESETTINGS_H
