// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_RETAINEDSNAPSHOT_H
#define BITCOIN_QML_RETAINEDSNAPSHOT_H
#include <qml/execution.h>
#include <memory>
#include <utility>
namespace execution {
// The scheduler retains the allocation, while GUI aliases retain only its
// cleanup lease. Admission precedes constructing/capturing structural values.
template <typename T, typename Factory>
std::shared_ptr<T> RetainSnapshot(const std::shared_ptr<Scheduler>& scheduler, Factory&& factory)
{
    struct Storage { std::unique_ptr<T> value; };
    const auto storage = std::make_shared<Storage>();
    const auto key = scheduler->allocateKey();
    const auto reserved = Submit(*scheduler, [storage] { storage->value.reset(); }, [] {}, {},
        {Lane::Cleanup, key, WorkClass::Action}, true);
    if (!reserved) return {};
    auto held = scheduler->borrow(key);
    scheduler->activateCleanup(reserved.id);
    if (!held) return {};
    const auto lease = std::make_shared<ResourceLease>(std::move(*held.lease));
    storage->value = std::forward<Factory>(factory)();
    return std::shared_ptr<T>{lease, storage->value.get()};
}
} // namespace execution
#endif // BITCOIN_QML_RETAINEDSNAPSHOT_H
