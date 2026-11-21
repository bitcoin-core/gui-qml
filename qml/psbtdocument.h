// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PSBTDOCUMENT_H
#define BITCOIN_QML_PSBTDOCUMENT_H

#include <qml/psbtworktypes.h>

#include <atomic>
#include <cstdint>
#include <memory>

namespace psbtdocument {

// Pure analysis of captured facts. No wallet, Node, device or file effect is
// permitted here. Native inspection and the Domain owner share these rules.
void Qualify(psbtwork::Snapshot& snapshot);

enum class Phase { Empty, Ready, Failed };
struct Snapshot {
    std::uint64_t generation{0}, operation{0}, sequence{0};
    Phase phase{Phase::Empty};
    std::shared_ptr<const psbtwork::Snapshot> analysis;
};

/** Owns one document generation and its coherent analysis on Domain.
 * Native completion already arrives on Domain through the injected scheduler;
 * consume must run in that delivery turn, before any GUI publication. The GUI
 * can only withdraw interest. RetainValues reserves background destruction for
 * the owner, including when its last facade disappears while a read is held.
 */
class Owner final
{
public:
    explicit Owner(std::uint64_t generation) { m_snapshot.generation = generation; }
    void withdraw() noexcept { m_withdrawn.store(true); }
    psbtwork::Result consume(std::uint64_t operation, psbtwork::Kind kind, psbtwork::Result result);
    const Snapshot& snapshot() const { return m_snapshot; } // Domain only.

private:
    std::atomic_bool m_withdrawn{false};
    Snapshot m_snapshot;
};

} // namespace psbtdocument

#endif // BITCOIN_QML_PSBTDOCUMENT_H
