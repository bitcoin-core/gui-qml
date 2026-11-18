// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/secretinput.h>
#include <qml/backendexecutor.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>
#include <QThread>
#include <QtTest/QtTest>
#include <atomic>

namespace {
class RecordingScheduler final : public execution::Scheduler {
    struct Task final : execution::Task {
        std::shared_ptr<execution::Task> inner; RecordingScheduler& recorder;
        Task(std::shared_ptr<execution::Task> value, RecordingScheduler& target) : inner(std::move(value)), recorder(target) {}
        void execute() override { inner->execute(); recorder.cleanup_thread = QThread::currentThread(); ++recorder.cleanups; }
        void deliver() override { inner->deliver(); }
        void notStarted(execution::NotStarted::Cause cause) override { inner->notStarted(cause); }
    };
    std::shared_ptr<execution::Scheduler> m_inner;
public:
    std::atomic<QThread*> cleanup_thread{nullptr}; std::atomic_int cleanups{0};
    explicit RecordingScheduler(std::shared_ptr<execution::Scheduler> inner) : m_inner(std::move(inner)) {}
    execution::SubmitResult submit(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override { return m_inner->submit(std::move(task), spec); }
    execution::SubmitResult reserveCleanup(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override { return m_inner->reserveCleanup(std::make_shared<Task>(std::move(task), *this), spec); }
    bool activateCleanup(execution::TaskId id) override { return m_inner->activateCleanup(id); }
    execution::Borrow borrow(execution::ResourceKey key) override { return m_inner->borrow(key); }
    execution::ResourceKey allocateKey() override { return m_inner->allocateKey(); }
    size_t pending() const override { return m_inner->pending(); }
    void stopAcceptingWork() override { m_inner->stopAcceptingWork(); }
    bool cancelQueued(execution::TaskId id) override { return m_inner->cancelQueued(id); }
};
}
class SecretInputTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void captureRetainsOriginalThroughManualNativeAndOutcome() {
        ManualRoutedExecution commands;
        QString original = QString(40'000, QChar{'p'}) + QString::fromUtf8("\xF0\x9F\x94\x91");
        const auto expected = original.toUtf8(); // Public fixture oracle, not a production secret conversion.
        auto captured = secretinput::Capture(commands.scheduler, original); QVERIFY(captured);
        QCOMPARE(captured.error, secretinput::Error::None); QVERIFY(captured.bytes > 32'768);
        original = "changed editor";
        bool native{false}, outcome{false}, match{false};
        QVERIFY(execution::Submit(*commands.scheduler, [input = captured.input, &native, &match, expected] {
            const auto secure = secretinput::Materialize(input); native = true;
            match = std::string(secure.begin(), secure.end()) == expected.toStdString(); return match;
        }, [&](bool value) { outcome = value; }, {}, {execution::Lane::Core}));
        QVERIFY(!native); QVERIFY(!outcome); QVERIFY(commands.step(execution::Lane::Core));
        QVERIFY(native); QVERIFY(match); QVERIFY(!outcome);
        QVERIFY(commands.step(execution::Lane::Domain)); QVERIFY(outcome);
        captured.input.reset(); commands.drain(); QVERIFY(commands.quiescent());
    }
    void emptyLiteralIsDistinctFromMissingAndNullIsTypedFailure() {
        ManualRoutedExecution commands;
        auto captured = secretinput::Capture(commands.scheduler, QString{}); QVERIFY(captured);
        bool empty{false};
        QVERIFY(execution::Submit(*commands.scheduler, [input = captured.input, &empty] {
            empty = secretinput::Materialize(input).empty();
        }, [] {}, {}, {execution::Lane::Core}));
        commands.drain(); QVERIFY(empty);
        try { secretinput::Materialize({}); QFAIL("Missing capture must fail before effects"); }
        catch (const secretinput::Failure& failure) { QCOMPARE(failure.error(), secretinput::Error::Unavailable); }
        captured.input.reset(); commands.drain(); QVERIFY(commands.quiescent());
    }
    void unicodeAndEmbeddedNullPreserveLegacyUtf8Bytes() {
        ManualRoutedExecution commands;
        QString literal = QStringLiteral("p\u00e4ss"); literal.append(QChar{0}); literal.append(QString::fromUtf8("\xF0\x9F\x94\x91"));
        auto captured = secretinput::Capture(commands.scheduler, literal); QVERIFY(captured);
        bool exact{false};
        QVERIFY(execution::Submit(*commands.scheduler, [input = captured.input, &exact] {
            const auto secret = secretinput::Materialize(input);
            exact = QByteArray(secret.data(), static_cast<qsizetype>(secret.size())).toHex() == QByteArray{"70c3a4737300f09f9491"};
        }, [] {}, {}, {execution::Lane::Core}));
        commands.drain(); QVERIFY(exact); captured.input.reset(); commands.drain(); QVERIFY(commands.quiescent());
    }
    void closedAdmissionRefusesWithoutMaterialization() {
        ManualRoutedExecution commands;
        commands.scheduler->close();
        const auto closed = secretinput::Capture(commands.scheduler, "password");
        QVERIFY(!closed); QCOMPARE(closed.error, secretinput::Error::Unavailable);
        commands.drain(); QVERIFY(commands.quiescent());
    }
    void realMaterializationAndRetirementUseSharedNativeWorkers() {
        BackendExecutor backend{execution::Options{}};
        auto scheduler = std::make_shared<RecordingScheduler>(backend.scheduler());
        auto captured = secretinput::Capture(scheduler, QString(40'000, QChar{'p'})); QVERIFY(captured);
        std::atomic<QThread*> native_thread{nullptr}; std::atomic_bool match{false}, done{false};
        QVERIFY(execution::Submit(*scheduler, [input = captured.input, &native_thread, &match] {
            native_thread = QThread::currentThread();
            const auto secret = secretinput::Materialize(input);
            match = secret == SecureString(40'000, 'p');
        }, [&] { done = true; }, {}, {execution::Lane::Core}));
        QTRY_VERIFY_WITH_TIMEOUT(done.load(), 5000); QVERIFY(match);
        QVERIFY(native_thread.load() != QThread::currentThread()); QCOMPARE(scheduler->cleanups.load(), 0);
        captured.input.reset();
        QTRY_COMPARE_WITH_TIMEOUT(scheduler->cleanups.load(), 1, 5000);
        QVERIFY(scheduler->cleanup_thread.load() != QThread::currentThread());
        backend.shutdown(); QTRY_VERIFY_WITH_TIMEOUT(backend.isDrained(), 5000);
    }
};
BITCOINQML_REGISTER_QT_TEST(SecretInputTests)
#include <test_secretinput.moc>
