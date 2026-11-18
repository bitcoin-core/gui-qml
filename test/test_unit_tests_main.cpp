// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QApplication>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QStringList>
#include <QStyleFactory>

#include <chainparams.h>
#include <qml/asyncjoin.h>
#include <qml/guipresentation.h>
#include <test/qt_test_registry.h>
#include <util/translation.h>
#include <cstdio>
#include <algorithm>
#include <optional>
#include <string_view>
#include <vector>

const TranslateFn G_TRANSLATION_FUN{nullptr};

int main(int argc, char* argv[])
{
    // Transition animations are irrelevant to assertions and make QML runs flaky.
    qputenv("BITCOIN_QML_TEST_NO_ANIMATIONS", "1");
    // Keep QSettings and standard paths away from the developer's real
    // configuration. The legacy-migration fixtures capture and verify
    // external stores by organization and application name; a real
    // ~/.config/Bitcoin presence (for example a key serialized as
    // @Invalid(), where contains() is true but the value is invalid)
    // can never satisfy that capture contract, and writes would pollute
    // the machine across runs.
    QTemporaryDir settings_sandbox;
    qputenv("XDG_CONFIG_HOME", (settings_sandbox.path() + QStringLiteral("/config")).toUtf8());
    // Runner options are removed before Qt consumes its unchanged function,
    // data-row and logging arguments. --list-test-classes needs no application
    // or test object. --test-class=NAME overrides a valid legacy environment
    // selection; every supplied class is validated before either path runs.
    const auto entries = qttestregistry::SortedEntries();
    auto classes = qEnvironmentVariable("BITCOIN_QML_TEST_CLASSES").split(',', Qt::SkipEmptyParts);
    std::optional<QString> requested_class;
    bool list_classes{false};
    int forwarded{1};
    for (int index{1}; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--list-test-classes") {
            list_classes = true;
        } else if (argument.starts_with("--test-class=")) {
            if (requested_class) {
                std::fputs("Only one --test-class=NAME selector may be supplied.\n", stderr);
                return 2;
            }
            const auto name = argument.substr(std::string_view{"--test-class="}.size());
            if (name.empty()) {
                std::fputs("--test-class=NAME requires a nonempty class name.\n", stderr);
                return 2;
            }
            requested_class = QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
        } else if (argument == "--test-class") {
            std::fputs("Use --test-class=NAME to select one Qt test class.\n", stderr);
            return 2;
        } else {
            argv[forwarded++] = argv[index];
            // A QtTest output filename is opaque, even when it happens to
            // have the spelling of a runner option.
            if (argument == "-o" && index + 1 < argc) argv[forwarded++] = argv[++index];
        }
    }
    argc = forwarded;
    argv[argc] = nullptr;
    auto supplied_classes = classes;
    if (requested_class) supplied_classes.push_back(*requested_class);
    for (const auto& name : supplied_classes) {
        const bool registered = std::any_of(entries.begin(), entries.end(), [&](const auto& entry) {
            return name == QString::fromUtf8(entry.name);
        });
        if (!registered) {
            std::fputs("Unknown Qt test class: ", stderr);
            std::fputs(name.toUtf8().constData(), stderr);
            std::fputs("\n", stderr);
            return 2;
        }
    }
    if (requested_class) classes = {*requested_class};
    if (list_classes) {
        if (entries.empty()) {
            std::fputs("No Qt test classes are registered.\n", stderr);
            return 2;
        }
        for (const auto& entry : entries) std::puts(entry.name);
        return 0;
    }

    QApplication app(argc, argv);
    // The default macOS style draws through Cocoa views, which the minimal
    // platform the tests run on does not provide, so any shown widget crashes.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QCoreApplication::setOrganizationName("BitcoinCoreGuiQmlTests");
    QCoreApplication::setApplicationName("bitcoinqml_unit_tests");
    SelectParams(ChainType::REGTEST);

    int status = 0;
    bool ran{false};
    for (const auto& test : entries) {
        if (!classes.isEmpty() && !classes.contains(QString::fromUtf8(test.name))) continue;
        ran = true;
        status |= test.run(argc, argv);
    }
    QEventLoop loop;
    bool drained{false};
    WaitForThreadJoins(&loop, [&] { drained = true; loop.quit(); });
    while (!drained) loop.exec();
    presentation::FlushRetirements();
    return ran ? status : 1;
}
