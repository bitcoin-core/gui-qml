// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/executor.h>
#include <qml/retainedvalue.h>
#include <qml/secretinput.h>
#include <test/mocks/inputwork.h>
#include <test/qt_test_registry.h>
#include <QScopeGuard>
#include <QThread>
#include <QtTest/QtTest>
#include <atomic>

class SecretInputTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void captureRetainsOriginalThroughManualNativeAndOutcome() {
        InputWork commands;
        QString original = QString(40'000, QChar{'p'}) + QString::fromUtf8("\xF0\x9F\x94\x91");
        const auto expected = original.toUtf8(); // Public fixture oracle, not a production secret conversion.
        auto captured = secretinput::Capture(commands.hold(), original); QVERIFY(captured);
        QCOMPARE(captured.error, secretinput::Error::None); QVERIFY(captured.bytes > 32'768);
        original = "changed editor";
        bool native{false}, outcome{false}, match{false};
        const auto finish = qScopeGuard([&] { commands.worker.drain(); });
        QVERIFY(commands.worker.core.post([&, input = captured.input] {
            const auto secure = secretinput::Materialize(input);
            native = true;
            match = std::string(secure.begin(), secure.end()) == expected.toStdString();
            commands.worker.outcomes.post([&] { outcome = match; });
        }));
        QVERIFY(!native); QVERIFY(!outcome); QVERIFY(commands.worker.step(execution::Lane::Core));
        QVERIFY(native); QVERIFY(match); QVERIFY(!outcome);
        QVERIFY(commands.worker.step(execution::Lane::Utility)); QVERIFY(outcome);
        captured.input.reset(); commands.worker.drain(); QVERIFY(commands.worker.quiescent());
    }
    void emptyLiteralIsDistinctFromMissingAndNullIsTypedFailure() {
        InputWork commands;
        auto captured = secretinput::Capture(commands.hold(), QString{}); QVERIFY(captured);
        bool empty{false};
        QVERIFY(commands.worker.core.post([input = captured.input, &empty] {
            empty = secretinput::Materialize(input).empty();
        }));
        commands.worker.drain(); QVERIFY(empty);
        try { secretinput::Materialize({}); QFAIL("Missing capture must fail before effects"); }
        catch (const secretinput::Failure& failure) { QCOMPARE(failure.error(), secretinput::Error::Unavailable); }
        captured.input.reset(); commands.worker.drain(); QVERIFY(commands.worker.quiescent());
    }
    void unicodeAndEmbeddedNullPreserveLegacyUtf8Bytes() {
        InputWork commands;
        QString literal = QStringLiteral("p\u00e4ss"); literal.append(QChar{0}); literal.append(QString::fromUtf8("\xF0\x9F\x94\x91"));
        auto captured = secretinput::Capture(commands.hold(), literal); QVERIFY(captured);
        bool exact{false};
        QVERIFY(commands.worker.core.post([input = captured.input, &exact] {
            const auto secret = secretinput::Materialize(input);
            exact = QByteArray(secret.data(), static_cast<qsizetype>(secret.size())).toHex() == QByteArray{"70c3a4737300f09f9491"};
        }));
        commands.worker.drain(); QVERIFY(exact); captured.input.reset(); commands.worker.drain(); QVERIFY(commands.worker.quiescent());
    }
    void closedAdmissionRefusesWithoutMaterialization() {
        InputWork commands;
        commands.cleanup->close([] {});
        const auto closed = secretinput::Capture(commands.hold(), "password");
        QVERIFY(!closed); QCOMPARE(closed.error, secretinput::Error::Unavailable);
        commands.worker.drain(); QVERIFY(commands.worker.quiescent());
    }
    void realMaterializationAndRetirementUseSharedNativeWorkers() {
        execution::Executor executor{execution::Executor::Config{}};
        auto native = executor.makeQueue(execution::Lane::Core);
        auto utility = executor.makeQueue(execution::Lane::Utility);
        const auto cleanup = std::make_shared<execution::ValueCleanupQueue>(
            std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility)));
        std::atomic<QThread*> native_thread{nullptr}, cleanup_thread{nullptr};
        std::atomic_int cleanups{0};
        std::atomic_bool match{false}, done{false};
        const execution::ValueCleanup hold = [&](auto work) {
            return cleanup->hold([&, work = std::move(work)] {
                work();
                cleanup_thread = QThread::currentThread();
                ++cleanups;
            });
        };
        auto captured = secretinput::Capture(hold, QString(40'000, QChar{'p'}));
        const auto join = qScopeGuard([&] {
            captured.input.reset();
            QSemaphore retired;
            cleanup->close([&] { retired.release(); });
            retired.acquire();
            executor.stopAndJoin();
        });
        QVERIFY(captured);
        QVERIFY(native.post([&, input = captured.input] {
            native_thread = QThread::currentThread();
            const auto secret = secretinput::Materialize(input);
            match = secret == SecureString(40'000, 'p');
            utility.post([&] { done = true; });
        }));
        QTRY_VERIFY_WITH_TIMEOUT(done.load(), 5000); QVERIFY(match);
        QVERIFY(native_thread.load() != QThread::currentThread()); QCOMPARE(cleanups.load(), 0);
        captured.input.reset();
        QTRY_COMPARE_WITH_TIMEOUT(cleanups.load(), 1, 5000);
        QVERIFY(cleanup_thread.load() != QThread::currentThread());
    }
};
BITCOINQML_REGISTER_QT_TEST(SecretInputTests)
#include <test_secretinput.moc>
