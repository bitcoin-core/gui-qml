// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/startupeffects.h>
#include <qml/quithandler.h>
#include <test/mocks/manualstartupexecution.h>
#include <test/qt_test_registry.h>
#include <QTimer>
#include <QtTest/QtTest>
#include <atomic>
#include <deque>
#include <optional>
#include <stdexcept>

class StartupEffectsTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void missingSchedulerFailsBeforeAnyBorrowedWork() {
        bool ran{false};
        bool rejected{false};
        try {
            (void)startup::Run(nullptr, nullptr, [&] { ran = true; return 1; });
        } catch (const std::invalid_argument&) {
            rejected = true;
        } catch (...) {
            QFAIL("Missing scheduler must throw std::invalid_argument");
        }
        QVERIFY(rejected);
        QVERIFY(!ran);
    }
    void manualAdmissionFailureNeverStartsBorrowedWork_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("transport-refuses") << 0;
        QTest::newRow("ordinary-submitted-closed") << 1;
    }
    void manualAdmissionFailureNeverStartsBorrowedWork() {
        QFETCH(int, mode);
        ManualStartupExecution environment;
        if (mode == 0) environment.reject_core = true;
        if (mode == 1) environment.core.close();
        int effects{0}, publications{0}; QString failure;
        const QString borrowed = QString::fromUtf8("original-startup-\xc3\xa9");
        try {
            (void)startup::Run(&environment.core, &environment.outcomes, [&] {
                ++effects; return QString{borrowed};
            }, [&](auto) { ++publications; });
        } catch (const std::runtime_error& error) { failure = QString::fromUtf8(error.what()); }
        QCOMPARE(failure, QString{"Startup work could not be queued"});
        QCOMPARE(effects, 0); QCOMPARE(publications, 0);
        environment.drain(); QVERIFY(environment.quiescent());
    }
    void quitAndAdmissionClosureCannotReplaceAcceptedOutcome_data() {
        QTest::addColumn<bool>("quit_before_effect"); QTest::addColumn<bool>("failure");
        QTest::newRow("quit-before-success") << true << false;
        QTest::newRow("quit-before-native-error") << true << true;
        QTest::newRow("quit-after-success-before-domain") << false << false;
        QTest::newRow("quit-after-native-error-before-domain") << false << true;
    }
    void quitAndAdmissionClosureCannotReplaceAcceptedOutcome() {
        QFETCH(bool, quit_before_effect); QFETCH(bool, failure);
        ManualStartupExecution environment;
        QmlQuitHandler quit_handler;
        std::deque<std::function<void()>> publications;
        const QString borrowed = QString::fromUtf8("borrowed-startup-\xe2\x82\xac");
        QString observed, returned, error;
        int effects{0}, notices{0}, stage{0};
        bool finished{false};
        const auto quit = [&] {
            QEvent event{QEvent::Quit};
            QCoreApplication::sendEvent(QCoreApplication::instance(), &event);
            QVERIFY(quit_handler.isQuitRequested());
            environment.core.close();
        };
        // A zero-delay event only enters each explicitly controlled phase. No
        // elapsed duration establishes effect/quit/outcome/publication order.
        QTimer driver;
        connect(&driver, &QTimer::timeout, this, [&] {
            if (stage == 0) {
                ++stage; QVERIFY(!finished); QCOMPARE(effects, 0);
                if (quit_before_effect) quit();
                QVERIFY(environment.step(execution::Lane::Core));
                QCOMPARE(effects, 1); QCOMPARE(observed, borrowed);
                QVERIFY(publications.empty()); QVERIFY(!finished);
                if (!quit_before_effect) quit();
            } else if (stage == 1) {
                ++stage; QVERIFY(!finished);
                QVERIFY(environment.step(execution::Lane::Utility));
                QCOMPARE(publications.size(), size_t{1}); QVERIFY(!finished);
            } else if (stage == 2) {
                ++stage; driver.stop();
                auto wake = std::move(publications.front()); publications.pop_front(); wake();
            }
        });
        driver.start(0);
        try {
            returned = startup::Run(&environment.core, &environment.outcomes, [&] {
                ++effects; observed = borrowed;
                if (failure) throw std::runtime_error{"original accepted startup error"};
                return QString{borrowed};
            }, [&](auto wake) {
                ++notices; publications.push_back(std::move(wake));
            });
        } catch (const std::runtime_error& exception) { error = QString::fromUtf8(exception.what()); }
        finished = true; driver.stop();
        QCOMPARE(stage, 3); QCOMPARE(effects, 1); QCOMPARE(notices, 1);
        QCOMPARE(error, failure ? QString{"original accepted startup error"} : QString{});
        QCOMPARE(returned, failure ? QString{} : borrowed);
        QVERIFY(quit_handler.isQuitRequested()); QVERIFY(publications.empty());
        environment.drain(); QVERIFY(environment.quiescent());
    }
    void throwingPrimaryPostCannotStrandAnActualDomainOutcome_data() {
        QTest::addColumn<bool>("failure"); QTest::newRow("success") << false; QTest::newRow("native-exception") << true;
    }
    void throwingPrimaryPostCannotStrandAnActualDomainOutcome() {
        QFETCH(bool, failure);
        execution::Executor executor{execution::Executor::Config{}};
        auto core = executor.makeQueue(execution::Lane::Core);
        auto outcomes = executor.makeQueue(execution::Lane::Utility);
        std::atomic<QThread*> native{nullptr}; std::atomic<int> effects{0}, posts{0};
        const auto post = [&](auto) { ++posts; throw std::runtime_error("Primary presentation post rejected"); };
        bool failed{false};
        try {
            const auto result = startup::Run(&core, &outcomes, [&] {
                native = QThread::currentThread(); ++effects;
                if (failure) throw std::runtime_error("original startup failure");
                return 42;
            }, post);
            QCOMPARE(result, 42);
        } catch (const std::runtime_error& error) {
            failed = true; QCOMPARE(QString::fromUtf8(error.what()), QStringLiteral("original startup failure"));
        }
        QCOMPARE(failed, failure); QCOMPARE(effects.load(), 1); QCOMPARE(posts.load(), 1);
        QVERIFY(native.load() != nullptr && native.load() != QThread::currentThread());
        executor.stopAndJoin();
    }
    void manualFallbackAndExpiredPrimaryWakeKeepTheActualOutcome_data() {
        QTest::addColumn<bool>("enqueued_then_threw"); QTest::addColumn<bool>("failure");
        QTest::newRow("empty-primary-success") << false << false;
        QTest::newRow("empty-primary-error") << false << true;
        QTest::newRow("retained-primary-success") << true << false;
        QTest::newRow("retained-primary-error") << true << true;
    }
    void manualFallbackAndExpiredPrimaryWakeKeepTheActualOutcome() {
        QFETCH(bool, enqueued_then_threw); QFETCH(bool, failure);
        ManualStartupExecution environment; std::deque<std::function<void()>> retained;
        int effects{0}, posts{0}, stage{0}; QString returned, error;
        const QString borrowed = QString::fromUtf8("fallback-startup-\xe2\x82\xac");
        presentation::Post post;
        if (enqueued_then_threw) post = [&](auto wake) {
            ++posts; retained.push_back(std::move(wake));
            throw std::runtime_error("primary retained its wake before rejection");
        };
        QTimer driver;
        connect(&driver, &QTimer::timeout, this, [&] {
            if (stage == 0) {
                ++stage; QCOMPARE(effects, 0); QVERIFY(environment.step(execution::Lane::Core));
                QCOMPARE(effects, 1); QCOMPARE(posts, 0);
            } else if (stage == 1) {
                ++stage; driver.stop(); QVERIFY(environment.step(execution::Lane::Utility));
                QCOMPARE(posts, enqueued_then_threw ? 1 : 0);
            }
        });
        driver.start(0);
        try {
            returned = startup::Run(&environment.core, &environment.outcomes, [&] {
                ++effects;
                if (failure) throw std::runtime_error("actual retained startup failure");
                return QString{borrowed};
            }, post);
        } catch (const std::runtime_error& exception) { error = QString::fromUtf8(exception.what()); }
        driver.stop(); QCOMPARE(stage, 2); QCOMPARE(effects, 1);
        QCOMPARE(returned, failure ? QString{} : borrowed);
        QCOMPARE(error, failure ? QStringLiteral("actual retained startup failure") : QString{});
        QCOMPARE(retained.size(), enqueued_then_threw ? size_t{1} : size_t{0});
        // The fallback completed the phase. Its independently retained primary
        // wake now observes the destroyed QEventLoop and cannot run work again.
        for (const auto& wake : retained) { wake(); wake(); }
        retained.clear(); QCOMPARE(effects, 1); QCOMPARE(posts, enqueued_then_threw ? 1 : 0);
        environment.drain(); QVERIFY(environment.quiescent());
    }
    void manualEmptyDomainValueIsStillAnOwnedOutcome_data() {
        QTest::addColumn<bool>("empty"); QTest::newRow("empty-optional") << true; QTest::newRow("populated-optional") << false;
    }
    void manualEmptyDomainValueIsStillAnOwnedOutcome() {
        QFETCH(bool, empty);
        ManualStartupExecution environment; int stage{0}, effects{0};
        QTimer driver;
        connect(&driver, &QTimer::timeout, this, [&] {
            if (stage == 0) { ++stage; QVERIFY(environment.step(execution::Lane::Core)); QCOMPARE(effects, 1); }
            else if (stage == 1) { ++stage; driver.stop(); QVERIFY(environment.step(execution::Lane::Utility)); }
        });
        driver.start(0);
        const auto result = startup::Run(&environment.core, &environment.outcomes, [&]() -> std::optional<QString> {
            ++effects;
            if (empty) return std::nullopt;
            return QStringLiteral("owned value");
        });
        driver.stop(); QCOMPARE(stage, 2); QCOMPARE(effects, 1); QCOMPARE(result.has_value(), !empty);
        if (result) QCOMPARE(*result, QStringLiteral("owned value"));
        environment.drain(); QVERIFY(environment.quiescent());
    }
    void closedUtilityQueuePreservesAcceptedStartupOutcome_data() {
        QTest::addColumn<bool>("failure");
        QTest::newRow("success") << false;
        QTest::newRow("native-exception") << true;
    }
    void closedUtilityQueuePreservesAcceptedStartupOutcome() {
        QFETCH(bool, failure);
        ManualStartupExecution environment;
        int effects{0};
        QTimer::singleShot(0, this, [&] {
            environment.outcomes.close();
            QVERIFY(environment.step(execution::Lane::Core));
        });
        QString error;
        int value{0};
        try {
            value = startup::Run(&environment.core, &environment.outcomes, [&] {
                ++effects;
                if (failure) throw std::runtime_error{"original startup error"};
                return 42;
            });
        } catch (const std::runtime_error& exception) { error = QString::fromUtf8(exception.what()); }
        QCOMPARE(effects, 1);
        QCOMPARE(value, failure ? 0 : 42);
        QCOMPARE(error, failure ? QStringLiteral("original startup error") : QString{});
        QVERIFY(environment.quiescent());
    }
};
BITCOINQML_REGISTER_QT_TEST(StartupEffectsTests)
#include <test_startupeffects.moc>
