// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/mempoolmodel.h>
#include <qml/test/qt_test_registry.h>
#include <QSignalSpy>
#include <QTest>
#include <atomic>
#include <condition_variable>
#include <mutex>

namespace {
struct FetchGate {
    std::mutex mutex;
    std::condition_variable completed;
    std::atomic_int calls{0};
    bool released{false};
    MempoolModel::Snapshot fetch()
    {
        const int count{++calls};
        std::unique_lock lock{mutex};
        completed.wait(lock, [&] { return released; });
        return {count, 2.0, 300.0};
    }
    void release()
    {
        {
            std::lock_guard lock{mutex};
            released = true;
        }
        completed.notify_all();
    }
};
struct ReleaseGate {
    FetchGate& gate;
    ~ReleaseGate() { gate.release(); }
};
}

class MempoolModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void waitsForReadinessAndVisiblePage()
    {
        std::atomic_int calls{0};
        MempoolModel model{[&] { ++calls; return MempoolModel::Snapshot{7, 1.5, 300}; }};
        model.refreshMempoolInfo();
        model.setMempoolInfoPollingActive(true);
        QCOMPARE(calls.load(), 0);
        model.initializeResult(true, {});
        QTRY_COMPARE(model.mempoolTransactionCount(), 7);
        QCOMPARE(model.mempoolUsageMB(), 1.5);
        model.stop();
        const int stopped_calls{calls.load()};
        model.setMempoolInfoPollingActive(true);
        model.refreshMempoolInfo();
        QVERIFY(!model.mempoolInfoPollingActive());
        QCOMPARE(calls.load(), stopped_calls);
    }

    void blocksonlyNeverSchedulesMempoolWork()
    {
        std::atomic_int calls{0};
        MempoolModel model{[&] { ++calls; return MempoolModel::Snapshot{}; }, false};
        model.initializeResult(true, {});
        model.setMempoolInfoPollingActive(true);
        model.refreshMempoolInfo();
        QVERIFY(!model.mempoolInformationAvailable());
        QVERIFY(!model.mempoolInfoPollingActive());
        QCOMPARE(calls.load(), 0);
    }

    void hidingDiscardsAnInflightResult()
    {
        FetchGate gate;
        MempoolModel model{[&] { return gate.fetch(); }};
        ReleaseGate cleanup{gate};
        QSignalSpy changed{&model, &MempoolModel::mempoolInfoChanged};
        model.initializeResult(true, {});
        model.setMempoolInfoPollingActive(true);
        QTRY_COMPARE(gate.calls.load(), 1);
        model.setMempoolInfoPollingActive(false);
        gate.release();
        model.stop(); // Joins the read; the queued completion remains stale.
        QCoreApplication::processEvents();
        QCOMPARE(changed.count(), 0);
        QCOMPARE(model.mempoolTransactionCount(), 0);
    }

    void coalescesRefreshesAndResamplesAfterReactivation()
    {
        FetchGate gate;
        MempoolModel model{[&] { return gate.fetch(); }};
        ReleaseGate cleanup{gate};
        model.initializeResult(true, {});
        model.setMempoolInfoPollingActive(true);
        QTRY_COMPARE(gate.calls.load(), 1);
        for (int i = 0; i < 20; ++i) model.refreshMempoolInfo();
        QCOMPARE(gate.calls.load(), 1);
        model.setMempoolInfoPollingActive(false);
        model.setMempoolInfoPollingActive(true);
        gate.release();
        QTRY_COMPARE(model.mempoolTransactionCount(), 2);
        QCOMPARE(gate.calls.load(), 2);
    }
};
BITCOINQML_REGISTER_QT_TEST(MempoolModelTests)
#include <test_mempoolmodel.moc>
