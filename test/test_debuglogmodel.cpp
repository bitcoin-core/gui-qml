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
    return model.data(model.index(row, 0), DebugLogModel::ContentRole).toString();
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
    void liveRefresh_insertsAtTopWithoutResetAndPrunesTail();
    void liveRefresh_canFullyDisplaceCacheWithoutReset();
    void liveRefresh_handlesDuplicateRecordsAndPartialWrites();
    void liveRefresh_discardsOversizedPartialUntilNewline();
    void liveRefresh_skipsOversizedCompleteLine();
    void loadMore_insertsOlderRowsAtBottom();
    void widerTailRequest_survivesRacesAndDeactivation();
    void filter_updatesIncrementallyAndWhileInactive();
    void rotation_fallsBackToFullSnapshot();
    void loadLimit_changesKeepRetainedCacheBounded();
    void openErrorEmptyByDefault();
    void openErrorPersistsAcrossSuccessfulRead();
    void clearOpenErrorClearsOnlyWhenSet();
    void openMissingFileIsLeftToTheReader();
    void logAvailableTracksReadFailures();
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
    QTRY_COMPARE(ContentAt(model, 0), QStringLiteral("line two"));
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
    records += Record("connect() to 127.0.0.1:9050 failed after wait: Connection refused (61)");
    records += Record("Writing 0 mempool transactions to file...");
    records += Record("ERROR: boom <bad>");
    records += Record("UpdateTip: new best=abc height=1");
    QVERIFY(WriteBytes(log_path, records));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 4);

    const QModelIndex update_tip = model.index(0, 0);
    QCOMPARE(model.data(update_tip, DebugLogModel::LineNumberRole).toString(), QStringLiteral("1"));
    QCOMPARE(model.data(update_tip, DebugLogModel::CommandRole).toString(), QStringLiteral("UpdateTip"));
    QCOMPARE(model.data(update_tip, DebugLogModel::MessageRole).toString(), QStringLiteral("new best=abc height=1"));
    QCOMPARE(model.data(update_tip, DebugLogModel::ContentRole).toString(), QStringLiteral("UpdateTip: new best=abc height=1"));
    QCOMPARE(model.data(update_tip, DebugLogModel::SeverityRole).toInt(), int(DebugLogModel::InfoSeverity));
    QVERIFY(!model.data(update_tip, DebugLogModel::DateLabelRole).toString().isEmpty());

    const QModelIndex error = model.index(1, 0);
    QCOMPARE(model.data(error, DebugLogModel::CommandRole).toString(), QStringLiteral("ERROR"));
    QCOMPARE(model.data(error, DebugLogModel::MessageRole).toString(), QStringLiteral("boom <bad>"));
    QCOMPARE(model.data(error, DebugLogModel::ContentRole).toString(), QStringLiteral("ERROR: boom &lt;bad&gt;"));
    QCOMPARE(model.data(error, DebugLogModel::SeverityRole).toInt(), int(DebugLogModel::ErrorSeverity));

    const QModelIndex plain = model.index(2, 0);
    QCOMPARE(model.data(plain, DebugLogModel::CommandRole).toString(), QString{});
    QCOMPARE(model.data(plain, DebugLogModel::MessageRole).toString(),
             QStringLiteral("Writing 0 mempool transactions to file..."));

    const QModelIndex endpoint = model.index(3, 0);
    QCOMPARE(model.data(endpoint, DebugLogModel::CommandRole).toString(), QString{});
    QCOMPARE(model.data(endpoint, DebugLogModel::MessageRole).toString(),
             QStringLiteral("connect() to 127.0.0.1:9050 failed after wait: Connection refused (61)"));
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
    QCOMPARE(ContentAt(extra_model, 0), QStringLiteral("line 1000"));
    QCOMPARE(ContentAt(extra_model, 999), QStringLiteral("line 1"));
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
    QVERIFY(ContentAt(model, 0).startsWith(QStringLiteral("very long y")));
    QVERIFY(ContentAt(model, 0).size() > 64 * 1024);
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
    QCOMPARE(ContentAt(model, 0), QStringLiteral("older two"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("older one"));

    // The newline completes the discarded physical line. Parsing resumes with
    // the first normal record after it instead of exposing a tail fragment.
    QVERIFY(WriteBytes(log_path, QByteArray{"\n"} + Record("after oversized"),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 3);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("after oversized"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("older two"));
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
    QCOMPARE(ContentAt(model, 0), QStringLiteral("newer"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("older"));
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
    QCOMPARE(ContentAt(model, 0), QStringLiteral("line 1000"));
    QCOMPARE(ContentAt(model, 999), QStringLiteral("line 1"));
    QCOMPARE(reset_spy.count(), 1);

    model.loadMore();
    QTRY_COMPARE(model.rowCount(), 1001);
    QVERIFY(!model.hasMoreLines());
    QCOMPARE(ContentAt(model, 1000), QStringLiteral("line 0"));
}

void DebugLogModelTests::liveRefresh_insertsAtTopWithoutResetAndPrunesTail()
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

    QTRY_COMPARE(ContentAt(model, 0), QStringLiteral("line 4"));
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(ContentAt(model, 1), QStringLiteral("line 3"));
    QCOMPARE(ContentAt(model, 2), QStringLiteral("line 2"));
    QCOMPARE(model.data(model.index(2, 0), DebugLogModel::LineNumberRole).toString(),
             QStringLiteral("3"));
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(insert_spy.at(0).at(1).toInt(), 0);
    QCOMPARE(insert_spy.at(0).at(2).toInt(), 1);
    QCOMPARE(remove_spy.count(), 1);
    QCOMPARE(remove_spy.at(0).at(1).toInt(), 3);
    QCOMPARE(remove_spy.at(0).at(2).toInt(), 4);
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

    QTRY_COMPARE(ContentAt(model, 0), QStringLiteral("line 6"));
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(ContentAt(model, 1), QStringLiteral("line 5"));
    QCOMPARE(ContentAt(model, 2), QStringLiteral("line 4"));
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(insert_spy.at(0).at(1).toInt(), 0);
    QCOMPARE(insert_spy.at(0).at(2).toInt(), 2);
    QCOMPARE(remove_spy.count(), 1);
    QCOMPARE(remove_spy.at(0).at(1).toInt(), 3);
    QCOMPARE(remove_spy.at(0).at(2).toInt(), 5);
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
    QCOMPARE(ContentAt(model, 0), QStringLiteral("split record"));
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
    QCOMPARE(ContentAt(model, 0), QStringLiteral("after oversized"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("baseline"));
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
    QCOMPARE(ContentAt(model, 0), QStringLiteral("after oversized"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("before oversized"));
    QCOMPARE(ContentAt(model, 2), QStringLiteral("baseline"));
}

void DebugLogModelTests::loadMore_insertsOlderRowsAtBottom()
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
    QCOMPARE(ContentAt(model, 999), QStringLiteral("line 200"));
    QCOMPARE(ContentAt(model, 1000), QStringLiteral("line 199"));
    QCOMPARE(ContentAt(model, 1199), QStringLiteral("line 0"));
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(insert_spy.at(0).at(1).toInt(), 1000);
    QCOMPARE(insert_spy.at(0).at(2).toInt(), 1199);
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
        QCOMPARE(ContentAt(model, 2499), QStringLiteral("line 0"));
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
        QCOMPARE(ContentAt(model, 1199), QStringLiteral("line 0"));
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
    QCOMPARE(ContentAt(model, 0), QStringLiteral("keep new"));
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(reset_spy.count(), 0);

    model.setActive(false);
    model.setFilter(QStringLiteral("drop"));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("drop new"));
    QVERIFY(WriteBytes(log_path, Record("drop while inactive"),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTest::qWait(50);
    QCOMPARE(model.rowCount(), 2);

    QSignalSpy reactivate_reset_spy(&model, &QAbstractItemModel::modelReset);
    model.setActive(true);
    QTRY_COMPARE(ContentAt(model, 0), QStringLiteral("drop while inactive"));
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
    QCOMPARE(ContentAt(model, 0), QStringLiteral("rotated two"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("rotated one"));
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
    // next activation; older rows are appended at the bottom.
    QSignalSpy insert_spy(&model, &QAbstractItemModel::rowsInserted);
    model.setLoadLimit(4);
    QCOMPARE(model.rowCount(), 2);
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 4);
    QCOMPARE(ContentAt(model, 3), QStringLiteral("line 1"));
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(insert_spy.at(0).at(1).toInt(), 2);
    QCOMPARE(insert_spy.at(0).at(2).toInt(), 3);
}

void DebugLogModelTests::openErrorEmptyByDefault()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const fs::path log_path = fs::PathFromString(temp_dir.filePath("debug.log").toStdString());

    DebugLogModel model(log_path);
    QVERIFY(model.openError().isEmpty());
}

// The action error (this click failed) and the state error (the log itself
// cannot be read) have different lifetimes, so they cannot share a field: the
// background reader clears its own error on every successful read, which would
// wipe the click failure on the next auto-refresh tick.
void DebugLogModelTests::openErrorPersistsAcrossSuccessfulRead()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path = temp_dir.filePath("debug.log");
    const fs::path log_path = fs::PathFromString(path.toStdString());
    QVERIFY(WriteBytes(path, NumberedRecords(0, 2)));

    // The log exists and is readable; only the hand-off to another application
    // fails, which is the one thing openLogFile() still reports itself.
    DebugLogModel model(log_path);
    model.setOpenLocalFileFnForTesting([](const QString&) { return false; });
    QVERIFY(!model.openLogFile());
    const QString action_error = model.openError();
    QVERIFY(!action_error.isEmpty());

    // Let a background read succeed.
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 2);

    // The read succeeded, but the click failure must still be reported.
    QCOMPARE(model.openError(), action_error);
}

