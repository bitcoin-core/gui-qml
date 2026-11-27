// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/qtlogsink.h>
#include <qml/guipresentation.h>
#include <test/qt_test_registry.h>
#include <QSemaphore>
#include <QScopeGuard>
#include <QTimer>
#include <QtTest/QtTest>
#include <atomic>
#include <stdexcept>
#include <vector>
#include <utility>

namespace {
struct WriterProbe {
    QStringList trace, debug, info, classification_inputs;
    QByteArray stderr_bytes;
    bool short_write{false}, flush_ok{true}, benign{false};
    int classifications{0};
    qtlog::WriterPorts ports() {
        return {
            [this](const QByteArray& text) { trace << "write"; stderr_bytes = text; return static_cast<size_t>(text.size()) - (short_write ? 1 : 0); },
            [this] { trace << "flush"; return flush_ok; },
            [this](const QString& text) { trace << "debug"; debug << text; },
            [this](const QString& text) { trace << "info"; info << text; },
            [this](const QString& text) { ++classifications; classification_inputs << text; return benign; }};
    }
};
struct NativeWriteProbe {
    std::atomic<int> writes{0}, posts{0}, callbacks{0};
    std::atomic_bool off_gui{true};
    std::vector<QString> messages;
};
}

class QtLogSinkTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void writerRoutingUsesActualStageSeverityAndOverflow_data() {
        QTest::addColumn<int>("type"); QTest::addColumn<bool>("startup"); QTest::addColumn<bool>("overflow");
        QTest::addColumn<bool>("benign"); QTest::addColumn<QString>("destination"); QTest::addColumn<int>("classifications");
        QTest::newRow("main-debug") << int(QtDebugMsg) << false << false << false << QString{"debug"} << 0;
        QTest::newRow("main-benign-warning") << int(QtWarningMsg) << false << false << true << QString{"debug"} << 1;
        QTest::newRow("main-warning") << int(QtWarningMsg) << false << false << false << QString{"info"} << 1;
        QTest::newRow("main-info") << int(QtInfoMsg) << false << false << false << QString{"info"} << 0;
        QTest::newRow("main-critical") << int(QtCriticalMsg) << false << false << false << QString{"info"} << 0;
        QTest::newRow("startup-debug") << int(QtDebugMsg) << true << false << false << QString{"debug"} << 0;
        QTest::newRow("startup-info") << int(QtInfoMsg) << true << false << false << QString{"stderr"} << 0;
        QTest::newRow("startup-warning") << int(QtWarningMsg) << true << false << true << QString{"stderr"} << 0;
        QTest::newRow("startup-critical") << int(QtCriticalMsg) << true << false << false << QString{"stderr"} << 0;
        QTest::newRow("main-overflow") << int(QtWarningMsg) << false << true << true << QString{"stderr"} << 0;
    }
    void writerRoutingUsesActualStageSeverityAndOverflow() {
        QFETCH(int, type); QFETCH(bool, startup); QFETCH(bool, overflow); QFETCH(bool, benign);
        QFETCH(QString, destination); QFETCH(int, classifications);
        WriterProbe probe; probe.benign = benign; qtlog::Writer writer{probe.ports()};
        qtlog::Record record; record.type = static_cast<QtMsgType>(type);
        record.stage = startup ? qtlog::Stage::Startup : qtlog::Stage::Main;
        record.message = QString::fromUtf8("original Native diagnostic-\xe2\x82\xac");
        record.overflow = overflow; record.rejected = 3; record.rejected_bytes = 246;
        QVERIFY(writer(record)); QCOMPARE(probe.classifications, classifications);
        if (destination == "stderr") {
            QCOMPARE(probe.trace, QStringList({"write", "flush"})); QVERIFY(probe.debug.empty()); QVERIFY(probe.info.empty());
            const auto message = overflow ? QString{"Qt log queue overflow: 3 messages (246 UTF-16 bytes) were rejected."} : record.message;
            QCOMPARE(probe.stderr_bytes, qFormatLogMessage(record.type, {nullptr, 0, nullptr, nullptr}, message).toUtf8() + '\n');
        } else if (destination == "debug") {
            QCOMPARE(probe.trace, QStringList{"debug"}); QCOMPARE(probe.debug, QStringList{record.message});
            QVERIFY(probe.info.empty()); QVERIFY(probe.stderr_bytes.isEmpty());
        } else {
            QCOMPARE(probe.trace, QStringList{"info"}); QCOMPARE(probe.info, QStringList{record.message});
            QVERIFY(probe.debug.empty()); QVERIFY(probe.stderr_bytes.isEmpty());
        }
    }
    void stderrAcknowledgementOwnsBothWriteAndFlush_data() {
        QTest::addColumn<bool>("short_write"); QTest::addColumn<bool>("flush_ok");
        QTest::newRow("complete") << false << true; QTest::newRow("short-write") << true << true;
        QTest::newRow("failed-flush") << false << false; QTest::newRow("both-fail") << true << false;
    }
    void writerRoutesTheDecoratedOwnedMessage() {
        WriterProbe probe; probe.benign = true; qtlog::Writer writer{probe.ports()};
        qtlog::Record record; record.stage = qtlog::Stage::Main; record.type = QtWarningMsg;
        record.message = "original"; record.context_truncated = true;
        QVERIFY(writer(record));
        const QStringList expected{"original [Qt log context truncated]"};
        QCOMPARE(probe.classification_inputs, expected); QCOMPARE(probe.debug, expected);
        QCOMPARE(probe.trace, QStringList{"debug"});
    }
    void stderrAcknowledgementOwnsBothWriteAndFlush() {
        QFETCH(bool, short_write); QFETCH(bool, flush_ok);
        WriterProbe probe; probe.short_write = short_write; probe.flush_ok = flush_ok;
        qtlog::Writer writer{probe.ports()};
        qtlog::Queue queue{[] {}, writer, {4, 32768}};
        QVERIFY(queue.enqueue(QtWarningMsg, {}, "actual startup error", qtlog::Stage::Startup));
        queue.close(); queue.drain();
        const bool acknowledged = !short_write && flush_ok;
        QCOMPARE(probe.trace, QStringList({"write", "flush"})); QCOMPARE(probe.classifications, 0);
        QCOMPARE(queue.receipt().accepted, quint64{1});
        QCOMPARE(queue.receipt().written, acknowledged ? quint64{1} : quint64{0});
        QCOMPARE(queue.receipt().failed, acknowledged ? quint64{0} : quint64{1}); QVERIFY(queue.receipt().closed);
        QCOMPARE(qtlog::FinalExitStatus(0, queue.receipt()), acknowledged ? 0 : 1);
        QCOMPARE(qtlog::FinalExitStatus(19, queue.receipt()), 19);
    }
    void missingWriterPortCannotProduceAnAcknowledgement_data() {
        QTest::addColumn<int>("port");
        for (int n = 0; n < 5; ++n) QTest::newRow(qPrintable(QString::number(n))) << n;
    }
    void missingWriterPortCannotProduceAnAcknowledgement() {
        QFETCH(int, port); WriterProbe probe; auto ports = probe.ports();
        if (port == 0) ports.stderr_write = {};
        if (port == 1) ports.stderr_flush = {};
        if (port == 2) ports.core_debug = {};
        if (port == 3) ports.core_info = {};
        if (port == 4) ports.benign_font_warning = {};
        QString error;
        try { qtlog::Writer writer{std::move(ports)}; }
        catch (const std::invalid_argument& exception) { error = QString::fromUtf8(exception.what()); }
        QCOMPARE(error, QString{"Missing Qt log writer port"}); QVERIFY(probe.trace.empty());
    }
    void invalidQueuePortsAndBudgetsFailBeforeEffects_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("missing-wake") << 0; QTest::newRow("missing-write") << 1;
        QTest::newRow("no-message-capacity") << 2; QTest::newRow("record-does-not-fit") << 3;
    }
    void invalidQueuePortsAndBudgetsFailBeforeEffects() {
        QFETCH(int, mode);
        int wakes{0}, writes{0};
        qtlog::Queue::Wake wake = [&] { ++wakes; };
        qtlog::Queue::Write write = [&](const auto&) { ++writes; return true; };
        auto limits = qtlog::Queue::Limits{4, 32768};
        if (mode == 0) wake = {};
        if (mode == 1) write = {};
        if (mode == 2) limits.messages = 0;
        if (mode == 3) limits.bytes = sizeof(qtlog::Record) - 1;
        QString failure;
        try { qtlog::Queue queue{std::move(wake), std::move(write), limits}; }
        catch (const std::invalid_argument& error) { failure = QString::fromUtf8(error.what()); }
        QCOMPARE(failure, QString{"Invalid Qt log ports or capacity"});
        QCOMPARE(wakes, 0); QCOMPARE(writes, 0);
    }
    void nonFatalForwardRetainsExactOwnedContext_data() {
        QTest::addColumn<int>("type"); QTest::addColumn<bool>("startup"); QTest::addColumn<bool>("policy");
        for (bool startup : {false, true}) {
            const QByteArray prefix = startup ? "startup-" : "main-";
            QTest::newRow(QByteArray(prefix + "debug").constData()) << int(QtDebugMsg) << startup << true;
            QTest::newRow(QByteArray(prefix + "info").constData()) << int(QtInfoMsg) << startup << true;
            QTest::newRow(QByteArray(prefix + "warning").constData()) << int(QtWarningMsg) << startup << true;
            QTest::newRow(QByteArray(prefix + "critical").constData()) << int(QtCriticalMsg) << startup << true;
        }
        QTest::newRow("startup-without-policy") << int(QtWarningMsg) << true << false;
        QTest::newRow("main-without-policy") << int(QtDebugMsg) << false << false;
    }
    void nonFatalForwardRetainsExactOwnedContext() {
        QFETCH(int, type); QFETCH(bool, startup); QFETCH(bool, policy);
        int wakes{0}; std::vector<qtlog::Record> writes;
        auto queue = std::make_shared<qtlog::Queue>([&] { ++wakes; },
            [&](const auto& record) { writes.push_back(record); return true; }, qtlog::Queue::Limits{4, 32768});
        auto fatal = policy ? std::make_shared<qtlog::FatalPolicy>(2, 2) : nullptr;
        QByteArray file{"emitter.cpp"}, function{"originalFunction"}, category{"startup.category"};
        QString message = QString::fromUtf8("original diagnostic-\xe2\x82\xac");
        const auto expected = message;
        const auto stage = startup ? qtlog::Stage::Startup : qtlog::Stage::Main;
        qtlog::Forward(static_cast<QtMsgType>(type),
            {file.constData(), 27, function.constData(), category.constData()}, message, stage, fatal, queue);
        file.fill('x'); function.fill('x'); category.fill('x'); message.fill('x');
        QCOMPARE(wakes, 1); QVERIFY(writes.empty());
        queue->close(); queue->drain();
        QCOMPARE(writes.size(), size_t{1}); const auto& record = writes.front();
        QCOMPARE(record.type, static_cast<QtMsgType>(type)); QCOMPARE(record.stage, stage);
        QCOMPARE(record.message, expected); QCOMPARE(record.sequence, quint64{1}); QCOMPARE(record.line, 27);
        QCOMPARE(QByteArray{record.file.data()}, QByteArray{"emitter.cpp"});
        QCOMPARE(QByteArray{record.function.data()}, QByteArray{"originalFunction"});
        QCOMPARE(QByteArray{record.category.data()}, QByteArray{"startup.category"});
        QVERIFY(record.has_file && record.has_function && record.has_category);
        QVERIFY(!record.context_truncated); QVERIFY(!record.overflow);
        QCOMPARE(qtlog::Message(record), expected);
        QCOMPARE(queue->receipt().accepted, quint64{1}); QCOMPARE(queue->receipt().written, quint64{1});
        QCOMPARE(queue->receipt().failed, quint64{0}); QCOMPARE(queue->receipt().rejected, quint64{0});
        QVERIFY(queue->receipt().closed); QCOMPARE(qtlog::FinalExitStatus(0, queue->receipt()), 0);
    }
    void missingQueueDoesNotResetOriginalFatalCounter() {
        auto policy = std::make_shared<qtlog::FatalPolicy>(2, 0);
        qtlog::Forward(QtWarningMsg, {}, "unavailable queue", qtlog::Stage::Startup, policy, {});
        const auto second = policy->enter(QtWarningMsg);
        QVERIFY(second.terminating); QVERIFY(!second.already_fatal);
    }
    void byteBudgetUsesUtf16AndOwnsExactOverflow_data() {
        QTest::addColumn<QString>("message"); QTest::addColumn<bool>("fits");
        QTest::newRow("empty-exact-record") << QString{} << true;
        QTest::newRow("bmp-exact-budget") << QString::fromUtf8("\xc3\xa9") << true;
        QTest::newRow("bmp-one-byte-short") << QString::fromUtf8("\xc3\xa9") << false;
        QTest::newRow("surrogate-pair-exact-budget") << QString::fromUtf8("\xf0\x9f\x99\x82") << true;
        QTest::newRow("surrogate-pair-one-byte-short") << QString::fromUtf8("\xf0\x9f\x99\x82") << false;
    }
    void byteBudgetUsesUtf16AndOwnsExactOverflow() {
        QFETCH(QString, message); QFETCH(bool, fits);
        const size_t text_bytes = static_cast<size_t>(message.size()) * sizeof(QChar);
        int wakes{0}; std::vector<qtlog::Record> writes;
        qtlog::Queue queue{[&] { ++wakes; }, [&](const auto& record) { writes.push_back(record); return true; },
            {4, sizeof(qtlog::Record) + text_bytes - (fits ? 0 : 1)}};
        QCOMPARE(queue.enqueue(QtInfoMsg, {}, message, qtlog::Stage::Main), fits);
        QCOMPARE(wakes, 1); QVERIFY(writes.empty()); queue.close(); queue.drain();
        QCOMPARE(writes.size(), size_t{1}); const auto& record = writes.front();
        QCOMPARE(record.sequence, quint64{1}); QCOMPARE(record.overflow, !fits);
        if (fits) QCOMPARE(qtlog::Message(record), message);
        else {
            QCOMPARE(record.rejected, quint64{1}); QCOMPARE(record.rejected_bytes, quint64{text_bytes});
            QCOMPARE(qtlog::Message(record), QString{"Qt log queue overflow: 1 messages (%1 UTF-16 bytes) were rejected."}.arg(text_bytes));
        }
        QCOMPARE(queue.receipt().accepted, fits ? quint64{1} : quint64{0});
        QCOMPARE(queue.receipt().rejected, fits ? quint64{0} : quint64{1});
        QCOMPARE(queue.receipt().written, quint64{1}); QCOMPARE(queue.receipt().failed, quint64{0});
        QVERIFY(queue.receipt().closed); QCOMPARE(qtlog::FinalExitStatus(0, queue.receipt()), fits ? 0 : 1);
    }
    void failedWriteDoesNotSkipLaterAcceptedRecords_data() {
        QTest::addColumn<bool>("throws");
        QTest::newRow("write-returns-false") << false; QTest::newRow("write-throws") << true;
    }
    void failedWriteDoesNotSkipLaterAcceptedRecords() {
        QFETCH(bool, throws);
        std::vector<QString> attempted;
        qtlog::Queue queue{[] {}, [&](const auto& record) {
            attempted.push_back(record.message);
            if (record.message == "middle") {
                if (throws) throw std::runtime_error{"actual writer failed"};
                return false;
            }
            return true;
        }, {4, 32768}};
        for (const auto* message : {"first", "middle", "last"}) QVERIFY(queue.enqueue(QtInfoMsg, {}, message, qtlog::Stage::Main));
        queue.close(); queue.drain();
        QCOMPARE(attempted, std::vector<QString>({"first", "middle", "last"}));
        QCOMPARE(queue.receipt().accepted, quint64{3}); QCOMPARE(queue.receipt().written, quint64{2});
        QCOMPARE(queue.receipt().failed, quint64{1}); QCOMPARE(queue.receipt().rejected, quint64{0});
        QVERIFY(queue.receipt().closed); QCOMPARE(qtlog::FinalExitStatus(0, queue.receipt()), 1);
        QCOMPARE(qtlog::FinalExitStatus(7, queue.receipt()), 7);
    }
    void overflowOrderSurvivesReentrantNativeWrite() {
        std::vector<qtlog::Record> writes; int wakes{0}; bool admitted_later{false};
        qtlog::Queue* owner{nullptr};
        qtlog::Queue queue{[&] { ++wakes; }, [&](const auto& record) {
            writes.push_back(record);
            if (record.sequence == 1) {
                (void)owner->receipt(); // Native ports run outside the mailbox lock.
                admitted_later = owner->enqueue(QtInfoMsg, {}, "later", qtlog::Stage::Main);
            }
            return true;
        }, {1, 32768}};
        owner = &queue;
        QVERIFY(queue.enqueue(QtWarningMsg, {}, "first", qtlog::Stage::Startup));
        QVERIFY(!queue.enqueue(QtInfoMsg, {}, "lost", qtlog::Stage::Startup));
        queue.drain(); QVERIFY(admitted_later); QCOMPARE(writes.size(), size_t{3});
        QCOMPARE(writes[0].message, QString{"first"}); QCOMPARE(writes[0].sequence, quint64{1});
        QVERIFY(writes[1].overflow); QCOMPARE(writes[1].sequence, quint64{2});
        QCOMPARE(writes[1].rejected, quint64{1}); QCOMPARE(writes[1].rejected_bytes, quint64{8});
        QCOMPARE(writes[2].message, QString{"later"}); QCOMPARE(writes[2].sequence, quint64{3});
        QCOMPARE(wakes, 1); queue.close(); queue.drain(); QCOMPARE(wakes, 2);
        QCOMPARE(queue.receipt().accepted, quint64{2}); QCOMPARE(queue.receipt().written, quint64{3});
        QCOMPARE(queue.receipt().rejected, quint64{1}); QVERIFY(queue.receipt().closed);
    }
    void manualWakeOwnsNativeOrderStageAndBoundedTurns() {
        int wakes{0}; std::vector<qtlog::Record> writes;
        qtlog::Queue queue{[&] { ++wakes; }, [&](const auto& record) { writes.push_back(record); return true; }, {32, 128 * 1024}};
        const QMessageLogContext context{"source.cpp", 12, "function", "category"};
        for (int i = 0; i < 20; ++i) QVERIFY(queue.enqueue(QtWarningMsg, context, QString::number(i), i < 10 ? qtlog::Stage::Startup : qtlog::Stage::Main));
        QCOMPARE(wakes, 1); QVERIFY(writes.empty()); queue.close(); QVERIFY(!queue.receipt().closed);
        queue.drain(); QCOMPARE(writes.size(), size_t{16}); QCOMPARE(wakes, 2); QVERIFY(!queue.receipt().closed);
        queue.drain(); QCOMPARE(writes.size(), size_t{20}); QVERIFY(queue.receipt().closed);
        for (int i = 0; i < 20; ++i) { QCOMPARE(writes[i].message, QString::number(i)); QCOMPARE(writes[i].stage, i < 10 ? qtlog::Stage::Startup : qtlog::Stage::Main); }
        QCOMPARE(queue.receipt().accepted, quint64{20}); QCOMPARE(queue.receipt().failed, quint64{0});
    }
    void fullAndOversizedAdmissionAlwaysProduceReservedOverflowDiagnostic() {
        std::vector<qtlog::Record> writes; qtlog::Queue queue{[] {}, [&](const auto& record) { writes.push_back(record); return true; }, {1, 4096}};
        const QMessageLogContext context{};
        QVERIFY(queue.enqueue(QtInfoMsg, context, "first", qtlog::Stage::Startup));
        QVERIFY(!queue.enqueue(QtInfoMsg, context, "second", qtlog::Stage::Main));
        QVERIFY(!queue.enqueue(QtWarningMsg, context, QString(8192, 'x'), qtlog::Stage::Main));
        queue.close(); queue.drain(); QCOMPARE(writes.size(), size_t{2}); QCOMPARE(writes[0].message, QStringLiteral("first"));
        QVERIFY(writes[1].overflow); QCOMPARE(writes[1].rejected, quint64{2}); QVERIFY(writes[1].rejected_bytes >= 16384);
        QCOMPARE(queue.receipt().accepted, quint64{1}); QCOMPARE(queue.receipt().rejected, quint64{2}); QVERIFY(queue.receipt().closed);
    }
    void closedAdmissionIsARealLossInFinalExitStatus() {
        qtlog::Queue queue{[] {}, [](const auto&) { return true; }, {4, 32768}};
        queue.close(); queue.drain(); QVERIFY(queue.receipt().closed);
        QVERIFY(!queue.enqueue(QtWarningMsg, {}, "late", qtlog::Stage::Main));
        QCOMPARE(queue.receipt().rejected, quint64{1}); QCOMPARE(qtlog::FinalExitStatus(0, queue.receipt()), 1);
        QCOMPARE(qtlog::FinalExitStatus(7, queue.receipt()), 7);
    }
    void exactNativeBudgetOwnsItsFinalWakeAndClose_data() {
        QTest::addColumn<bool>("overflow"); QTest::addColumn<bool>("close_before");
        QTest::newRow("open-exact-budget") << false << false;
        QTest::newRow("closed-exact-budget") << false << true;
        QTest::newRow("open-reserved-overflow") << true << false;
        QTest::newRow("closed-reserved-overflow") << true << true;
    }
    void exactNativeBudgetOwnsItsFinalWakeAndClose() {
        QFETCH(bool, overflow); QFETCH(bool, close_before);
        int wakes{0}; std::vector<qtlog::Record> writes;
        qtlog::Queue queue{[&] { ++wakes; }, [&](const auto& record) { writes.push_back(record); return true; }, {16, 64 * 1024}};
        for (int row = 0; row < 16; ++row) QVERIFY(queue.enqueue(QtInfoMsg, {}, QString::number(row), qtlog::Stage::Main));
        if (overflow) QVERIFY(!queue.enqueue(QtWarningMsg, {}, QStringLiteral("lost"), qtlog::Stage::Startup));
        if (close_before) queue.close();
        QCOMPARE(wakes, 1); QVERIFY(writes.empty()); queue.drain();
        QCOMPARE(writes.size(), size_t{16}); QCOMPARE(wakes, overflow ? 2 : 1);
        QCOMPARE(queue.receipt().closed, close_before && !overflow);
        if (overflow) {
            queue.drain(); QCOMPARE(writes.size(), size_t{17}); QVERIFY(writes.back().overflow);
            QCOMPARE(writes.back().sequence, quint64{17}); QCOMPARE(writes.back().rejected, quint64{1});
            QCOMPARE(writes.back().rejected_bytes, quint64{8});
        }
        for (int row = 0; row < 16; ++row) {
            QCOMPARE(writes[row].message, QString::number(row)); QCOMPARE(writes[row].sequence, quint64(row + 1));
        }
        const auto wakes_before_close = wakes; queue.close();
        QCOMPARE(wakes, wakes_before_close + (close_before ? 0 : 1)); queue.drain(); QVERIFY(queue.receipt().closed);
        const auto final_wakes = wakes; queue.close(); queue.drain(); QCOMPARE(wakes, final_wakes);
        QCOMPARE(queue.receipt().accepted, quint64{16}); QCOMPARE(queue.receipt().written, overflow ? quint64{17} : quint64{16});
        QCOMPARE(queue.receipt().rejected, overflow ? quint64{1} : quint64{0}); QCOMPARE(queue.receipt().failed, quint64{0});
    }
    void insufficientRecordRoomOwnsAReservedLoss_data() {
        QTest::addColumn<QString>("first");
        QTest::newRow("zero-room") << QStringLiteral("ab");
        QTest::newRow("two-bytes-remain") << QString::fromUtf8("\xc3\xa9");
    }
    void insufficientRecordRoomOwnsAReservedLoss() {
        QFETCH(QString, first);
        int wakes{0}; std::vector<qtlog::Record> writes;
        qtlog::Queue queue{[&] { ++wakes; }, [&](const auto& record) { writes.push_back(record); return true; }, {4, sizeof(qtlog::Record) + 4}};
        QVERIFY(queue.enqueue(QtInfoMsg, {}, first, qtlog::Stage::Main));
        QVERIFY(!queue.enqueue(QtCriticalMsg, {}, QString{}, qtlog::Stage::Startup));
        QCOMPARE(queue.receipt().accepted, quint64{1}); QCOMPARE(queue.receipt().rejected, quint64{1}); QCOMPARE(wakes, 1);
        queue.close(); queue.drain(); QCOMPARE(writes.size(), size_t{2});
        QCOMPARE(writes.front().message, first); QVERIFY(writes.back().overflow);
        QCOMPARE(writes.back().stage, qtlog::Stage::Startup); QCOMPARE(writes.back().rejected_bytes, quint64{0});
        QCOMPARE(writes.back().rejected, quint64{1}); QCOMPARE(queue.receipt().written, quint64{2}); QVERIFY(queue.receipt().closed);
    }
    void closingAdmissionStillRecordsAnOriginalLateDiagnostic_data() {
        QTest::addColumn<bool>("has_original"); QTest::newRow("empty-mailbox") << false; QTest::newRow("accepted-original") << true;
    }
    void closingAdmissionStillRecordsAnOriginalLateDiagnostic() {
        QFETCH(bool, has_original);
        int wakes{0}; std::vector<qtlog::Record> writes;
        qtlog::Queue queue{[&] { ++wakes; }, [&](const auto& record) { writes.push_back(record); return true; }, {4, 32768}};
        if (has_original) QVERIFY(queue.enqueue(QtInfoMsg, {}, QStringLiteral("original"), qtlog::Stage::Startup));
        queue.close(); QVERIFY(!queue.receipt().closed);
        QVERIFY(!queue.enqueue(QtCriticalMsg, {}, QStringLiteral("late"), qtlog::Stage::Main));
        QCOMPARE(wakes, 1); queue.drain(); QVERIFY(queue.receipt().closed);
        QCOMPARE(writes.size(), has_original ? size_t{2} : size_t{1});
        if (has_original) QCOMPARE(writes.front().message, QStringLiteral("original"));
        QVERIFY(writes.back().overflow); QCOMPARE(writes.back().stage, qtlog::Stage::Main);
        QCOMPARE(writes.back().rejected, quint64{1}); QCOMPARE(writes.back().rejected_bytes, quint64{8});
        QCOMPARE(queue.receipt().accepted, has_original ? quint64{1} : quint64{0});
        QCOMPARE(queue.receipt().written, has_original ? quint64{2} : quint64{1}); QCOMPARE(queue.receipt().rejected, quint64{1});
        QCOMPARE(qtlog::FinalExitStatus(0, queue.receipt()), 1);
    }
    void contextBoundaryFormatsOriginalOwnedFields_data() {
        QTest::addColumn<int>("length");
        QTest::newRow("absent-fields") << -1; QTest::newRow("present-empty-fields") << 0;
        QTest::newRow("exact-native-capacity") << 255; QTest::newRow("truncated-fields") << 256;
    }
    void contextBoundaryFormatsOriginalOwnedFields() {
        QFETCH(int, length);
        QByteArray file(qMax(0, length), 'f'), function(qMax(0, length), 'u'), category(qMax(0, length), 'c');
        const bool present = length >= 0; const bool truncated = length > 255;
        const QByteArray expected_file = file.left(255), expected_function = function.left(255), expected_category = category.left(255);
        const QMessageLogContext context{present ? file.constData() : nullptr, 79,
            present ? function.constData() : nullptr, present ? category.constData() : nullptr};
        qtlog::Record written; qtlog::Queue queue{[] {}, [&](const auto& record) { written = record; return true; }, {4, 32768}};
        const QString message = QString::fromUtf8("exact owned message-\xe2\x82\xac");
        QVERIFY(queue.enqueue(QtWarningMsg, context, message, qtlog::Stage::Startup));
        file.fill('x'); function.fill('x'); category.fill('x'); queue.close(); queue.drain();
        QCOMPARE(written.has_file, present); QCOMPARE(written.has_function, present); QCOMPARE(written.has_category, present);
        QCOMPARE(written.context_truncated, truncated); QCOMPARE(written.line, 79);
        QCOMPARE(QByteArray{written.file.data()}, expected_file); QCOMPARE(QByteArray{written.function.data()}, expected_function);
        QCOMPARE(QByteArray{written.category.data()}, expected_category);
        const QString displayed = message + (truncated ? QStringLiteral(" [Qt log context truncated]") : QString{});
        const QMessageLogContext expected{present ? expected_file.constData() : nullptr, 79,
            present ? expected_function.constData() : nullptr, present ? expected_category.constData() : nullptr};
        QCOMPARE(qtlog::Message(written), displayed);
        QCOMPARE(qtlog::FormatStderr(written), qFormatLogMessage(QtWarningMsg, expected, displayed).toUtf8() + '\n');
    }
    void captureKeepsOriginalFatalEnvironment_data() {
        QTest::addColumn<QByteArray>("warnings"); QTest::addColumn<QByteArray>("criticals");
        QTest::addColumn<int>("warning_limit"); QTest::addColumn<int>("critical_limit");
        QTest::newRow("disabled") << QByteArray{"0"} << QByteArray{"0"} << 0 << 0;
        QTest::newRow("decimal") << QByteArray{"2"} << QByteArray{"3"} << 2 << 3;
        QTest::newRow("hexadecimal") << QByteArray{"0x2"} << QByteArray{"0x2"} << 2 << 2;
        QTest::newRow("invalid-enables-first") << QByteArray{"enabled"} << QByteArray{"invalid"} << 1 << 1;
        const int negative_limit = QVersionNumber::fromString(QString::fromLatin1(qVersion())) >= QVersionNumber{6, 10, 0} ? 1 : 0;
        QTest::newRow("negative-runtime-contract") << QByteArray{"-1"} << QByteArray{"-1"} << negative_limit << negative_limit;
    }
    void captureKeepsOriginalFatalEnvironment() {
        QFETCH(QByteArray, warnings); QFETCH(QByteArray, criticals); QFETCH(int, warning_limit); QFETCH(int, critical_limit);
        const bool had_warnings = qEnvironmentVariableIsSet("QT_FATAL_WARNINGS"), had_criticals = qEnvironmentVariableIsSet("QT_FATAL_CRITICALS");
        const auto original_warnings = qgetenv("QT_FATAL_WARNINGS"), original_criticals = qgetenv("QT_FATAL_CRITICALS");
        const auto restore = qScopeGuard([&] {
            if (had_warnings) qputenv("QT_FATAL_WARNINGS", original_warnings); else qunsetenv("QT_FATAL_WARNINGS");
            if (had_criticals) qputenv("QT_FATAL_CRITICALS", original_criticals); else qunsetenv("QT_FATAL_CRITICALS");
        });
        qputenv("QT_FATAL_WARNINGS", warnings); qputenv("QT_FATAL_CRITICALS", criticals);
        const auto warning_policy = qtlog::CaptureFatalPolicy(), critical_policy = qtlog::CaptureFatalPolicy();
        qputenv("QT_FATAL_WARNINGS", "0"); qputenv("QT_FATAL_CRITICALS", "0");
        for (const auto& item : {std::pair{warning_policy, std::pair{QtWarningMsg, warning_limit}},
                                 std::pair{critical_policy, std::pair{QtCriticalMsg, critical_limit}}}) {
            const auto& policy = item.first; const auto [type, limit] = item.second;
            const int turns = limit ? limit : 4;
            for (int turn = 1; turn <= turns; ++turn) {
                const auto decision = policy->enter(type);
                QVERIFY(!decision.already_fatal); QCOMPARE(decision.terminating, limit != 0 && turn == limit);
            }
            if (limit) QVERIFY(policy->enter(QtDebugMsg).already_fatal);
            else QVERIFY(policy->enter(QtFatalMsg).terminating);
        }
    }
    void guiJoinRefusalKeepsIndependentNativeWakeCyclesOpen() {
        const auto* gui = QThread::currentThread(); auto state = std::make_shared<NativeWriteProbe>();
        qtlog::NativeSink sink{[state, gui](const auto& record) {
            if (QThread::currentThread() == gui) state->off_gui = false;
            state->messages.push_back(record.message); ++state->writes; return true;
        }};
        bool rejected{false};
        try { sink.finishAfterGui(); }
        catch (const std::logic_error&) { rejected = true; }
        QVERIFY(rejected);
        // Seventeen writes need at least two real bounded native drain turns.
        // No close is requested until the second native turn has been observed.
        for (int row = 0; row < 17; ++row) QVERIFY(sink.queue()->enqueue(QtInfoMsg, {}, QString::number(row), qtlog::Stage::Main));
        QTRY_COMPARE(state->writes.load(), 17); QVERIFY(!sink.queue()->receipt().closed);
        const auto post = presentation::QueuedPost(); bool done{false}; qtlog::Receipt final;
        sink.closeAsync([state, post](auto callback) { ++state->posts; post(std::move(callback)); },
            [&](auto receipt) { final = receipt; done = true; ++state->callbacks; });
        QTRY_VERIFY(done); QCOMPARE(state->posts.load(), 1); QCOMPARE(state->callbacks.load(), 1); QVERIFY(state->off_gui.load());
        QCOMPARE(final.accepted, quint64{17}); QCOMPARE(final.written, quint64{17}); QVERIFY(final.closed);
        for (int row = 0; row < 17; ++row) QCOMPARE(state->messages[row], QString::number(row));
        sink.closeAsync([state](auto) { ++state->posts; }, [state](auto) { ++state->callbacks; });
        QCOMPARE(state->posts.load(), 1); QCOMPARE(state->callbacks.load(), 1);
    }
    void guiDestructionOwnsTheAsynchronousNativeJoin() {
        const auto* gui = QThread::currentThread(); auto state = std::make_shared<NativeWriteProbe>();
        std::weak_ptr<qtlog::Queue> retired;
        {
            qtlog::NativeSink sink{[state, gui](const auto& record) {
                if (QThread::currentThread() == gui) state->off_gui = false;
                state->messages.push_back(record.message); ++state->writes; return true;
            }};
            retired = sink.queue();
            QVERIFY(sink.queue()->enqueue(QtInfoMsg, {}, QStringLiteral("owned through GUI destruction"), qtlog::Stage::Main));
        }
        // No facade remains. The public weak owner expires only after both the
        // native writer and its joining thread release the original queue.
        QTRY_VERIFY(retired.expired()); QCOMPARE(state->writes.load(), 1); QVERIFY(state->off_gui.load());
        QCOMPARE(state->messages, std::vector<QString>{QStringLiteral("owned through GUI destruction")});
    }
    void absentCloseNotificationPortsStillReleaseTheNativeOwner_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("post-without-observer") << 0; QTest::newRow("observer-without-post") << 1;
        QTest::newRow("no-presentation-ports") << 2;
    }
    void absentCloseNotificationPortsStillReleaseTheNativeOwner() {
        QFETCH(int, mode);
        auto state = std::make_shared<NativeWriteProbe>();
        auto sink = std::make_unique<qtlog::NativeSink>([state](const auto&) { ++state->writes; return true; });
        std::weak_ptr<qtlog::Queue> retired{sink->queue()};
        QVERIFY(sink->queue()->enqueue(QtInfoMsg, {}, QStringLiteral("original"), qtlog::Stage::Main));
        std::function<void(std::function<void()>)> post;
        std::function<void(qtlog::Receipt)> done;
        if (mode == 0) post = [state, bridge = presentation::QueuedPost()](auto callback) { ++state->posts; bridge(std::move(callback)); };
        if (mode == 1) done = [state](auto) { ++state->callbacks; };
        sink->closeAsync(std::move(post), std::move(done)); sink.reset();
        QTRY_VERIFY(retired.expired()); QCOMPARE(state->writes.load(), 1);
        QCOMPARE(state->posts.load(), 0); QCOMPARE(state->callbacks.load(), 0);
    }
    void fatalCounterParsingMatchesSupportedRuntimeVersions_data() {
        QTest::addColumn<QByteArray>("value"); QTest::addColumn<QString>("version"); QTest::addColumn<int>("expected");
        QTest::newRow("empty62") << QByteArray{} << QString{"6.2.4"} << 0;
        QTest::newRow("zero610") << QByteArray{"0"} << QString{"6.10.2"} << 0;
        QTest::newRow("text62") << QByteArray{"enabled"} << QString{"6.2.4"} << 1;
        QTest::newRow("text610") << QByteArray{"enabled"} << QString{"6.10.2"} << 1;
        QTest::newRow("hex62") << QByteArray{"0x2"} << QString{"6.2.4"} << 2;
        QTest::newRow("hex610") << QByteArray{"0x2"} << QString{"6.10.2"} << 2;
        QTest::newRow("octal") << QByteArray{"010"} << QString{"6.10.2"} << 8;
        QTest::newRow("negative62") << QByteArray{"-1"} << QString{"6.2.4"} << -1;
        QTest::newRow("negative69") << QByteArray{"-1"} << QString{"6.9.0"} << -1;
        QTest::newRow("negative610") << QByteArray{"-1"} << QString{"6.10.2"} << 1;
        QTest::newRow("long-leading-whitespace") << QByteArray(8192, ' ') + "2" << QString{"6.10.2"} << 2;
    }
    void fatalCounterParsingMatchesSupportedRuntimeVersions() {
        QFETCH(QByteArray, value); QFETCH(QString, version); QFETCH(int, expected);
        QCOMPARE(qtlog::FatalPolicy::parseCounter(value, QVersionNumber::fromString(version)), expected);
    }
    void warningAndCriticalCountersHaveIndependentOriginalOwnership() {
        const auto fatal = [](auto& policy, auto type) { return policy.enter(type).terminating; };
        qtlog::FatalPolicy policy{2, 2};
        QVERIFY(!fatal(policy, QtCriticalMsg)); QVERIFY(!fatal(policy, QtWarningMsg)); QVERIFY(!fatal(policy, QtDebugMsg));
        QVERIFY(fatal(policy, QtCriticalMsg)); QVERIFY(policy.enter(QtWarningMsg).already_fatal);
        qtlog::FatalPolicy disabled{-1, 0}; for (int i = 0; i < 20; ++i) { QVERIFY(!fatal(disabled, QtWarningMsg)); QVERIFY(!fatal(disabled, QtCriticalMsg)); }
    }
    void contextCaptureIsBoundedAndIndependentOfEmitterBuffers() {
        qtlog::Record written; qtlog::Queue queue{[] {}, [&](const auto& value) { written = value; return true; }, {4, 32768}};
        QByteArray file(8192, 'a'), function{"function"}, category{"category"};
        const QMessageLogContext context{file.constData(), 7, function.constData(), category.constData()};
        QVERIFY(queue.enqueue(QtWarningMsg, context, "message", qtlog::Stage::Startup));
        file.fill('b'); function.fill('b'); category.fill('b'); queue.close(); queue.drain();
        QVERIFY(written.context_truncated); QCOMPARE(QByteArray(written.file.data()), QByteArray(255, 'a'));
        QCOMPARE(QByteArray(written.function.data()), QByteArray{"function"}); QCOMPARE(written.line, 7);
    }
    void nativeWriteFailureIsOwnedByFinalReceipt() {
        qtlog::Queue queue{[] {}, [](const auto&) -> bool { throw std::runtime_error("write rejected"); }, {4, 32768}};
        QVERIFY(queue.enqueue(QtInfoMsg, {}, "message", qtlog::Stage::Main)); queue.close(); queue.drain();
        QCOMPARE(queue.receipt().written, quint64{0}); QCOMPARE(queue.receipt().failed, quint64{1}); QVERIFY(queue.receipt().closed);
    }
    void blockedRealWriterKeepsGuiResponsiveAndCloseAcknowledgesFinalFlush() {
        QSemaphore release; std::atomic_bool entered{false}; std::atomic<QThread*> writer{nullptr};
        std::vector<QString> writes; bool finished{false}; qtlog::Receipt receipt;
        qtlog::NativeSink sink{[&](const auto& record) {
            writer = QThread::currentThread(); entered = true; release.acquire(); writes.push_back(record.message); return true;
        }};
        QVERIFY(sink.queue()->enqueue(QtWarningMsg, {}, "before cleanup", qtlog::Stage::Startup));
        QVERIFY(sink.queue()->enqueue(QtWarningMsg, {}, "after cleanup", qtlog::Stage::Main));
        int heartbeats{0}; QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] { if (entered && !finished) { ++heartbeats; release.release(); } });
        heartbeat.start(1);
        sink.closeAsync(presentation::QueuedPost(), [&](auto result) { receipt = result; finished = true; });
        QVERIFY(!finished); QTRY_VERIFY(finished); heartbeat.stop();
        QVERIFY(heartbeats > 0); QVERIFY(writer.load() != nullptr && writer.load() != QThread::currentThread());
        QCOMPARE(writes, std::vector<QString>({"before cleanup", "after cleanup"}));
        QCOMPARE(receipt.accepted, quint64{2}); QCOMPARE(receipt.written, quint64{2}); QVERIFY(receipt.closed);
    }
};
BITCOINQML_REGISTER_QT_TEST(QtLogSinkTests)
#include <test_qtlogsink.moc>
