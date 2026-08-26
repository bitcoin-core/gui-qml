// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <QByteArray>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <qml/models/debuglogmodel.h>

#include <util/fs.h>

namespace {
QByteArray Record(const QByteArray& message)
{
    return QByteArray{"2026-06-19T10:00:00Z "} + message + '\n';
}

bool WriteBytes(const QString& path, const QByteArray& bytes,
                QIODevice::OpenMode mode = QIODevice::WriteOnly)
{
    QFile file(path);
    if (!file.open(mode)) return false;
    return file.write(bytes) == bytes.size();
}

QByteArray NumberedRecords(int first, int count)
{
    QByteArray bytes;
    for (int i = first; i < first + count; ++i) {
        bytes += Record("line " + QByteArray::number(i));
    }
    return bytes;
}

QByteArray OversizedLine(char fill)
{
    return QByteArray(DebugLogModel::kMaxLogLineBytes + 1, fill);
}

QString ContentAt(const DebugLogModel& model, int row)
{
    return model.data(model.index(row, 0), DebugLogModel::MessageRole).toString();
}
} // namespace

class DebugLogModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void inactiveModel_ignoresRefreshUntilActivated();
    void parsedRoles_extractStructuredLogLines();
    void initialLoad_isSingleBatchAndDetectsHasMore();
    void initialLoad_handlesBlankLinesAtBlockBoundaries();
    void initialLoad_discardsOversizedPartialAndResynchronizes();
    void initialLoad_skipsOversizedCompleteLine();
    void deltaAfterEmptyLoad_preservesHasMoreSentinel();
    void liveRefresh_appendsWithoutResetAndPrunesHead();
    void liveRefresh_canFullyDisplaceCacheWithoutReset();
    void liveRefresh_handlesDuplicateRecordsAndPartialWrites();
    void liveRefresh_discardsOversizedPartialUntilNewline();
    void liveRefresh_skipsOversizedCompleteLine();
    void loadMore_insertsOlderRowsAtTop();
    void widerTailRequest_survivesRacesAndDeactivation();
    void filter_updatesIncrementallyAndWhileInactive();
    void rotation_fallsBackToFullSnapshot();
    void loadLimit_changesKeepRetainedCacheBounded();
};

