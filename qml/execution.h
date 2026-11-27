// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_EXECUTION_H
#define BITCOIN_QML_EXECUTION_H

#ifndef Q_MOC_RUN
#include <compare>
#endif
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <limits>
#include <list>
#include <mutex>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>
#include <variant>

namespace execution {
struct TaskId {
    uint64_t value{0};
    auto operator<=>(const TaskId&) const = default;
};
enum class Rejection { None, Closed, Transport, Capacity, Invalid };
enum class Lane { Domain, Core, Control, Cleanup, Priority, Rpc, Utility };
enum class WorkClass { Action, Observation };
struct SubmitResult {
    TaskId id;
    Rejection rejection{Rejection::None};
    explicit operator bool() const { return rejection == Rejection::None; }
};
class NotStarted final : public std::exception
{
public:
    enum class Cause { Cancelled, Transport };
    explicit NotStarted(Cause value) : cause(value) {}
    const char* what() const noexcept override { return "Task did not start"; }
    const Cause cause;
};
/** Why a task did not start, or nothing when the error is absent or something else. */
inline std::optional<NotStarted::Cause> NotStartedCause(const std::exception_ptr& error) noexcept
{
    if (!error) return {};
    try { std::rethrow_exception(error); }
    catch (const NotStarted& stopped) { return stopped.cause; }
    catch (...) { return {}; }
}

/** Owned data and callbacks. Execution and outcome delivery are separate turns. */
struct Task {
    virtual ~Task() = default;
    virtual void execute() = 0;
    virtual void notStarted(NotStarted::Cause cause) = 0;
    virtual void deliver() = 0;
};

template <typename Work, typename Done>
std::shared_ptr<Task> MakeTask(Work work, Done done, std::function<void(std::exception_ptr)> failed)
{
    using Return = std::invoke_result_t<Work>;
    static_assert(!std::is_reference_v<Return>, "Return an owned result");
    using Result = std::conditional_t<std::is_void_v<Return>, std::monostate, Return>;
    struct Packet final : Task {
        std::optional<Work> work;
        Done done;
        std::function<void(std::exception_ptr)> failed;
        std::optional<Result> result;
        std::exception_ptr error;
        Packet(Work w, Done d, std::function<void(std::exception_ptr)> f)
            : work(std::move(w)), done(std::move(d)), failed(std::move(f)) {}
        void execute() override
        {
            try {
                if constexpr (std::is_void_v<Return>) {
                    std::invoke(*work);
                    result.emplace();
                } else {
                    result.emplace(std::invoke(*work));
                }
            } catch (...) {
                error = std::current_exception();
            }
            work.reset();
        }
        void deliver() override
        {
            if (error) {
                if (failed) failed(error);
            } else if constexpr (std::is_void_v<Return>) {
                std::invoke(done);
            } else {
                std::invoke(done, std::move(*result));
            }
        }
        void notStarted(NotStarted::Cause cause) override
        {
            error = std::make_exception_ptr(NotStarted{cause});
            work.reset();
        }
    };
    return std::make_shared<Packet>(std::move(work), std::move(done), std::move(failed));
}

} // namespace execution

#endif // BITCOIN_QML_EXECUTION_H
