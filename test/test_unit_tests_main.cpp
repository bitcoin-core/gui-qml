// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QApplication>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QStringList>
#include <QStyleFactory>

#include <chainparams.h>
#include <qml/backendexecutor.h>
#include <qml/guipresentation.h>
#include <qml/qtlogsink.h>
#include <test/qt_test_registry.h>
#include <util/translation.h>
#include <cstdio>
#include <algorithm>
#include <atomic>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

const TranslateFn G_TRANSLATION_FUN{nullptr};

namespace {
std::shared_ptr<qtlog::FatalPolicy> configured_fatal_policy;
std::shared_ptr<qtlog::Queue> configured_log_queue;
qtlog::Stage configured_log_stage{qtlog::Stage::Startup};
void ConfiguredQtHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{ qtlog::Forward(type, context, message, configured_log_stage, configured_fatal_policy, configured_log_queue); }
}

int RunStartupProfileProbe(const QString& mode);
int RunLegacyUnitProbe();

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
    // Runs before anything registers the legacy BitcoinUnit type.
    if (qEnvironmentVariableIsSet("BITCOIN_QML_LEGACY_UNIT_CASE")) return RunLegacyUnitProbe();
    if(qEnvironmentVariableIsSet("BITCOIN_QML_STARTUP_PROFILE_CASE"))return RunStartupProfileProbe(qEnvironmentVariable("BITCOIN_QML_STARTUP_PROFILE_CASE"));
    if (qEnvironmentVariableIsSet("BITCOIN_QML_QTLOG_SINK_CASE")) {
        const bool overflow = qgetenv("BITCOIN_QML_QTLOG_SINK_CASE") == "overflow";
        qtlog::NativeSink sink{[overflow](const qtlog::Record& record) {
            const auto text = overflow ? qtlog::FormatStderr(record) : QByteArray{"Qt log injected write failure\n"};
            std::fwrite(text.constData(), 1, static_cast<size_t>(text.size()), stderr); std::fflush(stderr);
            return overflow;
        }, {1, sizeof(qtlog::Record) + 16}};
        sink.queue()->enqueue(QtWarningMsg, {}, overflow ? QString(1000, 'x') : QStringLiteral("fail"), qtlog::Stage::Startup);
        sink.finishAfterGui();
        return qtlog::FinalExitStatus(0, sink.queue()->receipt());
    }
    if (qEnvironmentVariableIsSet("BITCOIN_QML_QTLOG_CONFIGURED_CASE")) {
        const auto mode = qgetenv("BITCOIN_QML_QTLOG_CONFIGURED_CASE");
        const bool critical = mode.contains("critical"), concurrent = mode.contains("concurrent");
        configured_log_stage = mode.contains("main") ? qtlog::Stage::Main : qtlog::Stage::Startup;
        qtlog::NativeSink sink{[](const qtlog::Record& record) {
            if (record.stage == qtlog::Stage::Startup) {
                const auto text = qtlog::FormatStderr(record);
                std::fwrite(text.constData(), 1, static_cast<size_t>(text.size()), stderr); std::fflush(stderr);
            }
            return true;
        }};
        configured_log_queue = sink.queue(); configured_fatal_policy = qtlog::CaptureFatalPolicy();
        qInstallMessageHandler(ConfiguredQtHandler);
        const auto emit_message = [&](int thread, int index) {
            const QString message = QStringLiteral("qtlog configured %1 %2:%3 - configured-fatal-tail-").arg(critical ? "critical" : "warning").arg(thread).arg(index) + QString::fromUtf8("\xE2\x82\xAC");
            if (critical) qCritical("%s", message.toUtf8().constData()); else qWarning("%s", message.toUtf8().constData());
        };
        if (concurrent) {
            std::atomic<int> entered{0}; std::atomic_bool go{false}; std::vector<std::thread> threads;
            threads.reserve(4);
            for (int thread = 0; thread < 4; ++thread) threads.emplace_back([&, thread] {
                ++entered; while (!go) std::this_thread::yield();
                for (int index = 0; index < 32; ++index) emit_message(thread, index);
            });
            while (entered != 4) std::this_thread::yield();
            go = true;
            for (auto& thread : threads) thread.join();
        } else for (int index = 0; index < 16; ++index) emit_message(0, index);
        configured_log_queue.reset(); qInstallMessageHandler(nullptr); sink.finishAfterGui();
        return qtlog::FinalExitStatus(0, sink.queue()->receipt());
    }
    if (qEnvironmentVariableIsSet("BITCOIN_QML_QTLOG_FATAL_CASE")) {
        configured_fatal_policy = qtlog::CaptureFatalPolicy(); qInstallMessageHandler(ConfiguredQtHandler);
        qFatal("qtlog fatal: %s", qgetenv("BITCOIN_QML_QTLOG_FATAL_CASE").constData());
    }

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
    BackendExecutor::shutdownAll(&loop, [&] { drained = true; loop.quit(); });
    while (!drained) loop.exec();
    presentation::FlushRetirements();
    return ran ? status : 1;
}
