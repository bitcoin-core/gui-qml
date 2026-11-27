// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// RPC work and delivery use separate executor and GUI turns. Tests cover both
// manual transport ordering and the actual independently routed worker lanes.

#include <coins.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <qml/asyncjoin.h>
#include <qml/models/rpcconsolemodel.h>
#include <test/mocks/stubnode.h>
#include <QTextDocument>
#include <QScopeGuard>
#include <univalue.h>
#include <util/result.h>
#include <util/translation.h>

#include <QMutex>
#include <QSemaphore>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

using RpcTestStubNode = StubNode;

namespace {
class ManualConsoleExecution
{
    using Turn = std::function<void()>;
    std::map<execution::Lane, std::deque<Turn>> m_turns;
    std::deque<Turn> m_gui;

    static bool step(std::deque<Turn>& turns)
    {
        if (turns.empty()) return false;
        auto turn = std::move(turns.front());
        turns.pop_front();
        turn();
        return true;
    }

public:
    bool reject_rpc{false};
    Turn on_rpc_admission;
    execution::Executor executor{[this](execution::Lane lane, execution::UtilityWorker, Turn turn) {
        if (lane == execution::Lane::Rpc && std::exchange(reject_rpc, false)) return false;
        m_turns[lane].push_back(std::move(turn));
        if (lane == execution::Lane::Rpc) {
            if (auto hook = std::exchange(on_rpc_admission, {})) hook();
        }
        return true;
    }};

    ~ManualConsoleExecution() { drain(); }

    bool step(execution::Lane lane) { return step(m_turns[lane]); }
    bool step() { return step(execution::Lane::Rpc) || step(execution::Lane::Priority) || step(execution::Lane::Utility); }
    void drain() { while (step()) {} }
    bool quiescent() const
    {
        return std::all_of(m_turns.begin(), m_turns.end(), [](const auto& lane) { return lane.second.empty(); });
    }
    void post(Turn turn) { m_gui.push_back(std::move(turn)); }
    bool publishNext() { return step(m_gui); }
    void publishAll() { while (publishNext()) {} }
};
} // namespace

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

class StructuredReplyNode : public RpcTestStubNode
{
public:
    explicit StructuredReplyNode(UniValue reply) : m_reply(std::move(reply)) {}
    UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override { return m_reply; }

private:
    UniValue m_reply;
};

