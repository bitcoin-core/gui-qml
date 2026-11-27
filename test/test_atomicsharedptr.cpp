// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/atomicsharedptr.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <QTimer>
#include <atomic>
#include <thread>
#include <vector>

class AtomicSharedPtrTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void exchangeKeepsOriginalHeldViewAlive()
    {
        struct Value {
            int generation;
            std::shared_ptr<std::atomic_int> destroyed;
            ~Value() { ++*destroyed; }
        };
        auto destroyed = std::make_shared<std::atomic_int>(0);
        ownership::AtomicSharedPtr<const Value> slot{std::make_shared<Value>(1, destroyed)};
        auto held = slot.load(std::memory_order_acquire);
        std::weak_ptr<const Value> original{held};
        std::atomic_bool finished{false};
        std::atomic_int replaced_generation{0};
        std::thread publisher{[&] {
            auto previous = slot.exchange(std::make_shared<Value>(2, destroyed), std::memory_order_acq_rel);
            replaced_generation = previous->generation;
            previous.reset();
            finished.store(true, std::memory_order_release);
        }};
        // Join only after the worker has finished; keep Qt event processing live.
        bool heartbeat{false};QTimer::singleShot(0, this, [&] { heartbeat = true; });
        const bool completed = QTest::qWaitFor([&] { return heartbeat && finished.load(std::memory_order_acquire); });
        publisher.join();QVERIFY(completed);
        QCOMPARE(replaced_generation.load(), 1);QCOMPARE(slot.load()->generation, 2);
        QVERIFY(!original.expired());QCOMPARE(held->generation, 1);QCOMPARE(destroyed->load(), 0);
        held.reset();QVERIFY(original.expired());QCOMPARE(destroyed->load(), 1);
        slot.store({}, std::memory_order_release);QVERIFY(!slot.load(std::memory_order_acquire));QCOMPARE(destroyed->load(), 2);
    }
    void concurrentReadersObserveCompleteImmutablePublications()
    {
        struct Value { int generation;std::vector<int> rows; };
        ownership::AtomicSharedPtr<const Value> slot;
        std::atomic_bool finished{false}, coherent{true};
        std::atomic_int observations{0};
        std::thread reader{[&] {
            do {
                if (const auto value = slot.load(std::memory_order_acquire)) {
                    if (value->rows.size() != 128) coherent = false;
                    for (int entry : value->rows) if (entry != value->generation) coherent = false;
                    ++observations;
                }
            } while (!finished.load(std::memory_order_acquire));
        }};
        std::thread publisher{[&] {
            for (int generation = 1; generation <= 1024; ++generation) {
                auto value = std::make_shared<Value>();value->generation = generation;value->rows.assign(128, generation);
                slot.store(std::move(value), std::memory_order_release);
                if (generation == 1) while (observations.load() == 0) std::this_thread::yield();
                if (generation % 16 == 0) std::this_thread::yield();
            }
            finished.store(true, std::memory_order_release);
        }};
        const bool completed = QTest::qWaitFor([&] { return finished.load(std::memory_order_acquire); });
        publisher.join();reader.join();QVERIFY(completed);
        QVERIFY(coherent.load());QVERIFY(observations.load() > 0);
        const auto final = slot.load(std::memory_order_acquire);QVERIFY(final);QCOMPARE(final->generation, 1024);
        QCOMPARE(final->rows, std::vector<int>(128, 1024));
    }
};
#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(AtomicSharedPtrTests)
#else
QTEST_MAIN(AtomicSharedPtrTests)
#endif
#include <test_atomicsharedptr.moc>
