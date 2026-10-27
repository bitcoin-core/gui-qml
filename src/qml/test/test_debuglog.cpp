// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/debuglogmodel.h>
#include <qml/models/debuglogreader.h>
#include <qml/test/qt_test_registry.h>

#include <QAbstractItemModelTester>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class DebugLogTests : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    std::atomic_bool m_cancelled{false};

    fs::path path() const { return fs::PathFromString(m_dir.filePath("debug.log").toStdString()); }

    void write(const QByteArray& bytes, bool append = false)
    {
        QFile file(QString::fromStdString(path().utf8string()));
        QVERIFY(file.open(QIODevice::WriteOnly | (append ? QIODevice::Append : QIODevice::Truncate)));
        QCOMPARE(file.write(bytes), bytes.size());
    }

    DebugLogReader::ReadResult read(const DebugLogReader::ReadResult& previous)
    {
        return DebugLogReader::ReadAndFilter(path(), 2, false, previous.file_size,
                                            previous.trailing_partial,
                                            previous.discarding_oversized_line,
                                            previous.file_anchor, m_cancelled);
    }

private Q_SLOTS:
    void boundedTailAndParsing()
    {
        write("older\n2026-01-01T00:00:00Z WARNING: <unsafe>& text\r\nERROR: last\nunfinished");
        const auto result = DebugLogReader::ReadTail(path(), 2, m_cancelled);
        QVERIFY(result.file_opened);
        QVERIFY(result.has_more_lines);
        QCOMPARE(result.lines.size(), 2);
        QCOMPARE(result.lines[0].severity, DebugLogReader::ErrorSeverity);
        QCOMPARE(result.lines[1].severity, DebugLogReader::WarningSeverity);
        QCOMPARE(result.lines[1].command, QStringLiteral("WARNING"));
        QCOMPARE(result.lines[1].message, QStringLiteral("<unsafe>& text"));
        QCOMPARE(result.lines[1].content, QStringLiteral("WARNING: &lt;unsafe&gt;&amp; text"));
        QCOMPARE(result.trailing_partial, QByteArray("unfinished"));
    }

    void incrementalFramingAndRotation()
    {
        write("first\npart");
        auto result = DebugLogReader::ReadTail(path(), 2, m_cancelled);
        write("ial\nnext\n", true);
        result = read(result);
        QVERIFY(!result.full_snapshot);
        QCOMPARE(result.lines.size(), 2);
        QCOMPARE(result.lines[1].message, QStringLiteral("partial"));
        QCOMPARE(result.lines[1].source_offset, 6);
        const auto unchanged = read(result);
        QVERIFY(unchanged.lines.isEmpty());
        write("rotated log with a different anchor\n");
        result = read(result);
        QVERIFY(result.continuity_lost);
        QVERIFY(result.full_snapshot);
        QCOMPARE(result.lines.size(), 1);
        QCOMPARE(result.lines[0].message, QStringLiteral("rotated log with a different anchor"));
    }

    void oversizedLinesAndCancellation()
    {
        write("before\n" + QByteArray(DebugLogReader::kMaxLogLineBytes + 1, 'x'));
        auto result = DebugLogReader::ReadTail(path(), 2, m_cancelled);
        QVERIFY(result.discarding_oversized_line);
        QVERIFY(result.trailing_partial.isEmpty());
        QCOMPARE(result.lines.size(), 1);
        write("more oversized bytes\nafter\n", true);
        result = read(result);
        QVERIFY(!result.discarding_oversized_line);
        QCOMPARE(result.lines.size(), 1);
        QCOMPARE(result.lines[0].message, QStringLiteral("after"));
        m_cancelled = true;
        QVERIFY(!DebugLogReader::ReadTail(path(), 2, m_cancelled).file_opened);
        m_cancelled = false;
    }

    void modelFilteringActivationAndLoadMore()
    {
        write("alpha\nbeta\nalpha newest\n");
        DebugLogModel model(path());
        QAbstractItemModelTester consistency(&model, QAbstractItemModelTester::FailureReportingMode::QtTest);
        model.setLoadLimit(2);
        model.refresh();
        QCOMPARE(model.rowCount(), 0); // An inactive page must not start a read.
        model.setActive(true);
        QTRY_COMPARE(model.rowCount(), 2);
        QVERIFY(model.hasMoreLines());
        model.setFilter("ALPHA");
        QCOMPARE(model.rowCount(), 1);
        model.loadMore();
        QTRY_COMPARE(model.rowCount(), 2);
        QVERIFY(!model.hasMoreLines());
        model.setActive(false);
        write("new log\n");
        model.refresh(true);
        QCOMPARE(model.rowCount(), 2);
        model.setFilter("");
        QCOMPARE(model.rowCount(), 3); // Filtering cached rows does not require a read.
        model.setActive(true);
        QTRY_COMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0), DebugLogModel::MessageRole).toString(), QStringLiteral("new log"));
        model.stop();
        model.refresh();
        QCOMPARE(model.rowCount(), 1);
    }

    void boundedModelAndMissingFileRecovery()
    {
        const auto missing = fs::PathFromString(m_dir.filePath("later.log").toStdString());
        DebugLogModel model(missing);
        model.setActive(true);
        QTRY_VERIFY(!model.openError().isEmpty());
        QFile file(QString::fromStdString(missing.utf8string()));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("recovered\n"), 10);
        file.close();
        model.refresh();
        QTRY_COMPARE(model.rowCount(), 1);
        QVERIFY(model.openError().isEmpty());
        model.setLoadLimit(0);
        QCOMPARE(model.loadLimit(), 1);
        model.setLoadLimit(DebugLogReader::kMaxLoadLimit + 1);
        QCOMPARE(model.loadLimit(), DebugLogReader::kMaxLoadLimit);
    }
};

BITCOINQML_REGISTER_QT_TEST(DebugLogTests)
#include <test_debuglog.moc>