void DebugLogModelTests::inactiveModel_ignoresRefreshUntilActivated()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, Record("line one")));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    QVERIFY(!model.active());

    model.refresh(/*full_load=*/true);
    QTest::qWait(50);
    QCOMPARE(model.rowCount(), 0);

    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 1);

    model.setActive(false);
    QVERIFY(WriteBytes(log_path, Record("line two"),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh(/*full_load=*/true);
    QTest::qWait(50);
    QCOMPARE(model.rowCount(), 1);

    // Reactivation paints the retained row immediately, then catches up from
    // the saved byte offset without requiring another full tail read.
    QSignalSpy insert_spy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset_spy(&model, &QAbstractItemModel::modelReset);
    model.setActive(true);
    QTRY_COMPARE(ContentAt(model, 1), QStringLiteral("line two"));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(reset_spy.count(), 0);
}

void DebugLogModelTests::parsedRoles_extractStructuredLogLines()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");

    QByteArray records;
    records += "2026-06-19T10:00:00.123456Z [net] Bound to 127.0.0.1\n";
    records += "2026-06-19T10:00:01Z [rpc:error] boom <bad>\n";
    records += "2026-06-19T10:00:02Z [mempool] Imported transactions\n";
    records += "2026-06-19T10:00:02Z [bench] benchmark completed\n";
    records += "2026-06-19T10:00:02Z [net:warning] peer is slow\n";
    records += "2026-06-19T10:00:03Z ERROR: legacy failure\n";
    records += "continuation without metadata\n";
    QVERIFY(WriteBytes(log_path, records));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 7);

    const QModelIndex network = model.index(0, 0);
    QCOMPARE(model.data(network, DebugLogModel::MessageRole).toString(), QStringLiteral("Bound to 127.0.0.1"));
    QCOMPARE(model.data(network, DebugLogModel::IsErrorRole).toBool(), false);
    QCOMPARE(model.data(network, DebugLogModel::IsWarningRole).toBool(), false);
    QCOMPARE(model.data(network, DebugLogModel::TimestampRole).toString(), QStringLiteral("10:00:00"));

    const QModelIndex rpc_error = model.index(1, 0);
    QCOMPARE(model.data(rpc_error, DebugLogModel::MessageRole).toString(), QStringLiteral("boom <bad>"));
    QCOMPARE(model.data(rpc_error, DebugLogModel::IsErrorRole).toBool(), true);
    QCOMPARE(model.data(rpc_error, DebugLogModel::IsWarningRole).toBool(), false);

    const QModelIndex mempool = model.index(2, 0);
    QCOMPARE(model.data(mempool, DebugLogModel::MessageRole).toString(), QStringLiteral("Imported transactions"));
    QCOMPARE(model.data(mempool, DebugLogModel::IsErrorRole).toBool(), false);

    const QModelIndex bench = model.index(3, 0);
    QCOMPARE(model.data(bench, DebugLogModel::MessageRole).toString(), QStringLiteral("benchmark completed"));

    const QModelIndex warning = model.index(4, 0);
    QCOMPARE(model.data(warning, DebugLogModel::MessageRole).toString(), QStringLiteral("peer is slow"));
    QCOMPARE(model.data(warning, DebugLogModel::IsErrorRole).toBool(), false);
    QCOMPARE(model.data(warning, DebugLogModel::IsWarningRole).toBool(), true);

    const QModelIndex legacy_error = model.index(5, 0);
    QCOMPARE(model.data(legacy_error, DebugLogModel::MessageRole).toString(), QStringLiteral("legacy failure"));
    QCOMPARE(model.data(legacy_error, DebugLogModel::IsErrorRole).toBool(), true);
    QCOMPARE(model.data(legacy_error, DebugLogModel::IsWarningRole).toBool(), false);

    const QModelIndex continuation = model.index(6, 0);
    QCOMPARE(model.data(continuation, DebugLogModel::TimestampRole).toString(), QString{});

    model.setWarningsAndErrorsOnly(true);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("boom <bad>"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("peer is slow"));
    QCOMPARE(ContentAt(model, 2), QStringLiteral("legacy failure"));
}

void DebugLogModelTests::initialLoad_isSingleBatchAndDetectsHasMore()
{
    QTemporaryDir exact_dir;
    QVERIFY(exact_dir.isValid());
    const QString exact_path = exact_dir.filePath("debug.log");
    QVERIFY(WriteBytes(exact_path, NumberedRecords(0, 1000)));

    DebugLogModel exact_model(fs::PathFromString(exact_path.toStdString()));
    QSignalSpy exact_reset_spy(&exact_model, &QAbstractItemModel::modelReset);
    QSignalSpy exact_insert_spy(&exact_model, &QAbstractItemModel::rowsInserted);
    exact_model.setActive(true);
    QTRY_COMPARE(exact_model.rowCount(), 1000);
    QVERIFY(!exact_model.hasMoreLines());
    QCOMPARE(exact_reset_spy.count(), 1);
    QCOMPARE(exact_insert_spy.count(), 0);

    QTemporaryDir extra_dir;
    QVERIFY(extra_dir.isValid());
    const QString extra_path = extra_dir.filePath("debug.log");
    QVERIFY(WriteBytes(extra_path, NumberedRecords(0, 1001)));

    DebugLogModel extra_model(fs::PathFromString(extra_path.toStdString()));
    QSignalSpy reset_spy(&extra_model, &QAbstractItemModel::modelReset);
    extra_model.setActive(true);
    QTRY_COMPARE(extra_model.rowCount(), 1000);
    QTRY_VERIFY(extra_model.hasMoreLines());
    QCOMPARE(reset_spy.count(), 1);
    QCOMPARE(ContentAt(extra_model, 0), QStringLiteral("line 1"));
    QCOMPARE(ContentAt(extra_model, 999), QStringLiteral("line 1000"));
}

