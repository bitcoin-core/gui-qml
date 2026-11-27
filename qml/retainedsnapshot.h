// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_RETAINEDSNAPSHOT_H
#define BITCOIN_QML_RETAINEDSNAPSHOT_H
#include <qml/retainedvalue.h>
#include <functional>
#include <memory>
#include <utility>
namespace execution {
// Ordinary snapshot ownership. Transfer the last value reference to the
// existing presentation cleanup when a facade or queued result drops it.
template <typename T, typename Factory>
std::shared_ptr<T> RetainSnapshot(const std::function<void(std::shared_ptr<const void>)>& retire, Factory&& factory)
{
    if (!retire) return {};
    using Retire = std::function<void(std::shared_ptr<const void>)>;
    struct Storage {
        Storage(std::shared_ptr<T> value, Retire retire) : value(std::move(value)), retire(std::move(retire)) {}
        ~Storage() { retire(std::move(value)); }
        std::shared_ptr<T> value;
        Retire retire;
    };
    auto storage = std::make_shared<Storage>(std::forward<Factory>(factory)(), retire);
    return {storage, storage->value.get()};
}
} // namespace execution
#endif // BITCOIN_QML_RETAINEDSNAPSHOT_H
