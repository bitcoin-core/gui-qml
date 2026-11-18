// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/guipresentation.h>
#include <qml/retainedvalue.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>

#include <QPointer>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTimer>
#include <QtTest/QtTest>

#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

class GuiPresentationTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void nativeLastOwnerQueuesGuiHandoff_data()
    {
        QTest::addColumn<bool>("queued_work");
        QTest::newRow("plain") << false;
        QTest::newRow("queued-callback") << true;
    }
    void nativeLastOwnerQueuesGuiHandoff()
    {
        QFETCH(bool, queued_work);
        QObject receiver;
        std::vector<int> events;
        QThread* destroyed_on{nullptr};
        const auto* gui = QThread::currentThread();
        auto owner = presentation::MakeGuiObject();
        const QPointer<QObject> original{owner.get()};
        connect(owner.get(), &QObject::destroyed, &receiver, [&] {
            destroyed_on = QThread::currentThread(); events.push_back(2);
        }, Qt::DirectConnection);
        QSemaphore native_done;
        std::thread native;
        const auto cleanup = qScopeGuard([&] {
            if (native.joinable()) native.join();
            presentation::FlushRetirements();
        });
        if (queued_work) {
            QVERIFY(QMetaObject::invokeMethod(owner.get(), [&] { events.push_back(1); }, Qt::QueuedConnection));
        }
        native = std::thread{[owner = std::move(owner), &native_done]() mutable {
            owner.reset(); native_done.release();
        }};
        // Deliberately hold GUI delivery until the bounded Native producer has
        // finished, rather than letting qWaitFor consume the handoff early.
        QVERIFY(native_done.tryAcquire(1, 5000));
        native.join();
        QVERIFY(original);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(original); // Native must not have called deleteLater itself.
        QVERIFY(events.empty());
        presentation::FlushRetirements(); // Registry can already be drained.
        QVERIFY(!original);
        QCOMPARE(destroyed_on, gui);
        QCOMPARE(events, queued_work ? (std::vector<int>{1, 2}) : (std::vector<int>{2}));
    }
    void queuedPostKeepsAcceptedCallbackAfterNativeRelease()
    {
        int delivered{0};
        QThread* delivered_on{nullptr};
        auto post = presentation::QueuedPost();
        QSemaphore native_done;
        std::thread native;
        const auto cleanup = qScopeGuard([&] {
            if (native.joinable()) native.join();
            presentation::FlushRetirements();
        });
        native = std::thread{[post = std::move(post), &native_done, &delivered, &delivered_on]() mutable {
            post([&] { ++delivered; delivered_on = QThread::currentThread(); });
            post = {}; native_done.release();
        }};
        QVERIFY(native_done.tryAcquire(1, 5000));
        native.join();
        QCOMPARE(delivered, 0);
        presentation::FlushRetirements();
        QCOMPARE(delivered, 1);
        QCOMPARE(delivered_on, QThread::currentThread());
        presentation::FlushRetirements();
        QCOMPARE(delivered, 1);
    }
    void reliablePostRetiresOriginalOutcomeWhenPrimaryThrows_data()
    {
        QTest::addColumn<bool>("enqueue_first");
        QTest::newRow("throw-before-enqueue") << false;
        QTest::newRow("enqueue-then-throw") << true;
    }
    void reliablePostRetiresOriginalOutcomeWhenPrimaryThrows()
    {
        QFETCH(bool, enqueue_first);
        struct Observed {
            int effects{0}, delivered{0}, value{0};
            QThread* delivered_on{nullptr};
        };
        const auto observed = std::make_shared<Observed>();
        const auto primary = std::make_shared<std::function<void()>>();
        ManualRoutedExecution graph;
        auto post = presentation::ReliablePost([primary, enqueue_first](auto callback) {
            if (enqueue_first) *primary = std::move(callback);
            throw std::runtime_error("primary presentation transport refused");
        });
        const auto cleanup = qScopeGuard([&] {
            graph.scheduler->closeAdmission();
            graph.drain();
            *primary = {};
            post = {};
            if (!graph.quiescent()) {
                QTest::qFail("The original presentation task graph did not drain", __FILE__, __LINE__);
                return;
            }
            presentation::FlushRetirements();
        });
        int input{50'000};
        const auto admitted = execution::Submit(*graph.scheduler,
            [observed, original = input] { ++observed->effects; return original; },
            [post, observed](int value) {
                post([observed, value] {
                    ++observed->delivered;
                    observed->value = value;
                    observed->delivered_on = QThread::currentThread();
                });
            }, {}, {execution::Lane::Core});
        QVERIFY(admitted);
        input = 60'000;
        const auto effect = graph.dispatchNext(execution::Lane::Core);
        QVERIFY(effect && *effect == admitted.id);
        QVERIFY(graph.finishWork(*effect));
        QCOMPARE(observed->effects, 1);
        QCOMPARE(observed->delivered, 0);
        QCOMPARE(graph.scheduler->phase(*effect), std::make_optional(execution::Phase::OutcomeReady));
        QVERIFY(graph.step(execution::Lane::Domain));
        // A throwing primary cannot escape TaskQueue::deliver and strand its
        // actual result. The queued GUI handoff is independently owned.
        QVERIFY(!graph.scheduler->phase(*effect));
        QCOMPARE(graph.scheduler->pending(), size_t{0});
        QVERIFY(graph.quiescent());
        QCOMPARE(observed->delivered, 0);
        QCOMPARE(bool(*primary), enqueue_first);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(observed->delivered, 1);
        QCOMPARE(observed->value, 50'000);
        QCOMPARE(input, 60'000);
        QCOMPARE(observed->delivered_on, QThread::currentThread());
        if (*primary) {
            (*primary)(); // The primary enqueued before throwing: late duplicate.
            *primary = {};
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(observed->delivered, 1);
        QCOMPARE(observed->value, 50'000);
        QCOMPARE(observed->effects, 1);
        QVERIFY(graph.quiescent());
    }
    void retirementKeepsRealGuiHeartbeat()
    {
        struct NativeState {
            QSemaphore entered, release;
            std::atomic_bool finished{false};
        };
        const auto state = std::make_shared<NativeState>();
        int destroyed{0}, heartbeat{0};
        bool released{false}, wrong_affinity{false};
        QTimer pulse;
        const auto* gui = QThread::currentThread();
        struct NativeOwners {
            std::shared_ptr<NativeState> state;
            std::vector<std::shared_ptr<QObject>> objects;
            explicit NativeOwners(std::shared_ptr<NativeState> original) : state(std::move(original)) {}
            ~NativeOwners()
            {
                state->entered.release(); state->release.acquire();
                objects.clear(); state->finished.store(true);
            }
        };
        BackendExecutor executor;
        auto owners = execution::RetainValues<NativeOwners>(executor.scheduler(), [&] {
            return std::make_unique<NativeOwners>(state);
        });
        const auto cleanup = qScopeGuard([&] {
            pulse.stop(); state->release.release(); owners.reset(); executor.shutdown();
            if (!QTest::qWaitFor([&] { return executor.isDrained(); }, 5000)) {
                QTest::qFail("The original Native retirement executor did not drain", __FILE__, __LINE__);
                return;
            }
            presentation::FlushRetirements();
        });
        QVERIFY(owners);
        owners->objects.reserve(32);
        for (int row = 0; row < 32; ++row) {
            auto object = presentation::MakeGuiObject();
            connect(object.get(), &QObject::destroyed, &pulse, [&] {
                ++destroyed;
                wrong_affinity |= QThread::currentThread() != gui;
            }, Qt::DirectConnection);
            owners->objects.push_back(std::move(object));
        }
        connect(&pulse, &QTimer::timeout, &pulse, [&] {
            ++heartbeat;
            if (!released && state->entered.tryAcquire()) { released = true; state->release.release(); }
        });
        pulse.start(0);
        owners.reset(); executor.shutdown();
        QVERIFY(QTest::qWaitFor([&] { return executor.isDrained(); }, 5000));
        pulse.stop();
        QVERIFY(heartbeat > 0); QVERIFY(released); QVERIFY(state->finished.load());
        QCOMPARE(executor.scheduler()->pending(), size_t{0});
        presentation::FlushRetirements();
        QCOMPARE(destroyed, 32);
        QVERIFY(!wrong_affinity);
    }
};

BITCOINQML_REGISTER_QT_TEST(GuiPresentationTests)
#include <test_guipresentation.moc>
