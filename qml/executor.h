// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_EXECUTOR_H
#define BITCOIN_QML_EXECUTOR_H

#include <functional>
#include <memory>

namespace execution {
enum class Lane;
enum class UtilityWorker { IO, Signer, Cleanup };

class Executor;

class SerialQueue
{
public:
    SerialQueue(SerialQueue&&) noexcept = default;
    SerialQueue& operator=(SerialQueue&&) noexcept = default;
    SerialQueue(const SerialQueue&) = delete;

    bool post(std::function<void()> task);
    void close();
    void whenDrained(std::function<void()> done);

private:
    friend class Executor;
    struct State;
    explicit SerialQueue(std::shared_ptr<State> state);
    std::shared_ptr<State> m_state;
};

class Executor
{
public:
    struct Config {};

    // An accepted dispatch must run the callback exactly once. A rejected
    // dispatch or transport exception must leave it uncalled. The callback
    // may also run inline. Callbacks handle their own failures.
    using Dispatch = std::function<bool(Lane, UtilityWorker, std::function<void()>)>;

    explicit Executor(Config);
    explicit Executor(Dispatch dispatch);
    ~Executor();

    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;

    SerialQueue makeQueue(Lane lane, UtilityWorker utility = UtilityWorker::IO);

    // Call from outside executor work and drain callbacks, one caller at a time.
    // The app finishes feature cleanup before closing the remaining queues.
    void stopAndJoin();

private:
    struct State;
    std::unique_ptr<State> m_state;
};
} // namespace execution

#endif // BITCOIN_QML_EXECUTOR_H