// clearOpenError() drops the action error (the page calls it when re-entered so
// a stale click failure does not survive navigation) and is a no-op when
// nothing is pending.
void DebugLogModelTests::clearOpenErrorClearsOnlyWhenSet()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path = temp_dir.filePath("debug.log");
    QVERIFY(WriteBytes(path, NumberedRecords(0, 1)));

    DebugLogModel model(fs::PathFromString(path.toStdString()));
    model.setOpenLocalFileFnForTesting([](const QString&) { return false; });
    QSignalSpy error_spy(&model, &DebugLogModel::openErrorChanged);

    // No pending error: no change, no signal.
    model.clearOpenError();
    QCOMPARE(error_spy.count(), 0);

    QVERIFY(!model.openLogFile());
    QVERIFY(!model.openError().isEmpty());
    QCOMPARE(error_spy.count(), 1);

    model.clearOpenError();
    QVERIFY(model.openError().isEmpty());
    QCOMPARE(error_spy.count(), 2);
}

// A missing log is a state of the log, not the outcome of one click, so
// openLogFile() must not record it as an action error: doing so would let the
// page clear it on re-entry while the file is still missing. The reader owns
// that message and keeps it for as long as the condition holds.
void DebugLogModelTests::openMissingFileIsLeftToTheReader()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const fs::path missing = fs::PathFromString(temp_dir.filePath("does-not-exist.log").toStdString());

    DebugLogModel model(missing);
    QSignalSpy error_spy(&model, &DebugLogModel::openErrorChanged);

    QVERIFY(!model.openLogFile());
    QVERIFY(model.openError().isEmpty());
    QCOMPARE(error_spy.count(), 0);

    // The reader reports it instead, and clearing on page entry does not drop
    // it, because the log is still missing.
    model.setActive(true);
    QTRY_VERIFY(!model.openError().isEmpty());
    model.clearOpenError();
    QVERIFY(!model.openError().isEmpty());
}

// logAvailable drives the controls that only make sense against readable log
// content, so it has to follow the reader in both directions.
void DebugLogModelTests::logAvailableTracksReadFailures()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path = temp_dir.filePath("debug.log");
    const fs::path log_path = fs::PathFromString(path.toStdString());

    DebugLogModel model(log_path);
    QSignalSpy available_spy(&model, &DebugLogModel::logAvailableChanged);

    // Starts optimistic so the page does not flash a disabled state.
    QVERIFY(model.logAvailable());

    model.setActive(true);
    QTRY_VERIFY(!model.logAvailable());
    QCOMPARE(available_spy.count(), 1);

    // The log appears and a refresh picks it up.
    QVERIFY(WriteBytes(path, NumberedRecords(0, 2)));
    model.refresh(true);
    QTRY_VERIFY(model.logAvailable());
    QCOMPARE(available_spy.count(), 2);
    QVERIFY(model.openError().isEmpty());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(DebugLogModelTests)
#else
QTEST_MAIN(DebugLogModelTests)
#endif
#include "test_debuglogmodel.moc"
