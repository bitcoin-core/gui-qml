// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_RETAINEDVALUE_H
#define BITCOIN_QML_RETAINEDVALUE_H

#include <qml/execution.h>
#include <qml/executor.h>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace execution {
using ValueCleanup = std::function<std::shared_ptr<void>(std::function<void()>)>;

// Keep the Utility queue open until each reserved value has been retired.
class ValueCleanupQueue : public std::enable_shared_from_this<ValueCleanupQueue>
{
public:
    explicit ValueCleanupQueue(std::shared_ptr<SerialQueue> queue);
    std::shared_ptr<void> hold(std::function<void()> cleanup);
    void close(std::function<void()> done);
    SubmitResult postWithOutcome(SerialQueue& worker, std::shared_ptr<Task> task);

private:
    struct Hold;
    void release();
    const std::shared_ptr<SerialQueue> m_queue;
    std::mutex m_mutex;
    size_t m_held{0};
    bool m_closed{false};
};

// Give the value its own control block so shared_from_this() and every
// external reference keep the same cleanup reservation alive.
template <typename T, typename Factory>
std::shared_ptr<T> RetainValues(const ValueCleanup& hold, Factory&& factory)
{
    if (!hold) return {};
    struct Storage { std::unique_ptr<T> value; };
    auto storage = std::make_shared<Storage>();
    auto lease = hold([storage] { storage->value.reset(); });
    if (!lease) return {};
    auto value = std::forward<Factory>(factory)();
    return std::shared_ptr<T>{value.release(),
        [storage = std::move(storage), lease = std::move(lease)](T* retired) mutable {
            storage->value.reset(retired);
            lease.reset();
            storage.reset();
        }};
}

} // namespace execution

namespace walletopening {
template <typename T, typename Opening, typename Factory>
std::shared_ptr<T> RetainValues(const std::shared_ptr<Opening>& opening, Factory&& factory)
{
    if (!opening) return {};
    return execution::RetainValues<T>([&opening](auto cleanup) { return opening->holdValues(std::move(cleanup)); },
                                      std::forward<Factory>(factory));
}
} // namespace walletopening

#endif // BITCOIN_QML_RETAINEDVALUE_H
