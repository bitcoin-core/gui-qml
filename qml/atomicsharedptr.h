// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_ATOMICSHAREDPTR_H
#define BITCOIN_QML_ATOMICSHAREDPTR_H

#include <atomic>
#include <memory>
#include <utility>
#include <version>

namespace ownership {
/** Atomic ownership publication, including libc++ versions without the C++20
 * atomic<shared_ptr> specialization. The standard fallback can use internal
 * synchronization, just like the specialization; neither is claimed lock-free.
 * Values retain their existing Native cleanup owners independently of this slot.
 * Destroy the slot only after all publishing/reading actors have drained. */
template <typename T>
class AtomicSharedPtr final
{
public:
    AtomicSharedPtr() noexcept = default;
    explicit AtomicSharedPtr(std::shared_ptr<T> value) noexcept : m_value(std::move(value)) {}
    AtomicSharedPtr(const AtomicSharedPtr&) = delete;
    AtomicSharedPtr& operator=(const AtomicSharedPtr&) = delete;
    std::shared_ptr<T> load(std::memory_order order = std::memory_order_seq_cst) const noexcept
    {
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
        return m_value.load(order);
#else
        return std::atomic_load_explicit(&m_value, order);
#endif
    }
    void store(std::shared_ptr<T> value, std::memory_order order = std::memory_order_seq_cst) noexcept
    {
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
        m_value.store(std::move(value), order);
#else
        std::atomic_store_explicit(&m_value, std::move(value), order);
#endif
    }
    std::shared_ptr<T> exchange(std::shared_ptr<T> value, std::memory_order order = std::memory_order_seq_cst) noexcept
    {
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
        return m_value.exchange(std::move(value), order);
#else
        return std::atomic_exchange_explicit(&m_value, std::move(value), order);
#endif
    }
private:
#if defined(__cpp_lib_atomic_shared_ptr) && __cpp_lib_atomic_shared_ptr >= 201711L
    std::atomic<std::shared_ptr<T>> m_value;
#else
    std::shared_ptr<T> m_value;
#endif
};
} // namespace ownership
#endif // BITCOIN_QML_ATOMICSHAREDPTR_H
