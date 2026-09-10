// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_CALLCOUNTER_H
#define BITCOIN_QML_TEST_MOCKS_CALLCOUNTER_H

#include <atomic>
#include <string_view>

class CallCounter
{
public:
    explicit CallCounter(std::string_view name) : m_name{name} {}

    CallCounter(const CallCounter&) = delete;
    CallCounter& operator=(const CallCounter&) = delete;

    int operator++() { return m_calls.fetch_add(1) + 1; }
    int load() const { return m_calls.load(); }
    std::string_view name() const { return m_name; }

private:
    std::atomic<int> m_calls{0};
    const std::string_view m_name;
};

#endif // BITCOIN_QML_TEST_MOCKS_CALLCOUNTER_H
