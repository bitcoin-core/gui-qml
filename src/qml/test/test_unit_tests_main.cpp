// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/test/qt_test_registry.h>
#include <util/translation.h>

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <functional>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

extern const std::function<std::vector<const char*>()> G_TEST_COMMAND_LINE_ARGUMENTS{};
extern const std::function<std::string()> G_TEST_GET_FULL_NAME{};
const TranslateFn G_TRANSLATION_FUN{nullptr};

int RunQmlTests(int argc, char* argv[]);

namespace {
enum class TestSuite {
    UNIT,
    QML,
};

bool ParseTestSuite(int& argc, char* argv[], TestSuite& suite)
{
    constexpr std::string_view PREFIX{"--suite="};
    int output_index{1};
    bool suite_set{false};
    for (int input_index{1}; input_index < argc; ++input_index) {
        const std::string_view argument{argv[input_index]};
        if (!argument.starts_with(PREFIX)) {
            argv[output_index++] = argv[input_index];
            continue;
        }
        if (suite_set) {
            std::cerr << "The --suite option may only be specified once.\n";
            return false;
        }
        suite_set = true;
        const std::string_view value{argument.substr(PREFIX.size())};
        if (value == "unit") {
            suite = TestSuite::UNIT;
        } else if (value == "qml") {
            suite = TestSuite::QML;
        } else {
            std::cerr << "Unknown test suite: " << value << "\n";
            return false;
        }
    }
    argc = output_index;
    argv[argc] = nullptr;
    return true;
}

int RunUnitTests(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    int status{0};
    for (const auto& test : qttestregistry::SortedEntries()) {
        status |= test.run(argc, argv);
    }
    return status;
}
} // namespace

int main(int argc, char* argv[])
{
    TestSuite suite{TestSuite::UNIT};
    if (!ParseTestSuite(argc, argv, suite)) return EXIT_FAILURE;

#ifdef Q_OS_LINUX
    // Qt requires a private runtime directory with 0700 permissions.
    QTemporaryDir runtime_dir;
    if (!runtime_dir.isValid() || !qputenv("XDG_RUNTIME_DIR", QFile::encodeName(runtime_dir.path()))) {
        std::cerr << "Failed to set up a private XDG_RUNTIME_DIR for Qt tests.\n";
        return EXIT_FAILURE;
    }
#endif

    switch (suite) {
    case TestSuite::UNIT:
        return RunUnitTests(argc, argv);
    case TestSuite::QML:
        return RunQmlTests(argc, argv);
    }
    return EXIT_FAILURE;
}