void DebugLogModelTests::initialLoad_handlesBlankLinesAtBlockBoundaries()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");

    QByteArray bytes;
    // More than one 64 KiB read block, with empty records around and across
    // block boundaries. Empty lines must neither hang parsing nor consume the
    // requested nonblank-line budget.
    for (int i = 0; i < 1400; ++i) {
        bytes += (i % 3 == 0) ? QByteArray{"\n"} : Record("payload " + QByteArray::number(i) + QByteArray(90, 'x'));
    }
    bytes += Record("very long " + QByteArray(70 * 1024, 'y'));
    QVERIFY(bytes.size() > 64 * 1024);
    QVERIFY(WriteBytes(log_path, bytes));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setLoadLimit(400);
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 400);
    QVERIFY(model.hasMoreLines());
    QVERIFY(ContentAt(model, 399).startsWith(QStringLiteral("very long y")));
    QVERIFY(ContentAt(model, 399).size() > 64 * 1024);
}

void DebugLogModelTests::initialLoad_discardsOversizedPartialAndResynchronizes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path,
                       Record("older one") + Record("older two") + OversizedLine('x')));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("older one"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("older two"));

    // The newline completes the discarded physical line. Parsing resumes with
    // the first normal record after it instead of exposing a tail fragment.
    QVERIFY(WriteBytes(log_path, QByteArray{"\n"} + Record("after oversized"),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 3);
    QCOMPARE(ContentAt(model, 1), QStringLiteral("older two"));
    QCOMPARE(ContentAt(model, 2), QStringLiteral("after oversized"));
}

void DebugLogModelTests::initialLoad_skipsOversizedCompleteLine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path,
                       Record("older") + OversizedLine('x') + '\n' + Record("newer")));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("older"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("newer"));
}

void DebugLogModelTests::deltaAfterEmptyLoad_preservesHasMoreSentinel()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, {}));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    QSignalSpy reset_spy(&model, &QAbstractItemModel::modelReset);
    model.setActive(true);
    QTRY_COMPARE(reset_spy.count(), 1);
    QCOMPARE(model.rowCount(), 0);

    QVERIFY(WriteBytes(log_path, NumberedRecords(0, 1001),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 1000);
    QTRY_VERIFY(model.hasMoreLines());
    QCOMPARE(ContentAt(model, 0), QStringLiteral("line 1"));
    QCOMPARE(ContentAt(model, 999), QStringLiteral("line 1000"));
    QCOMPARE(reset_spy.count(), 1);

    model.loadMore();
    QTRY_COMPARE(model.rowCount(), 1001);
    QVERIFY(!model.hasMoreLines());
    QCOMPARE(ContentAt(model, 0), QStringLiteral("line 0"));
}