// RpcTestStubNode that provides a known command list for autocomplete tests.
class CommandListNode : public RpcTestStubNode
{
public:
    std::vector<std::string> listRpcCommands() override
    {
        m_list_on_gui = QThread::currentThread() == QCoreApplication::instance()->thread();
        return {"getblockcount", "getblock", "help"};
    }
    bool listOnGui() const { return m_list_on_gui; }

private:
    std::atomic<bool> m_list_on_gui{true};
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
// command, modelling shutdown aborting an in-flight RPC. Stop must use an
// independent background lane, without occupying the GUI or blocked RPC lane.
class StopAbortNode : public RpcTestStubNode
{
public:
    UniValue executeRpc(const std::string& method, const UniValue&, const std::string&) override
    {
        if (method == "stop") {
            m_stop_on_gui = QThread::currentThread() == QCoreApplication::instance()->thread();
            m_stop_executed = true;
            m_gate.release();
            return {};
        }
        m_wait_entered = true;
        m_wait_released = m_gate.tryAcquire(1, 5000);
        return {};
    }
    bool waitEntered() const { return m_wait_entered; }
    bool waitReleased() const { return m_wait_released; }
    bool stopExecuted() const { return m_stop_executed; }
    bool stopOnGui() const { return m_stop_on_gui; }

private:
    QSemaphore m_gate{0};
    std::atomic<bool> m_wait_entered{false}, m_wait_released{false};
    std::atomic<bool> m_stop_executed{false};
    std::atomic<bool> m_stop_on_gui{false};
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

// submitNodeCommand() refuses a new command while one is in flight (matching Qt
// Widgets), so the asynchronous worker round-trip must complete before the next
// submit. The stub node's executeRpc() returns instantly, so this settles on the
// first event-loop pass; a rejected/idle command leaves executing() false and the
// wait returns immediately.
static void submitAndSettle(RpcConsoleModel& model, const QString& command)
{
    model.submitNodeCommand(command);
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

// Reconstruct the formatted reply for the structural HTML oracle. Physical
// line records introduce <br>; continuation records retain the same line.
static QString replyHtml(QAbstractItemModel* output)
{
    QString result;
    bool first{true};
    for (int i = 0; i < output->rowCount(); ++i) {
        const auto index = output->index(i, 0);
        if (output->data(index, RpcOutputListModel::CategoryRole).toInt() != RpcConsoleModel::CMD_REPLY) continue;
        if (!first && !output->data(index, RpcOutputListModel::ContinuationRole).toBool()) result += QStringLiteral("<br>");
        first = false;
        result += output->data(index, RpcOutputListModel::ContentRole).toString();
    }
    return result;
}

class RpcConsoleModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void explicitTargetsReplaceAmbiguousConsoleIngress()
    {
        const auto& meta = RpcConsoleModel::staticMetaObject;
        QCOMPARE(meta.indexOfMethod("submitCommand(QString,QString)"), -1);
        QVERIFY(meta.indexOfMethod("submitNodeCommand(QString)") >= 0);
        QVERIFY(meta.indexOfMethod("submitWalletCommand(QString,QString)") >= 0);
        QVERIFY(meta.indexOfMethod("submitInput(QString,bool,QString,bool)") >= 0);
    }
    void unavailableCleanupRejectsConsoleCommands();
    void parentDestructionRetiresMailboxBeforeFirstCommand();
    void cleanup() {
        bool done{false}; WaitForThreadJoins(this, [&] { done = true; });
        QTRY_VERIFY(done);
    }
    void manualExecutionSeparatesEffectOutcomeAndPublication();
    void acceptedCommandSurvivesObserverLoss();
    void sharedSchedulerClosureWaitsForActualNativeRetirement();
    void aggregateOutputAndRowsStayBounded();
    void historyNavigationOldestToNewest();
    void historyNavigationRestoresPendingText();
    void historyRecallIsRefusedOnceWhenTheWalletTargetChanged();
    void historyRecallRunsAtOnceForTheSameTargetOrAnEdit();
    void historyDeduplicatesConsecutiveDuplicates();
    void historyTruncatesAtMax();
    void resetHistoryNavigationClearsState();
    void submitRefusedWhileExecuting();
    void stopRunsIndependentlyWhileExecuting();
    void stopDuringWarmupReportsRpcErrorWithoutAborting();
    void walletNameScopesRpcToWalletUri();
    void explicitContextDistinguishesDefaultWalletFromNode();
    void outputRowsExposeCategoryAndRawTimestamp();
    void clearRemovesOutput();
    void clearDuringExecuteKeepsAcceptedOutcome_data();
    void clearDuringExecuteKeepsAcceptedOutcome();
    void clearResetsWalletBannerAndHistoryCursor();
    void clearAfterPartialProjectionDoesNotStrandBusy();
    void commandOutcomeSurvivesObserverLoss();
    void rejectedAndDuplicateInputsHaveNoEffect();
    void acceptedAdmissionKeepsOriginalOwner_data();
    void acceptedAdmissionKeepsOriginalOwner();
    void unknownEffectIsNotReplayed();
    void acceptedCancellationStaysKnownNotStarted();
    void acceptedParseFailureKeepsOriginalWallet();
    void acceptedRpcDeliversOnUtilityAfterExecutorCloses();
    void unavailableWalletNeverUsesNodeEndpoint();
    void unavailableInputKeepsIndependentStop();
    void publicOutputAppendCannotExceedBudgets();
    void outputTruncatedWhenResultTooLong();
    void jsonReplyKeyColoringSkipsStringsContainingColons();
    void jsonReplyPreservesNestedFormatting_data();
    void jsonReplyPreservesNestedFormatting();
    void jsonReplyTraversal_data();
    void jsonReplyTraversal();
    void deeplyNestedJsonReply();
    void availableCommandsIncludesHelpVariants();
    void autocompleteRejectsStaleManualResults();
    void secretRequestsAreNotRetained_data();
    void secretRequestsAreNotRetained();
    void argumentConversionDiagnosticsFollowRequestRedaction_data();
    void argumentConversionDiagnosticsFollowRequestRedaction();
};

void RpcConsoleModelTests::parentDestructionRetiresMailboxBeforeFirstCommand()
{
    struct Node final : RpcTestStubNode {
        int effects{0}, metadata{0};
        UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override { ++effects; return UniValue{7}; }
        std::vector<std::string> listRpcCommands() override { ++metadata; return {"help"}; }
    } node;
    ManualConsoleExecution env;
    auto parent = std::make_unique<QObject>();
    const QPointer<RpcConsoleModel> model = new RpcConsoleModel(node, env.executor,
        [&](auto turn) { env.post(std::move(turn)); }, parent.get());
    QCOMPARE(parent->children().size(), 1);
    QVERIFY(env.quiescent());
    parent.reset();
    QVERIFY(!model);
    QVERIFY(env.step(execution::Lane::Utility));
    env.drain();
    env.publishAll();
    QCOMPARE(node.effects, 0);
    QCOMPARE(node.metadata, 0);
    QVERIFY(env.quiescent());
}

void RpcConsoleModelTests::unavailableCleanupRejectsConsoleCommands()
{
    RpcTestStubNode node;
    ManualConsoleExecution env;
    env.executor.stopAndJoin();
    RpcConsoleModel model{node, env.executor, [&](auto turn) { env.post(std::move(turn)); }};
    QVERIFY(!model.completionError().isEmpty());
    QVERIFY(!model.submitNodeCommand("getblockcount"));
    QVERIFY(env.quiescent());
    QSignalSpy drained{&model, &RpcConsoleModel::drained};
    model.beginShutdown();
    QCOMPARE(drained.size(), 1);
}

void RpcConsoleModelTests::manualExecutionSeparatesEffectOutcomeAndPublication()
{
    struct Node final : RpcTestStubNode {
        int effects{0};
        UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override { ++effects; return UniValue{7}; }
    } node;
    ManualConsoleExecution env;
    RpcConsoleModel model{node, env.executor, [&](auto callback) { env.post(std::move(callback)); }};
    const auto settle = qScopeGuard([&] { env.drain(); env.publishAll(); });
    QVERIFY(model.submitNodeCommand("getblockcount"));
    QCOMPARE(node.effects, 0); QVERIFY(model.executing()); QCOMPARE(model.outputModel()->rowCount(), 0);
    QVERIFY(env.step());
    QCOMPARE(node.effects, 1); QVERIFY(model.executing()); QCOMPARE(model.outputModel()->rowCount(), 0);
    QVERIFY(env.step()); // Utility owns the actual outcome, GUI still untouched.
    QCOMPARE(node.effects, 1); QCOMPARE(model.outputModel()->rowCount(), 0);
    env.publishAll();
    QVERIFY(!model.executing()); QCOMPARE(model.outputModel()->rowCount(), 2);
    QCOMPARE(replyHtml(model.outputModel()), QStringLiteral("7"));
}

void RpcConsoleModelTests::acceptedCommandSurvivesObserverLoss()
{
    struct Node final : RpcTestStubNode {
        int effects{0};
        UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override { ++effects; return {}; }
    } node;
    ManualConsoleExecution env;
    auto model = std::make_unique<RpcConsoleModel>(node, env.executor, [&](auto callback) { env.post(std::move(callback)); });
    QVERIFY(model->submitNodeCommand("getblockcount"));
    model.reset(); // No nested GUI loop or cancellation of the accepted effect.
    QCOMPARE(node.effects, 0);
    env.drain(); env.publishAll();
    QCOMPARE(node.effects, 1); QVERIFY(env.quiescent());
}

void RpcConsoleModelTests::sharedSchedulerClosureWaitsForActualNativeRetirement()
{
    struct Node final : RpcTestStubNode {
        int effects{0};
        UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override {
            ++effects; return UniValue{std::string(200'000, 'r')};
        }
    } node;
    ManualConsoleExecution env;
    RpcConsoleModel model{node, env.executor, [&](auto turn) { env.post(std::move(turn)); }};
    QSignalSpy drained{&model, &RpcConsoleModel::drained};
    QVERIFY(model.submitNodeCommand("getblockcount"));
    const auto receipt = model.commandReceipt();
    QVERIFY(receipt); QVERIFY(!receipt->result()); QCOMPARE(node.effects, 0);
    model.beginShutdown();
    QVERIFY(!model.submitNodeCommand("getblockcount")); QCOMPARE(drained.size(), 0);
    QVERIFY(env.step(execution::Lane::Rpc)); QCOMPARE(node.effects, 1);
    env.publishAll(); QCOMPARE(drained.size(), 0); QVERIFY(!receipt->result());
    QVERIFY(env.step(execution::Lane::Utility));
    env.publishAll();
    QVERIFY(receipt->result()); QCOMPARE(drained.size(), 0); // Outcomes do not acknowledge queue retirement.
    env.drain(); QVERIFY(env.quiescent());
    env.publishAll();
    QCOMPARE(drained.size(), 1); QCOMPARE(model.outputModel()->rowCount(), 0);
    model.beginShutdown(); QCOMPARE(drained.size(), 1); QCOMPARE(node.effects, 1);
}

void RpcConsoleModelTests::aggregateOutputAndRowsStayBounded()
{
    LargeResultNode node;
    RpcConsoleModel model{node};
    for (int command = 0; command < 50; ++command) submitAndSettle(model, "getblockcount");
    auto* output = model.outputModel();
    QVERIFY(output->rowCount() <= RpcOutputListModel::kMaxRows);
    qsizetype characters{0};
    for (int row = 0; row < output->rowCount(); ++row) characters += output->data(output->index(row, 0), RpcOutputListModel::ContentRole).toString().size();
    QVERIFY(characters <= RpcOutputListModel::kMaxCharacters);
    QVERIFY(characters > 1'000'000); // Exercise eviction, never an empty pass.
}

void RpcConsoleModelTests::autocompleteRejectsStaleManualResults()
{
    CommandListNode node;
    ManualConsoleExecution env;
    RpcConsoleModel model{node, env.executor, [&](auto callback) { env.post(std::move(callback)); }};
    const auto settle = qScopeGuard([&] { env.drain(); env.publishAll(); });
    model.onNodeInitialized(); env.drain(); env.publishAll();
    QVERIFY(!model.availableCommands().isEmpty());
    model.setCompletionPrefix("getb");
    QVERIFY(model.completionBusy()); QCOMPARE(model.completionSuggestions().size(), 0);
    env.drain(); // Older match set is retained in the mailbox, not yet GUI.
    model.setCompletionPrefix("help getblockc");
    env.publishAll();
    QVERIFY(model.completionBusy()); QVERIFY(model.completionSuggestions().isEmpty());
    env.drain(); env.publishAll();
    QVERIFY(!model.completionBusy());
    QCOMPARE(model.completionSuggestions(), QStringList{QStringLiteral("help getblockcount")});
}

void RpcConsoleModelTests::historyNavigationOldestToNewest()
{
    RpcTestStubNode mock;
    RpcConsoleModel model{mock};

    // Each command must settle before the next, since submitNodeCommand() refuses a
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

// #1131: a recalled wallet command never silently moves to another wallet.
// The first Enter names both wallets and runs nothing; a second Enter runs it
// in the current target.
void RpcConsoleModelTests::historyRecallIsRefusedOnceWhenTheWalletTargetChanged()
{
    UriCapturingNode mock;
    RpcConsoleModel model{mock};
    QVERIFY(model.submitInput("getbalance", true, "alpha", true));
    QTRY_VERIFY(!model.executing());
    QCOMPARE(mock.lastUri(), QString("/wallet/alpha"));

    QCOMPARE(model.browseHistory(1, ""), QString("getbalance"));
    QVERIFY(!model.submitInput("getbalance", true, "beta", true));
    QVERIFY(!model.executing());
    QCOMPARE(mock.lastUri(), QString("/wallet/alpha"));
    QVERIFY2(model.completionError().contains("\"alpha\"") && model.completionError().contains("\"beta\""), qPrintable(model.completionError()));

    QVERIFY(model.submitInput("getbalance", true, "beta", true));
    QTRY_VERIFY(!model.executing());
    QCOMPARE(mock.lastUri(), QString("/wallet/beta"));

    // The same text is now recorded for beta too, so recalling it there runs at once.
    QCOMPARE(model.browseHistory(1, ""), QString("getbalance"));
    QVERIFY(model.submitInput("getbalance", true, "beta", true));
    QTRY_VERIFY(!model.executing());
    QCOMPARE(mock.lastUri(), QString("/wallet/beta"));

    // A wallet command recalled while the console has no wallet is guarded too.
    QCOMPARE(model.browseHistory(1, ""), QString("getbalance"));
    QVERIFY(!model.submitInput("getbalance", false, QString{}, true));
    QVERIFY(model.completionError().contains("no wallet"));
}

void RpcConsoleModelTests::historyRecallRunsAtOnceForTheSameTargetOrAnEdit()
{
    UriCapturingNode mock;
    RpcConsoleModel model{mock};
    QVERIFY(model.submitInput("getbalance", true, "alpha", true));
    QTRY_VERIFY(!model.executing());

    // Same target: no question.
    QCOMPARE(model.browseHistory(1, ""), QString("getbalance"));
    QVERIFY(model.submitInput("getbalance", true, "alpha", true));
    QTRY_VERIFY(!model.executing());
    QCOMPARE(mock.lastUri(), QString("/wallet/alpha"));

    // Editing the recalled text is a new command for the current target.
    QCOMPARE(model.browseHistory(1, ""), QString("getbalance"));
    model.resetHistoryNavigation();
    QVERIFY(model.submitInput("getbalance \"*\"", true, "beta", true));
    QTRY_VERIFY(!model.executing());
    QCOMPARE(mock.lastUri(), QString("/wallet/beta"));

    // A node command recalled in a wallet context is not guarded.
    QVERIFY(model.submitInput("getblockcount", false, QString{}, true));
    QTRY_VERIFY(!model.executing());
    QCOMPARE(model.browseHistory(1, ""), QString("getblockcount"));
    QVERIFY(model.submitInput("getblockcount", true, "alpha", true));
    QTRY_VERIFY(!model.executing());
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
    QVERIFY(model.submitNodeCommand("waitfornewblock"));
    QVERIFY(model.executing());

    // A second, non-"stop" command must be refused while one is in flight, so
    // keyboard Enter cannot queue commands behind the disabled Run button.
    QVERIFY(!model.submitNodeCommand("getblockcount"));

    // Let the worker finish and the model settle.
    mock.release();
    QTRY_VERIFY(!model.executing());

    // The refused command never entered history — only the first remains.
    QCOMPARE(model.browseHistory(1, ""), QString("waitfornewblock"));
    QCOMPARE(model.browseHistory(1, ""), QString("waitfornewblock")); // clamped: no 2nd entry
}

void RpcConsoleModelTests::stopRunsIndependentlyWhileExecuting()
{
    StopAbortNode mock;
    QSemaphore settings_reached_core;
    execution::Executor executor{execution::Executor::Config{}};
    auto core = executor.makeQueue(execution::Lane::Core);
    RpcConsoleModel model{mock, executor, presentation::QueuedPost()};

    // A long-running command blocks the worker thread.
    QVERIFY(model.submitNodeCommand("waitfornewblock"));
    QVERIFY(model.executing());
    QTRY_VERIFY(mock.waitEntered());
    QVERIFY(core.post([&] { settings_reached_core.release(); }));
    QVERIFY(settings_reached_core.tryAcquire(1, 1500));
    QVERIFY(!mock.waitReleased());

    // Stop is admitted independently of the blocked command. Admission does
    // not claim that Core has already executed the shutdown request.
    QVERIFY(model.submitNodeCommand("stop"));
    QTRY_VERIFY(mock.stopExecuted());
    QVERIFY(!mock.stopOnGui());

    // The independent stop released the blocked command, so the model settles.
    QTRY_VERIFY(!model.executing());
    QVERIFY(mock.waitReleased());
}

void RpcConsoleModelTests::stopDuringWarmupReportsRpcErrorWithoutAborting()
{
    struct WarmupStopNode final : RpcTestStubNode {
        std::atomic<bool> warming_up{true};
        std::atomic<bool> stop_on_gui{true};
        std::atomic<int> stop_calls{0};
        UniValue executeRpc(const std::string& method, const UniValue&, const std::string&) override
        {
            if (method == "stop") {
                stop_on_gui = QThread::currentThread() == QCoreApplication::instance()->thread();
                ++stop_calls;
                if (warming_up.load()) {
                    UniValue error{UniValue::VOBJ};
                    error.pushKV("code", -28);
                    error.pushKV("message", "Loading wallet...");
                    throw error;
                }
                return UniValue{"Bitcoin Core stopping"};
            }
            return {};
        }
    };

    WarmupStopNode mock;
    RpcConsoleModel model{mock};
    auto* output = model.outputModel();

    // Literal "stop" during Core warmup must run off the GUI thread and report
    // RPC_IN_WARMUP as an error row instead of aborting on an uncaught UniValue.
    QVERIFY(model.submitNodeCommand("stop"));
    QTRY_COMPARE(mock.stop_calls.load(), 1);
    QVERIFY(!mock.stop_on_gui.load());

    const auto saw_warmup_error = [&] {
        for (int i = 0; i < output->rowCount(); ++i) {
            const auto index = output->index(i, 0);
            if (output->data(index, RpcOutputListModel::CategoryRole).toInt() == RpcConsoleModel::CMD_ERROR &&
                output->data(index, RpcOutputListModel::ContentRole).toString().contains("Loading wallet... (code -28)")) {
                return true;
            }
        }
        return false;
    };
    QTRY_VERIFY(saw_warmup_error());

    // Once warmup finishes, a subsequent "stop" is accepted and succeeds.
    mock.warming_up = false;
    bool retried{false};
    QTRY_VERIFY(retried || (retried = model.submitNodeCommand("stop")));
    QTRY_COMPARE(mock.stop_calls.load(), 2);
    QVERIFY(!mock.stop_on_gui.load());
    QTRY_VERIFY(replyHtml(output).contains("Bitcoin Core stopping"));
}

void RpcConsoleModelTests::walletNameScopesRpcToWalletUri()
{
    UriCapturingNode mock;
    RpcConsoleModel model{mock};

    // A non-empty wallet name is percent-encoded into a /wallet/<name> endpoint.
    model.submitWalletCommand("getwalletinfo", "my wallet");
    QTRY_VERIFY(!model.executing());
    QCOMPARE(mock.lastUri(), QString("/wallet/my%20wallet"));

    // An empty wallet name runs the command without a wallet endpoint.
    model.submitNodeCommand("getblockcount");
    QTRY_VERIFY(!model.executing());
    QCOMPARE(mock.lastUri(), QString());
}

void RpcConsoleModelTests::explicitContextDistinguishesDefaultWalletFromNode()
{
    UriCapturingNode mock;
    RpcConsoleModel model{mock};

    // An explicitly selected wallet with an empty Core name is the default
    // wallet endpoint, not the node endpoint.
    QVERIFY(model.submitWalletCommand("getwalletinfo", ""));
    QTRY_VERIFY(!model.executing());
    QCOMPARE(mock.lastUri(), QString("/wallet/"));

    QVERIFY(model.submitNodeCommand("getblockcount"));
    QTRY_VERIFY(!model.executing());
    QCOMPARE(mock.lastUri(), QString());

    QVERIFY(model.submitWalletCommand("getwalletinfo", "my wallet"));
    QTRY_VERIFY(!model.executing());
    QCOMPARE(mock.lastUri(), QString("/wallet/my%20wallet"));
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

    QVERIFY(model.submitNodeCommand("getblockcount"));
    QTRY_VERIFY(out->rowCount() >= 1);

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

void RpcConsoleModelTests::clearDuringExecuteKeepsAcceptedOutcome_data()
{
    QTest::addColumn<int>("stage");
    QTest::newRow("before-effect") << 0;
    QTest::newRow("effect-before-domain") << 1;
    QTest::newRow("domain-before-gui") << 2;
}

void RpcConsoleModelTests::clearDuringExecuteKeepsAcceptedOutcome()
{
    QFETCH(int, stage);
    struct Node final : RpcTestStubNode {
        int effects{0};
        UniValue executeRpc(const std::string&, const UniValue&, const std::string& uri) override {
            ++effects; if (uri != "/wallet/old") throw std::runtime_error("Wrong target"); return UniValue{9};
        }
    } node;
    ManualConsoleExecution env;
    RpcConsoleModel model{node, env.executor, [&](auto turn) { env.post(std::move(turn)); }};
    const auto settle = qScopeGuard([&] { env.drain(); env.publishAll(); });
    QVERIFY(model.submitWalletCommand("getbalance", "old"));
    const auto id = model.commandId(), generation = model.outputGeneration();
    const auto receipt = model.commandReceipt(); QVERIFY(receipt);
    for (int i = 0; i < stage; ++i) QVERIFY(env.step());
    model.clear();
    QCOMPARE(model.outputGeneration(), generation + 1);
    QVERIFY(model.executing()); QCOMPARE(model.commandId(), id);
    env.drain(); env.publishAll();
    QCOMPARE(node.effects, 1); QVERIFY(!model.executing()); QCOMPARE(model.outputModel()->rowCount(), 0);
    const auto result = receipt->result(); QVERIFY(result); QVERIFY(result->value); QVERIFY(!result->error);
    QCOMPARE(result->value->command_id, id); QCOMPARE(result->value->generation, generation);
    QVERIFY(result->value->context == RpcCommandContext::ForWallet("old"));
    QVERIFY(result->value->attempted); QCOMPARE(result->value->outcome, RpcCommandResult::Outcome::Succeeded);
    QCOMPARE(model.browseHistory(1, ""), QStringLiteral("getbalance"));
}

void RpcConsoleModelTests::clearResetsWalletBannerAndHistoryCursor()
{
    UriCapturingNode node;
    RpcConsoleModel model{node};
    QVERIFY(model.submitWalletCommand("help one", "named")); QTRY_VERIFY(!model.executing());
    QVERIFY(model.submitWalletCommand("help two", "named")); QTRY_VERIFY(!model.executing());
    QCOMPARE(model.browseHistory(1, "unsent"), QStringLiteral("help two"));
    QCOMPARE(model.browseHistory(1, "help two"), QStringLiteral("help one"));
    model.clear();
    QCOMPARE(model.browseHistory(1, "fresh draft"), QStringLiteral("help two"));
    QCOMPARE(model.browseHistory(-1, "help two"), QStringLiteral("fresh draft"));
    QVERIFY(model.submitWalletCommand("getwalletinfo", "named")); QTRY_VERIFY(!model.executing());
    auto* out = model.outputModel(); QCOMPARE(out->rowCount(), 3);
    QVERIFY(out->data(out->index(0), RpcOutputListModel::ContentRole).toString().contains("named"));
    for (int row = 0; row < out->rowCount(); ++row) {
        QCOMPARE(out->data(out->index(row), RpcOutputListModel::CommandIdRole).toULongLong(), model.commandId());
        QCOMPARE(out->data(out->index(row), RpcOutputListModel::GenerationRole).toULongLong(), model.outputGeneration());
        QVERIFY(out->data(out->index(row), RpcOutputListModel::WalletContextRole).toBool());
        QCOMPARE(out->data(out->index(row), RpcOutputListModel::WalletNameRole).toString(), QStringLiteral("named"));
    }
}

void RpcConsoleModelTests::clearAfterPartialProjectionDoesNotStrandBusy()
{
    LargeResultNode node;
    ManualConsoleExecution env;
    RpcConsoleModel model{node, env.executor, [&](auto turn) { env.post(std::move(turn)); }};
    const auto settle = qScopeGuard([&] { env.drain(); env.publishAll(); });
    QVERIFY(model.submitNodeCommand("getblockcount"));
    env.drain(); QVERIFY(env.publishNext());
    QVERIFY(model.commandReceipt()->result());
    QVERIFY(model.outputModel()->rowCount() > 0); QVERIFY(model.executing());
    model.clear();
    QCOMPARE(model.outputModel()->rowCount(), 0); QVERIFY(!model.executing());
}

void RpcConsoleModelTests::commandOutcomeSurvivesObserverLoss()
{
    UriCapturingNode node;
    ManualConsoleExecution env;
    auto model = std::make_unique<RpcConsoleModel>(node, env.executor, [&](auto turn) { env.post(std::move(turn)); });
    QVERIFY(model->submitWalletCommand("getwalletinfo", "original"));
    const auto receipt = model->commandReceipt(); QVERIFY(receipt); QVERIFY(!receipt->result());
    const auto id = model->commandId(); model.reset();
    env.drain(); env.publishAll(); QVERIFY(env.quiescent());
    QCOMPARE(node.lastUri(), QStringLiteral("/wallet/original"));
    const auto result = receipt->result(); QVERIFY(result); QVERIFY(result->value);
    QCOMPARE(result->value->command_id, id); QVERIFY(result->value->attempted);
    QVERIFY(result->value->context == RpcCommandContext::ForWallet("original"));
}

void RpcConsoleModelTests::rejectedAndDuplicateInputsHaveNoEffect()
{
    struct Node final : RpcTestStubNode {
        int effects{0};
        UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override { ++effects; return {}; }
    } node;
    ManualConsoleExecution env;
    RpcConsoleModel model{node, env.executor, [&](auto turn) { env.post(std::move(turn)); }};
    const auto settle = qScopeGuard([&] { env.drain(); env.publishAll(); });
    QVERIFY(!model.submitNodeCommand(QString(RpcConsoleModel::kMaxInputCharacters + 1, 'x')));
    QVERIFY(!model.executing()); QCOMPARE(node.effects, 0);
    QVERIFY(model.submitNodeCommand("getblockcount"));
    const auto id = model.commandId();
    QVERIFY(!model.submitNodeCommand("getblockcount")); QCOMPARE(model.commandId(), id);
    env.drain(); env.publishAll(); QCOMPARE(node.effects, 1); QVERIFY(!model.executing());
    env.reject_rpc = true;
    QVERIFY(!model.submitNodeCommand("getblockcount"));
    QVERIFY(!model.executing()); QVERIFY(!model.completionError().isEmpty()); QCOMPARE(node.effects, 1);
}

void RpcConsoleModelTests::acceptedAdmissionKeepsOriginalOwner_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("clear") << 0;
    QTest::newRow("shutdown") << 1;
    QTest::newRow("delete") << 2;
}

void RpcConsoleModelTests::acceptedAdmissionKeepsOriginalOwner()
{
    QFETCH(int, mode);
    struct Node final : RpcTestStubNode {
        int effects{0};
        UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override { ++effects; return {}; }
    } node;
    ManualConsoleExecution env;
    auto model = std::make_unique<RpcConsoleModel>(node, env.executor, [&](auto callback) { env.post(std::move(callback)); });
    const auto settle = qScopeGuard([&] { env.drain(); env.publishAll(); });
    std::shared_ptr<const RpcCommandReceipt> original;
    bool duplicate_accepted{true};
    int calls{0};
    env.on_rpc_admission = [&] {
        ++calls;
        original = model->commandReceipt();
        duplicate_accepted = model->submitNodeCommand("getblockcount");
        if (mode == 0) model->clear();
        else if (mode == 1) model->beginShutdown();
        else model.reset();
    };
    auto* const observer = model.get();
    QVERIFY(observer->submitNodeCommand("getblockcount"));
    QCOMPARE(calls, 1); QVERIFY(!duplicate_accepted); QVERIFY(original);
    env.drain(); env.publishAll();
    QCOMPARE(node.effects, 1); QVERIFY(original->result()); QVERIFY(original->result()->value);
    QVERIFY(original->result()->value->attempted);
    if (model) { QCOMPARE(model->outputModel()->rowCount(), 0); if (!mode) QVERIFY(!model->executing()); }
}

void RpcConsoleModelTests::unknownEffectIsNotReplayed()
{
    struct Node final : RpcTestStubNode {
        int effects{0};
        UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override {
            ++effects; throw std::runtime_error("Reply lost after effect");
        }
    } node;
    ManualConsoleExecution env;
    RpcConsoleModel model{node, env.executor, [&](auto turn) { env.post(std::move(turn)); }};
    const auto settle = qScopeGuard([&] { env.drain(); env.publishAll(); });
    QVERIFY(model.submitNodeCommand("getblockcount"));
    const auto receipt = model.commandReceipt();
    env.drain(); env.publishAll(); QVERIFY(!model.executing()); QCOMPARE(node.effects, 1);
    const auto result = receipt->result(); QVERIFY(result); QVERIFY(result->value);
    QVERIFY(result->value->attempted); QCOMPARE(result->value->outcome, RpcCommandResult::Outcome::Uncertain);
    model.clear(); model.setCompletionPrefix("get"); env.drain(); env.publishAll();
    QCOMPARE(node.effects, 1); // Clear, observations and publication cannot replay an arbitrary RPC.
}

void RpcConsoleModelTests::acceptedRpcDeliversOnUtilityAfterExecutorCloses()
{
    UriCapturingNode node;
    ManualConsoleExecution env;
    RpcConsoleModel model{node, env.executor, [&](auto turn) { env.post(std::move(turn)); }};
    const auto settle = qScopeGuard([&] { env.drain(); env.publishAll(); });
    QVERIFY(model.submitWalletCommand("getwalletinfo", "original"));
    const auto receipt = model.commandReceipt();
    QVERIFY(receipt);
    env.executor.stopAndJoin();
    QVERIFY(env.step(execution::Lane::Rpc));
    QCOMPARE(node.lastUri(), QStringLiteral("/wallet/original"));
    QVERIFY(!receipt->result());
    QVERIFY(env.step(execution::Lane::Utility));
    const auto result = receipt->result();
    QVERIFY(result && result->value);
    QVERIFY(result->value->context == RpcCommandContext::ForWallet("original"));
    QCOMPARE(result->value->outcome, RpcCommandResult::Outcome::Succeeded);
}

void RpcConsoleModelTests::acceptedCancellationStaysKnownNotStarted()
{
    int effects{0}, notices{0};
    const auto receipt = std::make_shared<RpcCommandReceipt>([&] { ++notices; });
    std::shared_ptr<execution::Task> task;
    QVERIFY(receipt->startWith([&](auto done, auto failed) {
        task = execution::MakeTask([&] { ++effects; return RpcCommandResult{}; }, std::move(done), std::move(failed));
        return execution::SubmitResult{};
    }));
    task->notStarted(execution::NotStarted::Cause::Cancelled);
    task->deliver();
    QCOMPARE(effects, 0);
    QCOMPARE(notices, 1);
    const auto result = receipt->result(); QVERIFY(result); QVERIFY(result->error); QVERIFY(!result->value);
    bool known_not_started{false};
    try {
        std::rethrow_exception(result->error);
    } catch (const execution::NotStarted&) {
        known_not_started = true;
    } catch (...) {
        QFAIL("Cancelled RPC receipt retained an unexpected exception type");
    }
    QVERIFY(known_not_started);
}

void RpcConsoleModelTests::acceptedParseFailureKeepsOriginalWallet()
{
    struct Node final : RpcTestStubNode {
        int effects{0};
        UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override { ++effects; return {}; }
    } node;
    ManualConsoleExecution env;
    RpcConsoleModel model{node, env.executor, [&](auto turn) { env.post(std::move(turn)); }};
    QVERIFY(model.submitWalletCommand("getwalletinfo '", "original"));
    const auto receipt = model.commandReceipt();
    env.drain(); env.publishAll();
    const auto result = receipt->result();
    QVERIFY(result && result->value);
    QCOMPARE(result->value->outcome, RpcCommandResult::Outcome::NotStarted);
    QVERIFY(!result->value->attempted);
    QVERIFY(result->value->context == RpcCommandContext::ForWallet("original"));
    QCOMPARE(node.effects, 0);
    QVERIFY(!model.executing());
    auto* output = model.outputModel(); QCOMPARE(output->rowCount(), 1);
    QVERIFY(output->data(output->index(0), RpcOutputListModel::WalletContextRole).toBool());
    QCOMPARE(output->data(output->index(0), RpcOutputListModel::WalletNameRole).toString(), QStringLiteral("original"));
    model.clear(); env.drain(); env.publishAll(); QCOMPARE(node.effects, 0);
}

void RpcConsoleModelTests::unavailableWalletNeverUsesNodeEndpoint()
{
    struct Node final : RpcTestStubNode {
        std::vector<std::string> uris;
        UniValue executeRpc(const std::string&, const UniValue&, const std::string& uri) override {
            uris.push_back(uri);
            UniValue error{UniValue::VOBJ}; error.pushKV("code", -18); error.pushKV("message", "Requested wallet does not exist"); throw error;
        }
    } node;
    ManualConsoleExecution env;
    RpcConsoleModel model{node, env.executor, [&](auto turn) { env.post(std::move(turn)); }};
    const auto settle = qScopeGuard([&] { env.drain(); env.publishAll(); });
    QVERIFY(model.submitWalletCommand("getwalletinfo", "unloaded"));
    env.drain(); env.publishAll(); QVERIFY(!model.executing());
    QCOMPARE(node.uris, std::vector<std::string>{"/wallet/unloaded"});
    const auto result = model.commandReceipt()->result(); QVERIFY(result); QVERIFY(result->value);
    QCOMPARE(result->value->outcome, RpcCommandResult::Outcome::KnownError); QVERIFY(result->value->attempted);
}

void RpcConsoleModelTests::unavailableInputKeepsIndependentStop()
{
    struct Node final : RpcTestStubNode {
        int effects{0};
        std::vector<std::string> methods, uris;
        UniValue executeRpc(const std::string& method, const UniValue&, const std::string& uri) override {
            ++effects; methods.push_back(method); uris.push_back(uri); return {};
        }
    } node;
    ManualConsoleExecution env;
    RpcConsoleModel model{node, env.executor, [&](auto turn) { env.post(std::move(turn)); }};
    const auto settle = qScopeGuard([&] { env.drain(); env.publishAll(); });
    QVERIFY(!model.submitInput("getwalletinfo", true, "unavailable", false));
    QCOMPARE(node.effects, 0); QVERIFY(!model.executing());
    QVERIFY(model.submitNodeCommand("getblockcount"));
    QVERIFY(model.submitInput("stop", true, "unavailable", false));
    QVERIFY(env.step(execution::Lane::Priority));
    QCOMPARE(node.effects, 1); QCOMPARE(node.methods.front(), std::string{"stop"});
    QCOMPARE(node.uris.front(), std::string{}); QVERIFY(model.executing());
    env.drain(); env.publishAll();
    QCOMPARE(node.effects, 2); QVERIFY(!model.executing());
    model.beginShutdown(); env.drain();
    env.publishAll();
}

void RpcConsoleModelTests::publicOutputAppendCannotExceedBudgets()
{
    RpcOutputListModel out;
    out.appendRows({{"00:00:00", QString(RpcOutputListModel::kMaxCharacters + 1, 'x'), RpcConsoleModel::CMD_REPLY}}, 0, 1);
    QCOMPARE(out.rowCount(), 1);
    QVERIFY(out.data(out.index(0), RpcOutputListModel::ContentRole).toString().size() < 8192);
    RpcOutputListModel::Batch batch;
    for (int i = 0; i < RpcOutputListModel::kMaxRows + 1; ++i) batch.append({"00:00:00", QString(1024, 'y'), RpcConsoleModel::CMD_REPLY});
    out.appendRows(batch, 0, batch.size());
    QVERIFY(out.rowCount() > 1000); QVERIFY(out.rowCount() <= RpcOutputListModel::kMaxRows);
    qsizetype characters{0};
    for (int i = 0; i < out.rowCount(); ++i) characters += out.data(out.index(i), RpcOutputListModel::ContentRole).toString().size();
    QVERIFY(characters <= RpcOutputListModel::kMaxCharacters);
    const auto count = out.rowCount(); out.appendRows(batch, -1, 4); QCOMPARE(out.rowCount(), count);
}

void RpcConsoleModelTests::outputTruncatedWhenResultTooLong()
{
    LargeResultNode mock;
    RpcConsoleModel model{mock};

    auto* out = qobject_cast<QAbstractListModel*>(model.outputModel());
    QVERIFY(out != nullptr);
    QSignalSpy spy{out, &QAbstractItemModel::rowsInserted};

    QVERIFY(model.submitNodeCommand("getblockcount"));
    QTRY_VERIFY(!model.executing());
    QVERIFY(out->rowCount() > 2);
    for (int i = 0; i < out->rowCount(); ++i) {
        QTextDocument document;
        const auto html = out->data(out->index(i, 0), RpcOutputListModel::ContentRole).toString();
        document.setHtml(html);
        QVERIFY(document.toPlainText().size() <= RpcOutputListModel::kRowCharacters);
        QVERIFY(html.size() < 8192);
    }

    // Look up the content role dynamically — the model's roleNames map
    // "content" to the ContentRole integer.
    int content_role = roleForName(out, "content");
    QVERIFY(content_role != -1);

    const QString reply_html = replyHtml(out);

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

    model.submitNodeCommand("getsomething");
    QTRY_VERIFY(!model.executing());
    QVERIFY(out->rowCount() > 2);

    int content_role = roleForName(out, "content");
    QVERIFY(content_role != -1);

    const QString html = replyHtml(out);

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

void RpcConsoleModelTests::jsonReplyPreservesNestedFormatting_data()
{
    QTest::addColumn<QString>("json");
    QTest::addColumn<QString>("expected");
    QTest::newRow("empty-object") << QStringLiteral("{}") << QStringLiteral("{}");
    QTest::newRow("empty-array") << QStringLiteral("[]") << QStringLiteral("[]");
    const QString indent{QStringLiteral("&nbsp;&nbsp;")};
    const QString key{QStringLiteral("<span style='color:#123456'>\"")};
    QTest::newRow("mixed-containers") << QStringLiteral(R"({"a":[1,{"b":[]}],"z":{}})")
        << "{<br>" + indent + key + "a\"</span>: [<br>" + indent.repeated(2) + "1,<br>" +
           indent.repeated(2) + "{<br>" + indent.repeated(3) + key + "b\"</span>: []<br>" +
           indent.repeated(2) + "}<br>" + indent + "],<br>" + indent + key + "z\"</span>: {}<br>}";
    QTest::newRow("scalar-values") << QStringLiteral(R"([null,true,false,12,"<>&"])")
        << "[<br>" + indent + "null,<br>" + indent + "true,<br>" + indent + "false,<br>" +
           indent + "12,<br>" + indent + "\"&lt;&gt;&amp;\"<br>]";
    constexpr int depth{64};
    QString expected;
    for (int level{0}; level < depth; ++level) expected += "[<br>" + indent.repeated(level + 1);
    expected += "null";
    for (int level{depth - 1}; level >= 0; --level) expected += "<br>" + indent.repeated(level) + "]";
    QTest::newRow("deep-array") << QString(depth, '[') + "null" + QString(depth, ']') << expected;
}

void RpcConsoleModelTests::jsonReplyPreservesNestedFormatting()
{
    QFETCH(QString, json);
    QFETCH(QString, expected);
    UniValue reply;
    QVERIFY(reply.read(json.toStdString()));
    StructuredReplyNode mock{std::move(reply)};
    RpcConsoleModel model{mock};
    QVERIFY(model.setProperty("keyColor", QColor{"#123456"}));
    submitAndSettle(model, "getsomething");
    auto* out{model.outputModel()};
    QVERIFY(out->rowCount() >= 2);
    const int content_role{roleForName(out, "content")};
    QVERIFY(content_role != -1);
    QCOMPARE(replyHtml(out), expected);
}

void RpcConsoleModelTests::jsonReplyTraversal_data()
{
    QTest::addColumn<QString>("raw");
    QTest::addColumn<QString>("expected");
    QTest::newRow("empty-object") << QStringLiteral("{}") << QStringLiteral("{}");
    QTest::newRow("empty-array") << QStringLiteral("[]") << QStringLiteral("[]");
    QTest::newRow("mixed") << QStringLiteral("[{},[],true,false,null,12,\"<x>\"]")
        << QStringLiteral("[<br>&nbsp;&nbsp;{},<br>&nbsp;&nbsp;[],<br>&nbsp;&nbsp;true,<br>&nbsp;&nbsp;false,<br>&nbsp;&nbsp;null,<br>&nbsp;&nbsp;12,<br>&nbsp;&nbsp;\"&lt;x&gt;\"<br>]");
    QTest::newRow("nested-key") << QStringLiteral("{\"<key>\":[{\"n\":1}]}")
        << QStringLiteral("{<br>&nbsp;&nbsp;<span style='color:#98c379'>\"&lt;key&gt;\"</span>: [<br>&nbsp;&nbsp;&nbsp;&nbsp;{<br>&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;<span style='color:#98c379'>\"n\"</span>: 1<br>&nbsp;&nbsp;&nbsp;&nbsp;}<br>&nbsp;&nbsp;]<br>}");
    QTest::newRow("non-json") << QStringLiteral(" <help>\n  text") << QStringLiteral("&nbsp;&lt;help&gt;<br>&nbsp;&nbsp;text");
}

void RpcConsoleModelTests::jsonReplyTraversal()
{
    QFETCH(QString, raw);
    QFETCH(QString, expected);
    UniValue reply;
    if (!reply.read(raw.toStdString())) reply = UniValue(raw.toStdString());
    StructuredReplyNode node{std::move(reply)};
    RpcConsoleModel model{node};
    submitAndSettle(model, "getsomething");
    auto* output = qobject_cast<QAbstractListModel*>(model.outputModel());
    QVERIFY(output->rowCount() >= 2);
    QCOMPARE(replyHtml(output), expected);
}

void RpcConsoleModelTests::deeplyNestedJsonReply()
{
    constexpr int depth{256};
    const QString raw = QString(depth, '[') + "\"<leaf>\"" + QString(depth, ']');
    UniValue reply;
    if (!reply.read(raw.toStdString())) reply = UniValue(raw.toStdString());
    // Exercise the complete iterative formatter at depth256. Core pretty
    // serialization of an array at this depth exceeds the independent50k
    // console transport limit. A compact JSON string traverses the same reply
    // formatter without hitting that deliberately separate truncation policy.
    QVERIFY(reply.isArray());
    StructuredReplyNode node{UniValue{raw.toStdString()}};
    RpcConsoleModel model{node};
    submitAndSettle(model, "getsomething");
    auto* output = qobject_cast<QAbstractListModel*>(model.outputModel());
    const auto html = replyHtml(output);
    QCOMPARE(html.count('['), depth);
    QCOMPARE(html.count(']'), depth);
    QCOMPARE(html.count("<br>"), depth * 2);
    QCOMPARE(html.count("&lt;leaf&gt;"), 1);
    QVERIFY(!html.contains(','));
    QVERIFY(html.startsWith("[<br>"));
    QVERIFY(html.endsWith("<br>]"));
}

void RpcConsoleModelTests::availableCommandsIncludesHelpVariants()
{
    CommandListNode mock;
    RpcConsoleModel model{mock};

    // Populate availableCommands via the public slot.
    model.onNodeInitialized();

    QTRY_VERIFY(!model.availableCommands().isEmpty());
    QVERIFY(!mock.listOnGui());
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

void RpcConsoleModelTests::secretRequestsAreNotRetained_data()
{
    QTest::addColumn<QString>("method"); QTest::addColumn<QString>("arguments"); QTest::addColumn<QString>("expected_parameters");
    QTest::newRow("createwallet-passphrase") << "createwallet" << "redaction-wallet false false unique-rpc-secret-92"
        << R"(["redaction-wallet",false,false,"unique-rpc-secret-92"])";
    QTest::newRow("migratewallet-passphrase") << "migratewallet" << "redaction-wallet unique-rpc-secret-92"
        << R"(["redaction-wallet","unique-rpc-secret-92"])";
    QTest::newRow("addhdkey-private-key") << "addhdkey" << "unique-rpc-secret-92"
        << R"(["unique-rpc-secret-92"])";
    QTest::newRow("walletpassphrase-control") << "walletpassphrase" << "unique-rpc-secret-92 10"
        << R"(["unique-rpc-secret-92",10])";
}

void RpcConsoleModelTests::secretRequestsAreNotRetained()
{
    QFETCH(QString, method); QFETCH(QString, arguments); QFETCH(QString, expected_parameters);
    class RecordingNode final : public RpcTestStubNode {
        QMutex mutex;
        std::string called_method, called_arguments;
    public:
        UniValue executeRpc(const std::string& method, const UniValue& parameters, const std::string&) override
        {
            const QMutexLocker lock{&mutex}; called_method = method; called_arguments = parameters.write();
            return UniValue("safe reply");
        }
        std::pair<std::string, std::string> received() { const QMutexLocker lock{&mutex}; return {called_method, called_arguments}; }
    } node;
    RpcConsoleModel model{node};
    QVERIFY(model.submitNodeCommand(method + " " + arguments));
    QTRY_VERIFY(!model.executing());
    const auto received = node.received();
    QCOMPARE(received.first, method.toStdString());
    QCOMPARE(received.second, expected_parameters.toStdString());
    QVERIFY(QString::fromStdString(received.second).contains("unique-rpc-secret-92"));
    const int content = roleForName(model.outputModel(), "content"); QVERIFY(content >= 0);
    bool requests_safe{true};
    for (int i = 0; i < model.outputModel()->rowCount(); ++i) {
        const auto text = model.outputModel()->data(model.outputModel()->index(i, 0), content).toString();
        requests_safe = requests_safe && !text.contains("unique-rpc-secret-92");
    }
    model.clear(); QCOMPARE(model.outputModel()->rowCount(), 0);
    const QString pending = "ordinary unsent draft";
    const QString recalled = model.browseHistory(1, pending);
    const bool history_safe = !recalled.contains("unique-rpc-secret-92");
    qInfo() << "RPC privacy oracle" << method << "request safe" << requests_safe << "recall after Clear safe" << history_safe;
    QCOMPARE(model.browseHistory(-1, pending), pending);
    QVERIFY(requests_safe); QVERIFY(history_safe);
    QCOMPARE(recalled, method + QString::fromUtf8("(…)"));
}

void RpcConsoleModelTests::argumentConversionDiagnosticsFollowRequestRedaction_data()
{
    QTest::addColumn<QString>("command"); QTest::addColumn<bool>("sensitive");
    QTest::newRow("sensitive-direct") << "signrawtransactionwithkey deadbeef unique-rpc-secret-92" << true;
    QTest::newRow("sensitive-nested") << "echo(signrawtransactionwithkey(deadbeef unique-rpc-secret-92))" << true;
    QTest::newRow("ordinary-conversion-control") << "getblockhash ordinary-parse-control" << false;
}

void RpcConsoleModelTests::argumentConversionDiagnosticsFollowRequestRedaction()
{
    QFETCH(QString, command); QFETCH(bool, sensitive);
    struct Node final : RpcTestStubNode {
        int effects{0};
        UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override { ++effects; return {}; }
    } node;
    ManualConsoleExecution env;
    RpcConsoleModel model{node, env.executor, [&](auto turn) { env.post(std::move(turn)); }};
    const auto cleanup = qScopeGuard([&] { model.beginShutdown(); env.drain(); env.publishAll(); });
    QVERIFY(model.submitNodeCommand(command)); env.drain(); env.publishAll();
    QCOMPARE(node.effects, 0); QVERIFY(!model.executing());
    bool request{false}, error{false};
    auto* output = model.outputModel(); QVERIFY(output->rowCount() >= 2);
    for (int row = 0; row < output->rowCount(); ++row) {
        const auto index = output->index(row, 0);
        const auto text = output->data(index, RpcOutputListModel::ContentRole).toString();
        QVERIFY(!text.contains("unique-rpc-secret-92"));
        const int category = output->data(index, RpcOutputListModel::CategoryRole).toInt();
        if (category == RpcConsoleModel::CMD_REQUEST) request |= text.contains(sensitive ? QString::fromUtf8("(…)") : QStringLiteral("ordinary-parse-control"));
        if (category == RpcConsoleModel::CMD_ERROR) error |= text.contains(sensitive ? QStringLiteral("Invalid RPC arguments.") : QStringLiteral("Error parsing JSON: ordinary-parse-control"));
    }
    QVERIFY(request); QVERIFY(error);
    model.clear(); QCOMPARE(output->rowCount(), 0);
    const auto recalled = model.browseHistory(1, {});
    QVERIFY(!recalled.contains("unique-rpc-secret-92"));
    QVERIFY(recalled.contains(sensitive ? QString::fromUtf8("(…)") : QStringLiteral("ordinary-parse-control")));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(RpcConsoleModelTests)
#else
QTEST_MAIN(RpcConsoleModelTests)
#endif
#include <test_rpcconsolemodel.moc>
