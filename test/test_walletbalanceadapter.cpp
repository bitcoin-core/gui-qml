// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/walletbalanceadapter.h>
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
        const auto release_on_exit = qScopeGuard([&] { state->release.release(); });
        auto wallet = std::make_shared<Wallet>(state);
        BackendExecutor executor;
        const auto closed = std::make_shared<std::atomic_bool>(false);
        auto reader = std::make_shared<walletbalance::Reader>(executor.scheduler(), walletbalance::WalletRead(wallet),
            [state] { ++state->notices; }, closed);
        QCOMPARE(reader->refresh(), execution::Rejection::None);
        QTRY_VERIFY(state->entered.available() > 0);
        QVERIFY(state->entered.tryAcquire());
        QVERIFY(state->read_thread.load() != qApp->thread());
        wallet.reset();
        QVERIFY(!state->destroyed_thread.load());
        reader->close();
        reader.reset();
        executor.shutdown();
        QVERIFY(!executor.isDrained());
        bool heartbeat{false};
        QMetaObject::invokeMethod(qApp, [&] { heartbeat = true; }, Qt::QueuedConnection);
        QTRY_VERIFY(heartbeat); // Must finish while the actual read is held.
        QVERIFY(!state->destroyed_thread.load());
        state->release.release();
        QTRY_VERIFY(executor.isDrained());
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