void DebugLogModelTests::liveRefresh_appendsWithoutResetAndPrunesHead()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, NumberedRecords(0, 3)));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setLoadLimit(3);
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 3);

    QSignalSpy insert_spy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy remove_spy(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy reset_spy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy new_lines_spy(&model, &DebugLogModel::newLinesAdded);

    QVERIFY(WriteBytes(log_path, NumberedRecords(3, 2),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();

    QTRY_COMPARE(ContentAt(model, 2), QStringLiteral("line 4"));
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("line 2"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("line 3"));
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(insert_spy.at(0).at(1).toInt(), 1);
    QCOMPARE(insert_spy.at(0).at(2).toInt(), 2);
    QCOMPARE(remove_spy.count(), 1);
    QCOMPARE(remove_spy.at(0).at(1).toInt(), 0);
    QCOMPARE(remove_spy.at(0).at(2).toInt(), 1);
    QCOMPARE(reset_spy.count(), 0);
    QCOMPARE(new_lines_spy.count(), 1);
    QCOMPARE(new_lines_spy.at(0).at(0).toInt(), 2);
    QVERIFY(model.hasMoreLines());

    model.refresh();
    QTest::qWait(50);
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(reset_spy.count(), 0);
}

void DebugLogModelTests::liveRefresh_canFullyDisplaceCacheWithoutReset()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, NumberedRecords(0, 3)));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setLoadLimit(3);
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 3);

    QSignalSpy insert_spy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy remove_spy(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy reset_spy(&model, &QAbstractItemModel::modelReset);
    QVERIFY(WriteBytes(log_path, NumberedRecords(3, 4),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();

    QTRY_COMPARE(ContentAt(model, 2), QStringLiteral("line 6"));
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("line 4"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("line 5"));
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(insert_spy.at(0).at(1).toInt(), 0);
    QCOMPARE(insert_spy.at(0).at(2).toInt(), 2);
    QCOMPARE(remove_spy.count(), 1);
    QCOMPARE(remove_spy.at(0).at(1).toInt(), 0);
    QCOMPARE(remove_spy.at(0).at(2).toInt(), 2);
    QCOMPARE(reset_spy.count(), 0);
    QVERIFY(model.hasMoreLines());
}

void DebugLogModelTests::liveRefresh_handlesDuplicateRecordsAndPartialWrites()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    const QByteArray duplicate = Record("identical");
    QVERIFY(WriteBytes(log_path, duplicate + duplicate));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 2);

    QSignalSpy insert_spy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset_spy(&model, &QAbstractItemModel::modelReset);
    QVERIFY(WriteBytes(log_path, duplicate, QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 3);
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(reset_spy.count(), 0);

    // The incomplete record is retained off-model until its newline arrives.
    QVERIFY(WriteBytes(log_path, QByteArray{"2026-06-19T10:00:00Z split"},
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTest::qWait(50);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(insert_spy.count(), 1);

    QVERIFY(WriteBytes(log_path, QByteArray{" record\n"},
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 4);
    QCOMPARE(ContentAt(model, 3), QStringLiteral("split record"));
    QCOMPARE(insert_spy.count(), 2);
    QCOMPARE(reset_spy.count(), 0);
}

void DebugLogModelTests::liveRefresh_discardsOversizedPartialUntilNewline()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, Record("baseline")));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 1);

    QVERIFY(WriteBytes(log_path,
                       QByteArray(DebugLogModel::kMaxLogLineBytes - 16, 'x'),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTest::qWait(100);
    QCOMPARE(model.rowCount(), 1);

    // Cross the limit in a later watcher read, then verify further fragments
    // remain discarded until the record boundary arrives.
    QVERIFY(WriteBytes(log_path, QByteArray(32, 'y'),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTest::qWait(100);
    QVERIFY(WriteBytes(log_path, QByteArray{"ignored tail"},
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTest::qWait(100);
    QCOMPARE(model.rowCount(), 1);

    QVERIFY(WriteBytes(log_path, QByteArray{"\n"} + Record("after oversized"),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("baseline"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("after oversized"));
}

void DebugLogModelTests::liveRefresh_skipsOversizedCompleteLine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, Record("baseline")));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 1);

    QVERIFY(WriteBytes(log_path,
                       Record("before oversized") + OversizedLine('x') + '\n'
                           + Record("after oversized"),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 3);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("baseline"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("before oversized"));
    QCOMPARE(ContentAt(model, 2), QStringLiteral("after oversized"));
}

void DebugLogModelTests::loadMore_insertsOlderRowsAtTop()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, NumberedRecords(0, 1200)));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 1000);
    QVERIFY(model.hasMoreLines());

    QSignalSpy insert_spy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset_spy(&model, &QAbstractItemModel::modelReset);
    model.loadMore();

    QTRY_COMPARE(model.rowCount(), 1200);
    QCOMPARE(model.loadLimit(), 2000);
    QVERIFY(!model.hasMoreLines());
    QCOMPARE(ContentAt(model, 0), QStringLiteral("line 0"));
    QCOMPARE(ContentAt(model, 199), QStringLiteral("line 199"));
    QCOMPARE(ContentAt(model, 200), QStringLiteral("line 200"));
    QCOMPARE(ContentAt(model, 1199), QStringLiteral("line 1199"));
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(insert_spy.at(0).at(1).toInt(), 0);
    QCOMPARE(insert_spy.at(0).at(2).toInt(), 199);
    QCOMPARE(reset_spy.count(), 0);
}

void DebugLogModelTests::widerTailRequest_survivesRacesAndDeactivation()
{
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString log_path = dir.filePath("debug.log");
        QVERIFY(WriteBytes(log_path, NumberedRecords(0, 2500)));

        DebugLogModel model(fs::PathFromString(log_path.toStdString()));
        model.setActive(true);
        QTRY_COMPARE(model.rowCount(), 1000);

        // The second request is issued while the first full-tail read may be
        // in flight. A stale 2,000-row result must trigger the pending 3,000
        // capacity read rather than becoming the final snapshot.
        model.loadMore();
        model.loadMore();
        QCOMPARE(model.loadLimit(), 3000);
        QTRY_COMPARE(model.rowCount(), 2500);
        QVERIFY(!model.hasMoreLines());
        QCOMPARE(ContentAt(model, 0), QStringLiteral("line 0"));
        QCOMPARE(ContentAt(model, 2499), QStringLiteral("line 2499"));
    }

    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString log_path = dir.filePath("debug.log");
        QVERIFY(WriteBytes(log_path, NumberedRecords(0, 1200)));

        DebugLogModel model(fs::PathFromString(log_path.toStdString()));
        model.setActive(true);
        QTRY_COMPARE(model.rowCount(), 1000);

        model.loadMore();
        model.setActive(false);
        model.setActive(true);
        QTRY_COMPARE(model.rowCount(), 1200);
        QCOMPARE(model.loadLimit(), 2000);
        QCOMPARE(ContentAt(model, 0), QStringLiteral("line 0"));
    }
}

