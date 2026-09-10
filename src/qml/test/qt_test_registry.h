// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_QT_TEST_REGISTRY_H
#define BITCOIN_QML_TEST_QT_TEST_REGISTRY_H

#include <algorithm>
#include <string_view>
#include <vector>

namespace qttestregistry {

using Runner = int (*)(int, char**);

struct Entry
{
    const char* name;
    Runner run;
};

inline std::vector<Entry>& Registry()
{
    static std::vector<Entry> registry;
    return registry;
}

struct Registration
{
    Registration(const char* name, Runner run)
    {
        Registry().push_back({name, run});
    }
};

inline std::vector<Entry> SortedEntries()
{
    auto entries = Registry();
    std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
        return std::string_view{left.name} < std::string_view{right.name};
    });
    return entries;
}

} // namespace qttestregistry

#define BITCOINQML_REGISTER_QT_TEST(TestClass)                            \
    namespace {                                                           \
    int Run_##TestClass(int argc, char* argv[])                           \
    {                                                                     \
        TestClass tests;                                                  \
        return QTest::qExec(&tests, argc, argv);                          \
    }                                                                     \
    [[maybe_unused]] qttestregistry::Registration g_register_##TestClass{ \
        #TestClass, &Run_##TestClass};                                    \
    }

#endif // BITCOIN_QML_TEST_QT_TEST_REGISTRY_H
