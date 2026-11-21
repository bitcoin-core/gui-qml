// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_PAYMENTIMPORTCREATION_H
#define BITCOIN_QML_PAYMENTIMPORTCREATION_H
#include <atomic>
#include <cstdint>

namespace paymentimport {
/** One pre-reserved factory call. This receipt accounts for Source admission,
 * independently of its QObject projection, parsing result or later effects.
 * An uncertain caller reads this same receipt; it never calls the factory again.
 */
class CreationReceipt final {
public:
    enum class State { Reserved, Creating, Accepted, NotAccepted };
    bool begin() {
        auto expected = State::Reserved;
        return m_state.compare_exchange_strong(expected, State::Creating);
    }
    void withdraw() {
        m_withdrawn.store(true);
        auto expected = State::Reserved;
        m_state.compare_exchange_strong(expected, State::NotAccepted);
    }
    bool withdrawn() const { return m_withdrawn.load(); }
    State state() const { return m_state.load(); }
    uint64_t source() const { return state() == State::Accepted ? m_source.load() : 0; }
    void accepted(uint64_t source) {
        m_source.store(source);
        m_state.store(State::Accepted);
    }
    void notAccepted() {
        auto expected = State::Creating;
        m_state.compare_exchange_strong(expected, State::NotAccepted);
    }
private:
    std::atomic<State> m_state{State::Reserved};
    std::atomic<uint64_t> m_source{0};
    std::atomic_bool m_withdrawn{false};
};
} // namespace paymentimport
#endif // BITCOIN_QML_PAYMENTIMPORTCREATION_H