void DebugLogModelTests::filter_updatesIncrementallyAndWhileInactive()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, Record("keep old") + Record("drop old")));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 2);
    model.setFilter(QStringLiteral("keep"));
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("keep old"));

    QSignalSpy insert_spy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset_spy(&model, &QAbstractItemModel::modelReset);
    QVERIFY(WriteBytes(log_path, Record("keep new") + Record("drop new"),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("keep old"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("keep new"));
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(reset_spy.count(), 0);

    model.setActive(false);
    model.setFilter(QStringLiteral("drop"));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("drop old"));
    QVERIFY(WriteBytes(log_path, Record("drop while inactive"),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTest::qWait(50);
    QCOMPARE(model.rowCount(), 2);

    QSignalSpy reactivate_reset_spy(&model, &QAbstractItemModel::modelReset);
    model.setActive(true);
    QTRY_COMPARE(ContentAt(model, 2), QStringLiteral("drop while inactive"));
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(reactivate_reset_spy.count(), 0);
}

void DebugLogModelTests::rotation_fallsBackToFullSnapshot()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, NumberedRecords(0, 5)));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 5);

    QSignalSpy reset_spy(&model, &QAbstractItemModel::modelReset);
    QVERIFY(WriteBytes(log_path, Record("rotated one") + Record("rotated two")));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("rotated one"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("rotated two"));
    QCOMPARE(reset_spy.count(), 1);
}

void DebugLogModelTests::loadLimit_changesKeepRetainedCacheBounded()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, NumberedRecords(0, 5)));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setLoadLimit(5);
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 5);
    model.setActive(false);

    model.setLoadLimit(2);
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(model.hasMoreLines());

    // Raising the cap while inactive forces a wider bounded tail read on the
    // next activation; older rows are prepended at the top.
    QSignalSpy insert_spy(&model, &QAbstractItemModel::rowsInserted);
    model.setLoadLimit(4);
    QCOMPARE(model.rowCount(), 2);
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 4);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("line 1"));
    QCOMPARE(ContentAt(model, 3), QStringLiteral("line 4"));
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(insert_spy.at(0).at(1).toInt(), 0);
    QCOMPARE(insert_spy.at(0).at(2).toInt(), 1);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(DebugLogModelTests)
#else
QTEST_MAIN(DebugLogModelTests)
#endif
#include "test_debuglogmodel.moc"
