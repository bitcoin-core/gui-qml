// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_RETAINEDVALUE_H
#define BITCOIN_QML_RETAINEDVALUE_H

#include <qml/walletopening.h>
#include <memory>
#include <utility>

namespace walletopening {
/** Reserve cleanup before constructing values that will cross into GUI.
 * Every alias shares the resource lease, while Cleanup owns the allocation.
 * The last alias release makes teardown eligible without performing it inline.
 * A failed reservation does not invoke the factory or capture its values.
 */
template <typename T, typename Factory>
std::shared_ptr<T> RetainValues(const std::shared_ptr<Session>& opening, Factory&& factory)
{
    if (!opening) return {};
    struct Storage { std::unique_ptr<T> value; };
    const auto storage = std::make_shared<Storage>();
    auto held = opening->holdValues([storage] { storage->value.reset(); });
    if (!held) return {};
    const auto lease = std::make_shared<execution::ResourceLease>(std::move(*held.lease));
    storage->value = std::forward<Factory>(factory)();
    return std::shared_ptr<T>{lease, storage->value.get()};
}
} // namespace walletopening

namespace execution {
// Pure application values (editors, parsers, validation) share the injected
// scheduler without retaining a wallet or creating another executor.
template <typename T, typename Factory>
std::shared_ptr<T> RetainValues(const std::shared_ptr<Scheduler>& scheduler, Factory&& factory)
{
    if (!scheduler) return {};
    struct Storage { std::unique_ptr<T> value; };
    const auto storage = std::make_shared<Storage>();
    const auto key = scheduler->allocateKey();
    const auto cleanup = Submit(*scheduler, [storage] { storage->value.reset(); }, [] {}, {},
        {Lane::Cleanup, key, WorkClass::Action}, true);
    auto held = cleanup ? scheduler->borrow(key) : BorrowAdmission{{}, cleanup.rejection};
    if (cleanup) scheduler->activateCleanup(cleanup.id);
    if (!held) return {};
    const auto lease = std::make_shared<ResourceLease>(std::move(*held.lease));
    storage->value = std::forward<Factory>(factory)();
    return std::shared_ptr<T>{lease, storage->value.get()};
}
} // namespace execution

#endif // BITCOIN_QML_RETAINEDVALUE_H
