// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/asyncjoin.h>
#include <qml/indexedsequence.h>
#include <qml/models/activitysnapshot.h>
#include <qml/nativelistmodel.h>
#include <qml/retainedvalue.h>
#include <test/mocks/manualstartupexecution.h>
#include <test/mocks/nativelistqueues.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <QScopeGuard>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace {
class ManualList final : public NativeListModel {
public:
    ManualList(presentation::Post post, Queues queues, std::vector<NativeRowHandle>& rows, int& reads)
        : NativeListModel(std::move(post), nullptr, std::move(queues)) { setRead([&rows, &reads] { ++reads; return rows; }); }
    void refresh() { refreshNative(); }
    std::function<void()> updated;
protected:
    void rowUpdated(const QString&, const NativeRowHandle&) override { if (updated) updated(); }
};
struct NativeFixture {
    ManualStartupExecution env;
    NativeListModel::Queues queues{NativeListQueues(env.executor())};
    std::deque<std::function<void()>> gui;
    std::vector<NativeRowHandle> rows;
    int reads{0};
    ManualList model{[this](auto turn) { gui.push_back(std::move(turn)); }, queues, rows, reads};
    NativeFixture() { rows.reserve(65); }
    NativeRowHandle row(int id, int value) {
        return execution::RetainValues<NativeListRow>(queues.hold, [=] {
            auto result = std::make_unique<NativeListRow>();
            result->key = QString::number(1000 + id); result->values.insert(Qt::DisplayRole, value); return result;
        });
    }
    bool publishOne() {
        if (gui.empty()) return false;
        auto turn = std::move(gui.front()); gui.pop_front(); turn(); return true;
    }
    void settle() {
        for (int turn = 0; turn < 1024; ++turn) {
            env.drain();
            if (!publishOne()) return;
        }
        QTest::qFail("Native list did not settle within its turn budget", __FILE__, __LINE__);
    }
    ~NativeFixture() { model.close(); rows.clear(); settle(); }
};
}
class PersistentTreeTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void nativeStandaloneJoinWaitsForRetainedVersion()
    {
        struct Cleanup {
            std::atomic_bool retired{false}, off_gui{false};
        };
        const auto cleanup = std::make_shared<Cleanup>();
        auto row = std::make_shared<NativeListRow>();
        row->key = QStringLiteral("original");
        row->values.insert(Qt::DisplayRole, 42);
        row->payload = std::shared_ptr<int>{new int{0}, [cleanup, gui = QThread::currentThread()](int* value) {
            delete value;
            cleanup->off_gui = QThread::currentThread() != gui;
            cleanup->retired = true;
        }};
        std::vector<NativeRowHandle> rows{std::move(row)};
        int reads{0};
        auto model = std::make_unique<ManualList>(presentation::Post{}, NativeListModel::Queues{}, rows, reads);
        model->refresh();
        QTRY_COMPARE(model->rowCount(), 1);
        auto held = model->completedSnapshot();
        QVERIFY(held);
        QCOMPARE(reads, 1);
        rows.clear();
        model.reset();

        bool joined{false};
        QObject receiver;
        WaitForThreadJoins(&receiver, [&] { joined = true; });
        QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
        QVERIFY(!joined);
        QVERIFY(!cleanup->retired);
        {
            const auto retained = NativeListModel::Rows(held);
            QCOMPARE(retained.size(), size_t{1});
            QCOMPARE(retained.front()->values.value(Qt::DisplayRole).toInt(), 42);
        }
        held.reset();
        QTRY_VERIFY(joined);
        QVERIFY(cleanup->retired);
        QVERIFY(cleanup->off_gui);
    }

    void malformedPublicActivitySnapshotRejectsMissingRank() {
        using namespace activityview;
        const SharedRow row = std::make_shared<const Row>(Row{{Qt::DisplayRole, 42}});
        // Public snapshot metadata can be stale even when the retained row is
        // valid. Missing ranks must not change or replace the original node.
        const Tree root = std::make_shared<const Node>(Node{QStringLiteral("original"), row, {}, {}, 1, 2});
        const auto* existing = At(root, 0);
        QCOMPARE(existing, root.get()); QCOMPARE(existing->id, QStringLiteral("original"));
        QCOMPARE(existing->row.get(), row.get()); QCOMPARE(existing->row->value(Qt::DisplayRole).toInt(), 42);
        QVERIFY(At(root, 1) == nullptr);
        QCOMPARE(At(root, 0), existing); QCOMPARE(root->row.get(), row.get());
    }
    void indexedSequenceKeepsRanksAndIdentityThroughRotationsAndSuccessorRemoval() {
        IndexedSequence<int> rows;
        std::vector<int> expected;
        expected.reserve(257);
        uint32_t state{0x13579bdf};
        const auto check = [&] {
            QCOMPARE(rows.size(), int(expected.size()));
            for (int row = 0; row < rows.size(); ++row) {
                QCOMPARE(rows[row], expected[row]); QCOMPARE(rows.indexOf(expected[row]), row);
            }
        };
        for (int value = 0; value < 257; ++value) {
            state = static_cast<uint32_t>((uint64_t{state} * 1664525U + 1013904223U) & 0xffffffffU);
            const int rank = int(state % (expected.size() + 1));
            rows.insertAt(rank, value); expected.insert(expected.begin() + rank, value); check();
        }
        while (!expected.empty()) {
            state = static_cast<uint32_t>((uint64_t{state} * 1664525U + 1013904223U) & 0xffffffffU);
            const int rank = int(state % expected.size()); const int removed = expected[rank];
            QCOMPARE(rows.takeAt(rank), removed); expected.erase(expected.begin() + rank);
            QCOMPARE(rows.indexOf(removed), -1); check();
        }
        QVERIFY(rows.empty());
    }
    void activityRootsPreserveOldRowsAndBalancedRanksAcrossReplacementAndErase() {
        using namespace activityview;
        Tree root;
        std::map<QString, int> expected;
        struct Held { Tree tree; std::map<QString, int> expected; };
        std::vector<Held> held;
        held.reserve(6);
        const auto check = [](const Tree& tree, const std::map<QString, int>& oracle) {
            QCOMPARE(Size(tree), int(oracle.size())); int rank{0};
            for (const auto& [id, value] : oracle) {
                const auto* node = At(tree, rank); QVERIFY(node); QCOMPARE(node->id, id);
                QCOMPARE(node->row->value(Qt::DisplayRole).toInt(), value); QCOMPARE(Rank(tree, id), rank++);
            }
            QVERIFY(!At(tree, -1)); QVERIFY(!At(tree, int(oracle.size())));
            std::vector<const Node*> pending;
            pending.reserve(Height(tree));
            if (tree) pending.push_back(tree.get());
            while (!pending.empty()) {
                const auto* node = pending.back(); pending.pop_back();
                QCOMPARE(node->size, 1 + Size(node->left) + Size(node->right));
                QCOMPARE(node->height, 1 + std::max(Height(node->left), Height(node->right)));
                QVERIFY(std::abs(Height(node->left) - Height(node->right)) <= 1);
                if (node->left) pending.push_back(node->left.get());
                if (node->right) pending.push_back(node->right.get());
            }
        };
        for (int index = 0; index < 129; ++index) {
            const int value = (index * 73) % 129; const auto id = QString::number(1000 + value);
            auto row = std::make_shared<Row>(); row->insert(Qt::DisplayRole, value);
            root = Insert(root, id, row); expected[id] = value; check(root, expected);
            if (index % 32 == 0) held.push_back({root, expected});
        }
        for (int value = 0; value < 129; value += 3) {
            const auto id = QString::number(1000 + value); auto row = std::make_shared<Row>();
            row->insert(Qt::DisplayRole, value + 1000); root = Insert(root, id, row); expected[id] = value + 1000;
        }
        check(root, expected); held.push_back({root, expected});
        root = Erase(root, QStringLiteral("not-present")); check(root, expected);
        for (int index = 0; index < 129; ++index) {
            const auto id = QString::number(1000 + (index * 73) % 129);
            root = Erase(root, id); expected.erase(id); check(root, expected);
        }
        QVERIFY(!root);
        for (const auto& version : held) check(version.tree, version.expected);
    }
    void nativeReentrantPlanYieldsBeforePublishingItsSuccessor_data() {
        QTest::addColumn<bool>("qt_notice");
        QTest::newRow("inside-Qt-row-notice") << true;
        QTest::newRow("inside-presentation-hook") << false;
    }
    void nativeReentrantPlanYieldsBeforePublishingItsSuccessor() {
        QFETCH(bool, qt_notice);
        NativeFixture fixture;
        for (int id = 0; id < 65; ++id) fixture.rows.push_back(fixture.row(id, id));
        QSignalSpy inserted{&fixture.model, &QAbstractItemModel::rowsInserted};
        QSignalSpy removed{&fixture.model, &QAbstractItemModel::rowsRemoved};
        QSignalSpy moved{&fixture.model, &QAbstractItemModel::rowsMoved};
        QSignalSpy ready{&fixture.model, &NativeListModel::snapshotReady};
        bool replaced{false};
        const auto replace = [&] {
            if (std::exchange(replaced, true)) return;
            std::reverse(fixture.rows.begin(), fixture.rows.end()); fixture.model.refresh(); fixture.env.drain();
            while (fixture.publishOne()) {} // Deliver the newer plan inside the original presentation phase.
        };
        if (qt_notice) connect(&fixture.model, &QAbstractItemModel::rowsInserted, this, replace);
        else fixture.model.updated = replace;
        fixture.model.refresh(); fixture.env.drain(); QVERIFY(fixture.publishOne());
        QVERIFY(replaced); QCOMPARE(inserted.size(), 1); QVERIFY(ready.empty());
        for (int turn = 0; turn < 128 && fixture.model.projectionPending(); ++turn) {
            fixture.env.drain(); const auto before = inserted.size() + removed.size() + moved.size();
            if (!fixture.publishOne()) break;
            QVERIFY(inserted.size() + removed.size() + moved.size() - before <= 32);
        }
        QCOMPARE(ready.size(), 1); QCOMPARE(fixture.model.rowCount(), 65);
        for (int row = 0; row < 65; ++row) QCOMPARE(fixture.model.data(fixture.model.index(row), Qt::DisplayRole).toInt(), 64 - row);
        const auto held = fixture.model.completedSnapshot();
        const auto original = NativeListModel::Rows(held); QCOMPARE(original.size(), size_t{65});
        const QPersistentModelIndex persistent{fixture.model.index(20)};
        const auto identity = fixture.model.rowHandle(persistent)->key;
        std::rotate(fixture.rows.begin(), fixture.rows.begin() + 17, fixture.rows.end());
        const int replaced_id = fixture.rows[20]->key.toInt() - 1000;
        fixture.rows[20] = fixture.row(replaced_id, 777);
        fixture.model.refresh(); fixture.settle();
        QVERIFY(persistent.isValid()); QCOMPARE(fixture.model.rowHandle(persistent)->key, identity);
        QCOMPARE(persistent.row(), fixture.model.indexForKey(identity));
        for (int row = 0; row < 65; ++row) {
            QCOMPARE(fixture.model.rowHandle(fixture.model.index(row))->key, fixture.rows[row]->key);
            QCOMPARE(fixture.model.data(fixture.model.index(row), Qt::DisplayRole), fixture.rows[row]->values.value(Qt::DisplayRole));
        }
        fixture.rows.clear(); fixture.model.refresh(); fixture.settle(); QCOMPARE(fixture.model.rowCount(), 0);
        QVERIFY(!persistent.isValid());
        const auto retained = NativeListModel::Rows(held); QCOMPARE(retained.size(), size_t{65});
        for (int row = 0; row < 65; ++row) QCOMPARE(retained[row]->values.value(Qt::DisplayRole).toInt(), 64 - row);
    }
    void nativeQuietCloseBalancesTheOriginalQtInsertBeforeRetiringRows() {
        NativeFixture fixture;
        for (int id = 0; id < 65; ++id) fixture.rows.push_back(fixture.row(id, id));
        int observed_after_insert{-1};
        connect(&fixture.model, &QAbstractItemModel::rowsAboutToBeInserted, this, [&] { fixture.model.close(); });
        connect(&fixture.model, &QAbstractItemModel::rowsInserted, this, [&] { observed_after_insert = fixture.model.rowCount(); });
        fixture.model.refresh(); fixture.env.drain(); QVERIFY(fixture.publishOne());
        QCOMPARE(observed_after_insert, 1); QCOMPARE(fixture.model.rowCount(), 0);
        fixture.rows.clear(); fixture.settle(); QVERIFY(fixture.env.quiescent());
    }
    void nativeSnapshotReadyCanRequestANewerVersionWithoutStrandingIt() {
        NativeFixture fixture;
        fixture.rows.push_back(fixture.row(0, 7));
        NativeVersionHandle original;
        int ready{0};
        connect(&fixture.model, &NativeListModel::snapshotReady, this, [&] {
            if (++ready != 1) return;
            original = fixture.model.completedSnapshot();
            fixture.rows.push_back(fixture.row(1, 8)); fixture.model.refresh(); fixture.env.drain();
            while (fixture.publishOne()) {}
        });
        fixture.model.refresh(); fixture.settle();
        QCOMPARE(ready, 2); QVERIFY(!fixture.model.projectionPending()); QCOMPARE(fixture.model.rowCount(), 2);
        const auto retained = NativeListModel::Rows(original); QCOMPARE(retained.size(), size_t{1});
        QCOMPARE(retained.front()->values.value(Qt::DisplayRole).toInt(), 7);
    }
};
BITCOINQML_REGISTER_QT_TEST(PersistentTreeTests)
#include <test_persistenttrees.moc>
