// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Tests for RpcConsoleModel history navigation logic.
//
// RpcConsoleModel::browseHistory(), resetHistoryNavigation(), and the history
// append logic in submitCommand() do not use the node interface at all.  The
// worker-thread dispatch in submitCommand() is a QueuedConnection invoke, so
// with no Qt event loop running the worker's execute() slot is never called
// and executeRpc() is never reached.  A minimal RpcTestStubNode that compiles is
// therefore sufficient.

#include <coins.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <qml/models/rpcconsolemodel.h>
#include <test/mocks/stubnode.h>
#include <univalue.h>
#include <util/result.h>
#include <util/translation.h>

#include <QMutex>
#include <QSemaphore>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using RpcTestStubNode = StubNode;

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
// RpcTestStubNode subclass that returns a large result from executeRpc, used to test
// the output-truncation logic in RpcConsoleWorker::execute().
class LargeResultNode : public RpcTestStubNode
{
public:
    UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override
    {
        // Return a 100,000-character string — well above the 50,000-char cap.
        return UniValue(std::string(100'000, 'x'));
    }
};

// RpcTestStubNode that returns a JSON object with a string value containing ": ",
// used to exercise the JSON reply formatter's structural key detection.
class JsonReplyNode : public RpcTestStubNode
{
public:
    UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override
    {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("key1", "value1");
        obj.pushKV("note", "see section:");
        obj.pushKV("key3", "value3");
        return obj;
    }
};

// RpcTestStubNode that provides a known command list for autocomplete tests.
class CommandListNode : public RpcTestStubNode
{
public:
    std::vector<std::string> listRpcCommands() override
    {
        return {"getblockcount", "getblock", "help"};
    }
};

// RpcTestStubNode whose executeRpc() blocks the worker thread until the test
// releases it, so the model stays in the executing state deterministically.
// A timeout backstops the wait so a forgotten release() can never hang the suite.
class BlockingNode : public RpcTestStubNode
{
public:
    UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override
    {
        m_gate.tryAcquire(1, 5000);
        return {};
    }
    void release() { m_gate.release(); }

private:
    QSemaphore m_gate{0};
};

// RpcTestStubNode whose long-running command blocks the worker thread until
// "stop" is executed. "stop" records that it ran and releases the blocked
// command, modelling shutdown aborting an in-flight RPC. This only works if
// "stop" runs on the caller's thread: a worker-queued "stop" would sit behind
// the blocked command and never release it.
class StopAbortNode : public RpcTestStubNode
{
public:
    UniValue executeRpc(const std::string& method, const UniValue&, const std::string&) override
    {
        if (method == "stop") {
            m_stop_executed = true;
            m_gate.release();
            return {};
        }
        m_gate.tryAcquire(1, 5000);
        return {};
    }
    bool stopExecuted() const { return m_stop_executed; }

private:
    QSemaphore m_gate{0};
    std::atomic<bool> m_stop_executed{false};
};

// RpcTestStubNode that records the URI executeRpc() is invoked with, so tests can
// assert that a wallet name is translated into a /wallet/<name> endpoint.
class UriCapturingNode : public RpcTestStubNode
{
public:
    UniValue executeRpc(const std::string&, const UniValue&, const std::string& uri) override
    {
        QMutexLocker lock(&m_mutex);
        m_last_uri = QString::fromStdString(uri);
        return {};
    }
    QString lastUri()
    {
        QMutexLocker lock(&m_mutex);
        return m_last_uri;
    }

private:
    QMutex m_mutex;
    QString m_last_uri;
};

// submitCommand() now refuses a new command while one is in flight (matching Qt
// Widgets), so the asynchronous worker round-trip must complete before the next
// submit. The stub node's executeRpc() returns instantly, so this settles on the
// first event-loop pass; a rejected/idle command leaves executing() false and the
// wait returns immediately.
static void submitAndSettle(RpcConsoleModel& model, const QString& command)
{
    model.submitCommand(command);
    QTRY_VERIFY(!model.executing());
}

static int roleForName(QAbstractItemModel* model, const QByteArray& name)
{
    const auto roles = model->roleNames();
    for (auto it = roles.constBegin(); it != roles.constEnd(); ++it) {
        if (it.value() == name) return it.key();
    }
    return -1;
}

class RpcConsoleModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void historyNavigationOldestToNewest();
    void historyNavigationRestoresPendingText();
    void historyDeduplicatesConsecutiveDuplicates();
    void historyTruncatesAtMax();
    void resetHistoryNavigationClearsState();
    void submitRefusedWhileExecuting();
    void stopRunsSynchronouslyWhileExecuting();
    void walletNameScopesRpcToWalletUri();
    void outputRowsExposeCategoryAndRawTimestamp();
    void clearRemovesOutput();
    void outputTruncatedWhenResultTooLong();
    void jsonReplyKeyColoringSkipsStringsContainingColons();
    void availableCommandsIncludesHelpVariants();
};

void RpcConsoleModelTests::historyNavigationOldestToNewest()
{
    RpcTestStubNode mock;
    RpcConsoleModel model{mock};

    // Each command must settle before the next, since submitCommand() refuses a
    // new command while one is executing.
    submitAndSettle(model, "cmd_a");
    submitAndSettle(model, "cmd_b");
    submitAndSettle(model, "cmd_c");

    // Navigate backward (direction=1 → older)
    QCOMPARE(model.browseHistory(1, ""), QString("cmd_c"));
    QCOMPARE(model.browseHistory(1, ""), QString("cmd_b"));
    QCOMPARE(model.browseHistory(1, ""), QString("cmd_a"));

    // Clamped at oldest — should stay at cmd_a
    QCOMPARE(model.browseHistory(1, ""), QString("cmd_a"));

    // Navigate forward (direction=-1 → newer)
    QCOMPARE(model.browseHistory(-1, ""), QString("cmd_b"));
    QCOMPARE(model.browseHistory(-1, ""), QString("cmd_c"));

    // Past the newest end → restore pending text (empty string we passed in)
    QCOMPARE(model.browseHistory(-1, ""), QString(""));
}

void RpcConsoleModelTests::historyNavigationRestoresPendingText()
{
    RpcTestStubNode mock;
    RpcConsoleModel model{mock};

    submitAndSettle(model, "getblockcount");
    submitAndSettle(model, "help");

    const QString pending = "getblock";
    QCOMPARE(model.browseHistory(1, pending), QString("help"));
    QCOMPARE(model.browseHistory(1, pending), QString("getblockcount"));
    // Navigate back past newest → pending text is restored
    QCOMPARE(model.browseHistory(-1, pending), QString("help"));
    QCOMPARE(model.browseHistory(-1, pending), QString("getblock"));
}

void RpcConsoleModelTests::historyDeduplicatesConsecutiveDuplicates()
{
    RpcTestStubNode mock;
    RpcConsoleModel model{mock};

    submitAndSettle(model, "getblockcount");
    submitAndSettle(model, "getblockcount"); // duplicate — should not be added
    submitAndSettle(model, "getblockcount"); // duplicate — should not be added
    submitAndSettle(model, "help");

    // History should be: [getblockcount, help]
    QCOMPARE(model.browseHistory(1, ""), QString("help"));
    QCOMPARE(model.browseHistory(1, ""), QString("getblockcount"));
    // No more entries
    QCOMPARE(model.browseHistory(1, ""), QString("getblockcount"));
}

void RpcConsoleModelTests::historyTruncatesAtMax()
{
    RpcTestStubNode mock;
    RpcConsoleModel model{mock};

    // Submit 51 unique commands — only the last 50 should be kept.
    for (int i = 0; i < 51; ++i) {
        submitAndSettle(model, QString("cmd_%1").arg(i));
    }

    // Navigate to oldest: should be cmd_1 (cmd_0 was evicted), newest cmd_50.
    // Walk all the way back to the oldest.
    QString last;
    for (int i = 0; i < 50; ++i) {
        last = model.browseHistory(1, "");
    }
    QCOMPARE(last, QString("cmd_1")); // cmd_0 was evicted
}

void RpcConsoleModelTests::resetHistoryNavigationClearsState()
{
    RpcTestStubNode mock;
    RpcConsoleModel model{mock};

    submitAndSettle(model, "getblockcount");
    submitAndSettle(model, "help");

    // Start navigating
    QCOMPARE(model.browseHistory(1, "typing..."), QString("help"));

    // Reset — next browseHistory should restart from the top
    model.resetHistoryNavigation();
    QCOMPARE(model.browseHistory(1, ""), QString("help"));
}

void RpcConsoleModelTests::submitRefusedWhileExecuting()
{
    BlockingNode mock;
    RpcConsoleModel model{mock};

    // First command occupies the worker thread and blocks there.
    QVERIFY(model.submitCommand("waitfornewblock"));
    QVERIFY(model.executing());

    // A second, non-"stop" command must be refused while one is in flight, so
    // keyboard Enter cannot queue commands behind the disabled Run button.
    QVERIFY(!model.submitCommand("getblockcount"));

    // Let the worker finish and the model settle.
    mock.release();
    QTRY_VERIFY(!model.executing());

    // The refused command never entered history — only the first remains.
    QCOMPARE(model.browseHistory(1, ""), QString("waitfornewblock"));
    QCOMPARE(model.browseHistory(1, ""), QString("waitfornewblock")); // clamped: no 2nd entry
}

void RpcConsoleModelTests::stopRunsSynchronouslyWhileExecuting()
{
    StopAbortNode mock;
    RpcConsoleModel model{mock};

    // A long-running command blocks the worker thread.
    QVERIFY(model.submitCommand("waitfornewblock"));
    QVERIFY(model.executing());

    // "stop" is exempt from the executing guard and runs synchronously on the
    // calling thread (matching Core), so its effect is already visible here
    // without spinning the event loop. A worker-queued "stop" would not have run
    // yet, since the worker is blocked on the in-flight command.
    QVERIFY(model.submitCommand("stop"));
    QVERIFY(mock.stopExecuted());

    // The synchronous "stop" released the blocked command, so the model settles.
    QTRY_VERIFY(!model.executing());
}

void RpcConsoleModelTests::walletNameScopesRpcToWalletUri()
{
    UriCapturingNode mock;
    RpcConsoleModel model{mock};

    // A non-empty wallet name is percent-encoded into a /wallet/<name> endpoint.
    model.submitCommand("getwalletinfo", "my wallet");
    QTRY_VERIFY(!model.executing());
    QCOMPARE(mock.lastUri(), QString("/wallet/my%20wallet"));

    // An empty wallet name runs the command without a wallet endpoint.
    model.submitCommand("getblockcount", "");
    QTRY_VERIFY(!model.executing());
    QCOMPARE(mock.lastUri(), QString());
}

void RpcConsoleModelTests::outputRowsExposeCategoryAndRawTimestamp()
{
    RpcTestStubNode mock;
    RpcConsoleModel model{mock};

    auto* out = qobject_cast<QAbstractListModel*>(model.outputModel());
    QVERIFY(out != nullptr);
    const int timestamp_role = roleForName(out, "timestamp");
    const int content_role = roleForName(out, "content");
    const int category_role = roleForName(out, "category");
    QVERIFY(timestamp_role != -1);
    QVERIFY(content_role != -1);
    QVERIFY(category_role != -1);

    QVERIFY(model.submitCommand("getblockcount"));
    QCOMPARE(out->rowCount(), 1);

    const QModelIndex request_index = out->index(0, 0);
    const QString timestamp = out->data(request_index, timestamp_role).toString();
    QCOMPARE(timestamp.size(), 8);
    QVERIFY(!timestamp.startsWith("["));
    QVERIFY(!timestamp.endsWith("]"));
    QCOMPARE(out->data(request_index, category_role).toInt(), int(RpcConsoleModel::CMD_REQUEST));

    const QString request_html = out->data(request_index, content_role).toString();
    QVERIFY(request_html.contains("getblockcount"));
    QVERIFY(!request_html.contains("&gt;&gt;"));

    QTRY_VERIFY(!model.executing());
    QCOMPARE(out->rowCount(), 2);
    QCOMPARE(out->data(out->index(1, 0), category_role).toInt(), int(RpcConsoleModel::CMD_REPLY));
}

void RpcConsoleModelTests::clearRemovesOutput()
{
    RpcTestStubNode mock;
    RpcConsoleModel model{mock};

    auto* out = qobject_cast<QAbstractListModel*>(model.outputModel());
    QVERIFY(out != nullptr);
    submitAndSettle(model, "getblockcount");
    QVERIFY(out->rowCount() > 0);

    model.clear();
    QCOMPARE(out->rowCount(), 0);
}

void RpcConsoleModelTests::outputTruncatedWhenResultTooLong()
{
    LargeResultNode mock;
    RpcConsoleModel model{mock};

    auto* out = qobject_cast<QAbstractListModel*>(model.outputModel());
    QVERIFY(out != nullptr);
    QSignalSpy spy{out, &QAbstractItemModel::rowsInserted};

    // submitCommand inserts the CMD_REQUEST row synchronously, then dispatches
    // the execute() slot to the worker thread via QueuedConnection.
    model.submitCommand("getblockcount");
    QCOMPARE(out->rowCount(), 1); // CMD_REQUEST arrived synchronously

    // Process events until the worker thread's CMD_REPLY row is inserted
    // (or until the 5-second safety timeout expires).
    QVERIFY(spy.wait(5000));
    QCOMPARE(out->rowCount(), 2);

    // Look up the content role dynamically — the model's roleNames map
    // "content" to the ContentRole integer.
    int content_role = roleForName(out, "content");
    QVERIFY(content_role != -1);

    const QString reply_html = out->data(out->index(1, 0), content_role).toString();

    // Formatted reply must be well below the raw 100,000-character input.
    QVERIFY2(reply_html.size() < 100'000,
             qPrintable(QString("reply_html size was %1").arg(reply_html.size())));
    // The truncation notice must appear in the output.
    QVERIFY2(reply_html.contains("truncated"),
             "Expected truncation notice in output");
}

void RpcConsoleModelTests::jsonReplyKeyColoringSkipsStringsContainingColons()
{
    JsonReplyNode mock;
    RpcConsoleModel model{mock};

    auto* out = qobject_cast<QAbstractListModel*>(model.outputModel());
    QVERIFY(out != nullptr);
    QSignalSpy spy{out, &QAbstractItemModel::rowsInserted};

    model.submitCommand("getsomething");
    QCOMPARE(out->rowCount(), 1); // CMD_REQUEST arrived synchronously
    QVERIFY(spy.wait(5000));
    QCOMPARE(out->rowCount(), 2);

    int content_role = roleForName(out, "content");
    QVERIFY(content_role != -1);

    const QString html = out->data(out->index(1, 0), content_role).toString();

    // Real keys are wrapped in a key-colour span that closes immediately
    // after the quoted key — the structural formatter produces literally
    // `"key1"</span>` at the boundary.
    QVERIFY2(html.contains(QStringLiteral("\"key1\"</span>")),
             qPrintable("Missing coloured wrap for key1 in:\n" + html));
    QVERIFY2(html.contains(QStringLiteral("\"note\"</span>")),
             qPrintable("Missing coloured wrap for note in:\n" + html));
    QVERIFY2(html.contains(QStringLiteral("\"key3\"</span>")),
             qPrintable("Missing coloured wrap for key3 in:\n" + html));

    // The string value `"see section:"` must NOT close a key-colour span.
    // The old regex-based formatter incorrectly treated this as a key.
    QVERIFY2(!html.contains(QStringLiteral("\"see section\"</span>")),
             qPrintable("String value was mis-coloured as a key in:\n" + html));
}

void RpcConsoleModelTests::availableCommandsIncludesHelpVariants()
{
    CommandListNode mock;
    RpcConsoleModel model{mock};

    // Populate availableCommands via the public slot.
    model.onNodeInitialized();

    QStringList cmds = model.availableCommands();

    // Original commands present
    QVERIFY(cmds.contains("getblockcount"));
    QVERIFY(cmds.contains("getblock"));
    QVERIFY(cmds.contains("help"));

    // "help <cmd>" variants present
    QVERIFY(cmds.contains("help getblockcount"));
    QVERIFY(cmds.contains("help getblock"));
    QVERIFY(cmds.contains("help help"));

    // "help-console" present
    QVERIFY(cmds.contains("help-console"));

    // List is sorted (case-insensitive)
    for (int i = 1; i < cmds.size(); ++i) {
        QVERIFY2(cmds[i - 1].compare(cmds[i], Qt::CaseInsensitive) <= 0,
                 qPrintable(QString("Not sorted: '%1' > '%2'").arg(cmds[i - 1], cmds[i])));
    }

    // No duplicates
    QStringList deduped = cmds;
    deduped.removeDuplicates();
    QCOMPARE(cmds.size(), deduped.size());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(RpcConsoleModelTests)
#else
QTEST_MAIN(RpcConsoleModelTests)
#endif
#include "test_rpcconsolemodel.moc"
