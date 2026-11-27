// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/executor.h>
#include <qml/walletbalanceadapter.h>
#include <qml/walletopening.h>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>

#include <QScopeGuard>
#include <QSemaphore>
#include <QtTest/QtTest>

#include <atomic>

using walletbalance::Status;

class WalletBalanceAdapterTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void realWorkerReleasesRetainedInterfaceOffGuiAndDrains()
    {
        struct State {
            QSemaphore entered, release;
            std::atomic<QThread*> read_thread{nullptr}, destroyed_thread{nullptr};
            std::atomic<int> notices{0};
        };
        struct Wallet final : StubWallet {
            explicit Wallet(std::shared_ptr<State> state) : state(std::move(state)) {}
            ~Wallet() override { state->destroyed_thread = QThread::currentThread(); }
            bool tryGetBalances(interfaces::WalletBalances& balances, uint256& hash) override
            {
                state->read_thread = QThread::currentThread();
                state->entered.release();
                state->release.acquire();
                balances.balance = 123;
                hash = uint256::ONE;
                return true;
            }
            const std::shared_ptr<State> state;
        };
        const auto state = std::make_shared<State>();
        execution::Executor executor{execution::Executor::Config{}};
        using execution::Lane;
        const auto core = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Core));
        const auto utility = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility));
        const auto closed = std::make_shared<std::atomic_bool>(false);
        const auto metadata_ready = std::make_shared<QSemaphore>();
        const auto stopped = std::make_shared<std::atomic_bool>(false);
        const auto opening = std::make_shared<walletopening::Session>(
            closed, [metadata_ready] { metadata_ready->release(); }, walletopening::Session::Preferences{},
            std::chrono::steady_clock::now, nullptr, walletopening::NativeEvents{},
            walletopening::Session::Queues{utility, [core](auto done) {
                core->close();
                core->whenDrained(std::move(done));
            }, core, utility});
        const auto complete = [stopped] { stopped->store(true); };
        const auto release_on_exit = qScopeGuard([&] {
            state->release.release();
            opening->stop(false, complete);
            QVERIFY(QTest::qWaitFor([&] { return stopped->load(); }, 5000));
        });
        std::unique_ptr<interfaces::Wallet> wallet = std::make_unique<Wallet>(state);
        std::unique_ptr<interfaces::Handler> unload;
        QVERIFY(opening->start(wallet, unload));
        QTRY_VERIFY(metadata_ready->available() > 0);
        const auto update = opening->take();
        QVERIFY(update && update->metadata);
        auto reader = std::make_shared<walletbalance::Reader>(core, utility, walletbalance::WalletRead(opening),
            [state] { ++state->notices; }, closed);
        QCOMPARE(reader->refresh(), execution::Rejection::None);
        QTRY_VERIFY(state->entered.available() > 0);
        QVERIFY(state->entered.tryAcquire());
        QVERIFY(state->read_thread.load() != qApp->thread());
        QVERIFY(!state->destroyed_thread.load());
        reader->close();
        reader.reset();
        const auto core_drained = std::make_shared<std::atomic_bool>(false);
        core->whenDrained([core_drained] { core_drained->store(true); });
        opening->stop(false, complete);
        QVERIFY(!stopped->load());
        QVERIFY(!core_drained->load());
        bool heartbeat{false};
        QMetaObject::invokeMethod(qApp, [&] { heartbeat = true; }, Qt::QueuedConnection);
        QTRY_VERIFY(heartbeat); // Must finish while the actual read is held.
        QVERIFY(!state->destroyed_thread.load());
        QVERIFY(!stopped->load());
        state->release.release();
        QTRY_VERIFY(core_drained->load());
        QTRY_VERIFY(stopped->load());
        executor.stopAndJoin();
        QVERIFY(state->destroyed_thread.load());
        QVERIFY(state->destroyed_thread.load() != qApp->thread());
        QCOMPARE(state->notices.load(), 0);
    }
    void expiredPublicInterfaceIsUnavailable()
    {
        auto read = walletbalance::WalletRead({});
        const auto result = read();
        QCOMPARE(result.status, Status::Unavailable);
        QVERIFY(!result.snapshot);
    }
};

BITCOINQML_REGISTER_QT_TEST(WalletBalanceAdapterTests)
#include <test/test_walletbalanceadapter.moc>
