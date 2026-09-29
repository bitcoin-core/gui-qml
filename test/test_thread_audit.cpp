// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/thread_audit.h>
#include <test/backend_barrier.h>
#include <test/mocks/mockwallet.h>
#include <test/mocks/stubnode.h>
#include <qml/models/nodemodel.h>

#include <chainparams.h>
#include <common/args.h>
#include <common/system.h>
#include <util/translation.h>

#include <QCoreApplication>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

using namespace qmlintegration;
using namespace std::chrono_literals;

const TranslateFn G_TRANSLATION_FUN{};

namespace {

struct WalletLifetime {
    std::atomic<int> alive{0};
    std::atomic<int> calls{0};
};

class AuditWallet : public StubWallet
{
public:
    explicit AuditWallet(std::shared_ptr<WalletLifetime> state) : m_state{std::move(state)} { ++m_state->alive; }
    ~AuditWallet() override { --m_state->alive; }
    CAmount getBalance() override { std::fprintf(stderr, "BACKEND_ENTERED\n"); std::fflush(stderr); ++m_state->calls; return 123; }
    std::string getWalletName() override { return "audit-wallet"; }
private:
    std::shared_ptr<WalletLifetime> m_state;
};

class AuditLoader : public StubWalletLoader
{
public:
    std::shared_ptr<WalletLifetime> state{std::make_shared<WalletLifetime>()};
    LoadWalletFn callback;
    std::unique_ptr<interfaces::Wallet> wallet() { return std::make_unique<AuditWallet>(state); }
    util::Result<std::unique_ptr<interfaces::Wallet>> createWallet(const std::string&, const SecureString&, uint64_t, std::vector<bilingual_str>&) override { return wallet(); }
    util::Result<std::unique_ptr<interfaces::Wallet>> loadWallet(const std::string&, std::vector<bilingual_str>&) override { return wallet(); }
    util::Result<std::unique_ptr<interfaces::Wallet>> restoreWallet(const fs::path&, const std::string&, std::vector<bilingual_str>&, bool) override { return wallet(); }
    util::Result<interfaces::WalletMigrationResult> migrateWallet(const std::string&, const SecureString&) override
    {
        return interfaces::WalletMigrationResult{wallet(), "watch", "solvable", "backup"};
    }
    std::vector<std::unique_ptr<interfaces::Wallet>> getWallets() override
    {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(wallet());
        return wallets;
    }
    std::unique_ptr<interfaces::Handler> handleLoadWallet(LoadWalletFn fn) override
    {
        callback = std::move(fn);
        return interfaces::MakeCleanupHandler([this] { callback = {}; });
    }
};

class LoaderNode : public StubNode
{
public:
    AuditLoader loader;
    interfaces::WalletLoader& walletLoader() override { return loader; }
};

std::unique_ptr<interfaces::Wallet> DeliveredWallet(interfaces::WalletLoader& loader, AuditLoader& backend, const QString& route)
{
    std::vector<bilingual_str> warnings;
    if (route == "create") return std::move(*loader.createWallet("", {}, 0, warnings));
    if (route == "load") return std::move(*loader.loadWallet("", warnings));
    if (route == "restore") return std::move(*loader.restoreWallet({}, "", warnings, true));
    if (route == "migrate") return std::move(loader.migrateWallet("", {})->wallet);
    if (route == "list") return std::move(loader.getWallets().front());
    std::unique_ptr<interfaces::Wallet> result;
    auto handler = loader.handleLoadWallet([&](std::unique_ptr<interfaces::Wallet> wallet) { result = std::move(wallet); });
    backend.callback(backend.wallet());
    return result;
}

class AuditNode : public StubNode
{
public:
    Barrier* action_barrier{nullptr};
    Barrier* snapshot_barrier{nullptr};
    std::atomic<bool> hold_snapshot{false};
    std::atomic<int> warning_calls{0};
    NotifyAlertChangedFn alert;
    bilingual_str getWarnings() override
    {
        ++warning_calls;
        if (hold_snapshot) snapshot_barrier->enter();
        return Untranslated(hold_snapshot ? "changed" : "initial");
    }
    void setNetworkActive(bool) override { action_barrier->enter(); }
    std::unique_ptr<interfaces::Handler> handleNotifyAlertChanged(NotifyAlertChangedFn fn) override
    {
        alert = std::move(fn);
        return interfaces::MakeCleanupHandler([this] { alert = {}; });
    }
    int getNumBlocks() override { std::fprintf(stderr, "BACKEND_ENTERED\n"); std::fflush(stderr); return 42; }
    bool baseInitialize() override { return true; }
};

int Violate(const QString& route)
{
    // Production redirects Qt messages into debug.log; fatal diagnostics must
    // still be present in the process stderr captured by CTest/Python.
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString&) {});
    auto audit = std::make_shared<ThreadAudit>(route != "ipc-shutdown");
    audit->setPhase(TestPhase::Running);
    if (route.startsWith("wallet-")) {
        auto backend = std::make_unique<AuditLoader>();
        auto* raw = backend.get();
        auto loader = CheckWalletLoader(std::move(backend), audit);
        auto wallet = std::async(std::launch::async, [&] { return DeliveredWallet(*loader, *raw, route.mid(7)); }).get();
        wallet->getBalance();
    } else if (route == "affinity") {
        QObject model;
        std::thread worker{[&] { RequireModelThread(&model); }};
        worker.join();
    } else {
        auto node = CheckNode(std::make_unique<AuditNode>(), audit);
        if (route == "default") node->context();
        else if (route == "bootstrap") node->baseInitialize();
        else if (route == "ipc-shutdown") node->shutdownRequested();
        else node->getNumBlocks();
    }
    return 0;
}
} // namespace

class ThreadAuditTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { SelectParams(ChainType::REGTEST); }

    void forbiddenCallsFailBeforeForwarding_data()
    {
        QTest::addColumn<QString>("route");
        QTest::addColumn<QByteArray>("method");
        QTest::newRow("node") << QString{"node"} << QByteArray{"Node::getNumBlocks"};
        QTest::newRow("inherited-default") << QString{"default"} << QByteArray{"Node::context"};
        QTest::newRow("bootstrap-ended") << QString{"bootstrap"} << QByteArray{"Node::baseInitialize"};
        QTest::newRow("no-local-exceptions-for-ipc") << QString{"ipc-shutdown"} << QByteArray{"Node::shutdownRequested"};
        QTest::newRow("model-affinity") << QString{"affinity"} << QByteArray{"GUI model mutated on a foreign thread"};
        for (const auto* route : {"create", "load", "restore", "migrate", "list", "callback"}) {
            QTest::newRow(route) << QString{"wallet-"} + route << QByteArray{"Wallet::getBalance"};
        }
    }

    void forbiddenCallsFailBeforeForwarding()
    {
        QFETCH(QString, route);
        QFETCH(QByteArray, method);
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(), {"--violate", route});
        QVERIFY(child.waitForFinished(20'000));
        const auto diagnostics = child.readAllStandardError();
        QVERIFY2(child.exitStatus() == QProcess::CrashExit || child.exitCode() != 0, diagnostics.constData());
        QVERIFY2(diagnostics.contains(method), diagnostics.constData());
        if (route != "affinity") QVERIFY(diagnostics.contains("phase=running"));
        QVERIFY(!diagnostics.contains("BACKEND_ENTERED"));
    }

    void forwardingPreservesWalletOwnershipAndResults()
    {
        auto audit = std::make_shared<ThreadAudit>();
        auto backend = std::make_unique<LoaderNode>();
        auto* raw = &backend->loader;
        auto state = raw->state;
        auto node = CheckNode(std::move(backend), audit);
        auto& loader = node->walletLoader();
        QCOMPARE(&loader, &node->walletLoader());
        QVERIFY(&loader != raw);
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        std::async(std::launch::async, [&] {
            for (const auto* route : {"create", "load", "restore", "migrate", "list", "callback"}) {
                wallets.push_back(DeliveredWallet(loader, *raw, route));
            }
        }).get();
        QCOMPARE(state->alive.load(), 6);
        QVERIFY(!raw->callback); // Returned handler disconnected on destruction.
        node.reset();
        audit.reset(); // Wallets retain the shared audit, independently of loader.
        const auto results = std::async(std::launch::async, [&] {
            CAmount sum{0};
            for (const auto& wallet : wallets) sum += wallet->getBalance();
            return sum;
        }).get();
        QCOMPARE(results, 6 * 123);
        QCOMPARE(state->calls.load(), 6);
        wallets.clear();
        QCOMPARE(state->alive.load(), 0);
    }

    void bootstrapExceptionEndsAtRuntime()
    {
        auto audit = std::make_shared<ThreadAudit>();
        auto node = CheckNode(std::make_unique<AuditNode>(), audit);
        QVERIFY(node->baseInitialize());
        audit->setPhase(TestPhase::Running);
        QCOMPARE(std::async(std::launch::async, [&] { return node->getNumBlocks(); }).get(), 42);
        // Cancellation remains possible when a local worker is blocked.
        std::async(std::launch::async, [&] { node->startShutdown(); }).get();
        QVERIFY(!node->shutdownRequested());
    }

    void guiProcessesEventsDuringActionAndDrain()
    {
        Barrier barrier;
        auto backend = std::make_unique<AuditNode>();
        backend->action_barrier = &barrier;
        auto audit = std::make_shared<ThreadAudit>();
        auto checked = CheckNode(std::move(backend), audit);
        NodeModel model{*checked};
        model.initializeResult(true, {});
        audit->setPhase(TestPhase::Running);
        QTRY_COMPARE(model.warnings(), QString{"initial"});
        model.setPause(true);
        QTRY_VERIFY(barrier.entered.load());
        bool event_received{false};
        QTimer::singleShot(0, &model, [&] { event_received = true; });
        QTRY_VERIFY(event_received);
        QVERIFY(!barrier.timed_out);
        QSignalSpy drained{&model, &NodeModel::backendDrained};
        model.drainBackend();
        event_received = false;
        QTimer::singleShot(0, &model, [&] { event_received = true; });
        QTRY_VERIFY(event_received);
        QCOMPARE(drained.count(), 0);
        QVERIFY(!barrier.timed_out);
        barrier.release();
        QTRY_COMPARE(drained.count(), 1);
    }

    void notificationBurstPublishesOneFollowupSnapshot()
    {
        Barrier barrier;
        auto backend = std::make_unique<AuditNode>();
        auto* raw = backend.get();
        raw->snapshot_barrier = &barrier;
        auto audit = std::make_shared<ThreadAudit>();
        auto checked = CheckNode(std::move(backend), audit);
        NodeModel model{*checked};
        audit->setPhase(TestPhase::Running);
        QTRY_COMPARE(model.warnings(), QString{"initial"});
        raw->hold_snapshot = true;
        raw->alert();
        QTRY_VERIFY(barrier.entered.load());
        const int calls = raw->warning_calls.load();
        for (int i = 0; i < 40; ++i) raw->alert();
        bool delivered{false};
        QTimer::singleShot(0, &model, [&] { delivered = true; });
        QTRY_VERIFY(delivered);
        QCOMPARE(model.warnings(), QString{"initial"});
        QVERIFY(!barrier.timed_out);
        barrier.release();
        QTRY_COMPARE(model.warnings(), QString{"changed"});
        QTRY_COMPARE(raw->warning_calls.load(), calls + 1);
        QSignalSpy drained{&model, &NodeModel::backendDrained};
        model.drainBackend();
        QTRY_COMPARE(drained.count(), 1);
        QCOMPARE(raw->warning_calls.load(), calls + 1);
    }

    void notificationsCoalesceAndShutdownDiscardsPendingSnapshot()
    {
        Barrier barrier;
        auto backend = std::make_unique<AuditNode>();
        auto* raw = backend.get();
        raw->snapshot_barrier = &barrier;
        auto audit = std::make_shared<ThreadAudit>();
        auto checked = CheckNode(std::move(backend), audit);
        NodeModel model{*checked};
        audit->setPhase(TestPhase::Running);
        QTRY_COMPARE(model.warnings(), QString{"initial"});
        raw->hold_snapshot = true;
        raw->alert();
        QTRY_VERIFY(barrier.entered.load());
        const int calls = raw->warning_calls.load();
        for (int i = 0; i < 40; ++i) raw->alert();
        bool delivered{false};
        QTimer::singleShot(0, &model, [&] { delivered = true; });
        QTRY_VERIFY(delivered);
        QCOMPARE(raw->warning_calls.load(), calls);
        QCOMPARE(model.warnings(), QString{"initial"});
        QSignalSpy drained{&model, &NodeModel::backendDrained};
        model.drainBackend();
        barrier.release();
        QTRY_COMPARE(drained.count(), 1);
        QCOMPARE(model.warnings(), QString{"initial"});
        QCOMPARE(raw->warning_calls.load(), calls);
        QVERIFY(!barrier.timed_out);
    }
};

int main(int argc, char* argv[])
{
    QCoreApplication app{argc, argv};
    if (argc == 3 && QByteArray{argv[1]} == "--violate") return Violate(QString::fromLocal8Bit(argv[2]));
    ThreadAuditTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include <test_thread_audit.moc>
