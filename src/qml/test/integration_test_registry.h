// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_INTEGRATION_TEST_REGISTRY_H
#define BITCOIN_QML_TEST_INTEGRATION_TEST_REGISTRY_H

#include <algorithm>
#include <string_view>
#include <vector>

class BitcoinQmlApplication;

namespace qmlintegration {
struct Entry {
    const char* name;
    int (*run)(BitcoinQmlApplication&, int, char**);
};

inline std::vector<Entry>& Registry()
{
    static std::vector<Entry> entries;
    return entries;
}

struct Registration {
    Registration(const char* name, int (*run)(BitcoinQmlApplication&, int, char**))
    {
        Registry().push_back({name, run});
    }
};

inline auto SortedEntries()
{
    auto entries = Registry();
    std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
        return std::string_view(left.name) < std::string_view(right.name);
    });
    return entries;
}
} // namespace qmlintegration

#define BITCOINQML_REGISTER_INTEGRATION_TEST(TestClass)                      \
    namespace {                                                            \
    int RunIntegration_##TestClass(BitcoinQmlApplication& app, int argc, char** argv) \
    {                                                                      \
        TestClass tests(app);                                              \
        return QTest::qExec(&tests, argc, argv);                             \
    }                                                                      \
    [[maybe_unused]] qmlintegration::Registration g_integration_##TestClass{ \
        #TestClass, &RunIntegration_##TestClass};                           \
    }

#endif // BITCOIN_QML_TEST_INTEGRATION_TEST_REGISTRY_H
