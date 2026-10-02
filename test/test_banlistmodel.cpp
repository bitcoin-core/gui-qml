// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <QSet>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>
#include <QTimer>

#include <test/mocks/mocknode.h>
#include <qml/models/banlistmodel.h>
#include <util/translation.h>

#include <netbase.h>

#include <atomic>
#include <map>
#include <stdexcept>
#include <utility>

namespace {
class DrainingBanListModel : public BanListModel
{
public:
    using BanListModel::BanListModel;
    ~DrainingBanListModel() override
    {
        beginShutdown();
        if (!QTest::qWaitFor([this] { return isDrained(); }, 10'000)) qFatal("Ban fixture did not drain");
    }
};
CSubNet ParseSubnet(const std::string& subnet)
{
    CSubNet parsed = LookupSubNet(subnet);
    if (!parsed.IsValid()) {
        throw std::runtime_error("failed to parse subnet test fixture");
    }
    return parsed;
}

CBanEntry MakeBanEntry(int64_t ban_until)
{
    CBanEntry entry;
    entry.nBanUntil = ban_until;
    return entry;
}
} // namespace

class BanListModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void gatedRefreshIsBoundedAndKeepsGuiResponsive();
    void unbanCapturesSubnetAcrossRefreshAndCoalescesNotifications();
    void unbanAcknowledgesNotificationsBeforeFinalRead();
    void refreshFailureKeepsCachedEntries();
    void shutdownWaitsForCommandAndSuppressesResults();
    void externalMutationDefersNotificationsUntilCompletion();
    void disconnectCompletionFlushesDeferredBanNotification();
    void refreshPopulatesRolesAndRows();
    void unbanAtTargetsSelectedSubnet();
    void unbanAtReportsBackendRejection();
    void unbanAtIgnoresInvalidRows();
};

void BanListModelTests::refreshPopulatesRolesAndRows()
{
    banmap_t banned;
    const CSubNet subnet_a = ParseSubnet("10.0.0.0/8");
    const CSubNet subnet_b = ParseSubnet("127.0.0.1/32");
    banned.emplace(subnet_a, MakeBanEntry(1'900'000'000));
    banned.emplace(subnet_b, MakeBanEntry(2'000'000'000));

    MockNode node;
    node.get_banned_fn = [&](banmap_t& result) {
        result = banned;
        return true;
    };

    DrainingBanListModel model{node, nullptr};
    QSignalSpy count_spy(&model, &BanListModel::countChanged);

    model.refresh();
    QTRY_VERIFY(!model.refreshPending());

    QCOMPARE(node.calls.getBanned.load(), 1);
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);
    QCOMPARE(count_spy.count(), 1);

    const auto roles = model.roleNames();
    QCOMPARE(roles.value(static_cast<int>(BanListModel::BanRoles::AddressRole)), QByteArray{"address"});
    QCOMPARE(roles.value(static_cast<int>(BanListModel::BanRoles::BanUntilRole)), QByteArray{"banUntil"});

    QSet<QString> addresses;
    for (int row = 0; row < model.rowCount(); ++row) {
        const QModelIndex index = model.index(row, 0);
        QVERIFY(index.isValid());

        const QString address = model.data(index, static_cast<int>(BanListModel::BanRoles::AddressRole)).toString();
        QVERIFY(!address.isEmpty());
        addresses.insert(address);

        const QString ban_until = model.data(index, static_cast<int>(BanListModel::BanRoles::BanUntilRole)).toString();
        QVERIFY(!ban_until.isEmpty());
    }

    QVERIFY(addresses.contains(QString::fromStdString(subnet_a.ToString())));
    QVERIFY(addresses.contains(QString::fromStdString(subnet_b.ToString())));
}

void BanListModelTests::unbanAtTargetsSelectedSubnet()
{
    banmap_t banned;
    const CSubNet subnet = ParseSubnet("10.0.0.0/8");
    banned.emplace(subnet, MakeBanEntry(1'900'000'000));

    MockNode node;
    node.get_banned_fn = [&](banmap_t& result) {
        result = banned;
        return true;
    };

    DrainingBanListModel model{node, nullptr};
    model.refresh();
    QTRY_VERIFY(!model.refreshPending());

    bool received_expected_subnet{false};
    node.unban_fn = [&](const CSubNet& value) {
        received_expected_subnet = value.ToString() == subnet.ToString();
        return true;
    };
    QSignalSpy finished{&model, &BanListModel::unbanFinished};
    QVERIFY(model.unbanAt(0));
    QTRY_COMPARE(finished.count(), 1);
    QTRY_VERIFY(!model.refreshPending());
    QVERIFY(finished.at(0).at(0).toBool());
    QCOMPARE(node.calls.getBanned.load(), 2);
    QCOMPARE(node.calls.unban.load(), 1);
    QVERIFY(received_expected_subnet);
}

void BanListModelTests::unbanAtReportsBackendRejection()
{
    banmap_t banned;
    banned.emplace(ParseSubnet("10.0.0.0/8"), MakeBanEntry(1'900'000'000));

    MockNode node;
    node.get_banned_fn = [&](banmap_t& result) {
        result = banned;
        return true;
    };

    DrainingBanListModel model{node, nullptr};
    model.refresh();
    QTRY_VERIFY(!model.refreshPending());

    node.unban_fn = [](const CSubNet&) { return false; };
    QSignalSpy finished{&model, &BanListModel::unbanFinished};
    QVERIFY(model.unbanAt(0));
    QTRY_COMPARE(finished.count(), 1);
    QTRY_VERIFY(!model.refreshPending());
    QVERIFY(!finished.at(0).at(0).toBool());
    QVERIFY(!model.actionError().isEmpty());
    QCOMPARE(node.calls.getBanned.load(), 2);
    QCOMPARE(node.calls.unban.load(), 1);
}

void BanListModelTests::unbanAtIgnoresInvalidRows()
{
    banmap_t banned;
    banned.emplace(ParseSubnet("10.0.0.0/8"), MakeBanEntry(1'900'000'000));

    MockNode node;
    node.get_banned_fn = [&](banmap_t& result) {
        result = banned;
        return true;
    };

    DrainingBanListModel model{node, nullptr};
    model.refresh();
    QTRY_VERIFY(!model.refreshPending());

    QVERIFY(!model.unbanAt(-1));
    QVERIFY(!model.unbanAt(42));
    QCOMPARE(node.calls.getBanned.load(), 1);
    QCOMPARE(node.calls.unban.load(), 0);
}


void BanListModelTests::gatedRefreshIsBoundedAndKeepsGuiResponsive()
{
    MockNode node;
    QSemaphore entered, release;
    std::atomic<bool> worker_thread{false};
    const auto* gui_thread = QThread::currentThread();
    node.get_banned_fn = [&](banmap_t& result) {
        worker_thread = QThread::currentThread() != gui_thread;
        entered.release(); release.acquire();
        result.emplace(ParseSubnet("10.0.0.0/8"), MakeBanEntry(1'900'000'000));
        return true;
    };
    DrainingBanListModel model{node, nullptr, false};
    const auto unblock = qScopeGuard([&] { release.release(3); });
    model.refresh();
    QTest::qWait(20);
    QCOMPARE(node.calls.getBanned.load(), 0);
    model.backendInitialized();
    QTRY_VERIFY(entered.available() > 0);
    entered.acquire();
    QVERIFY(worker_thread.load());
    for (int i = 0; i < 100; ++i) model.refresh();
    bool gui_progress{false};
    QTimer::singleShot(0, this, [&] { gui_progress = true; });
    QTRY_VERIFY(gui_progress);
    QCOMPARE(model.count(), 0);
    QCOMPARE(node.calls.getBanned.load(), 1);
    bool model_on_gui{false};
    connect(&model, &BanListModel::modelReset, &model, [&] { model_on_gui = QThread::currentThread() == gui_thread; });
    release.release();
    QTRY_VERIFY(entered.available() > 0);
    entered.acquire();
    QVERIFY(model_on_gui);
    QCOMPARE(node.calls.getBanned.load(), 2);
    release.release();
    QTRY_VERIFY(!model.refreshPending());
    QTest::qWait(20);
    QCOMPARE(node.calls.getBanned.load(), 2);
    QCOMPARE(model.count(), 1);
}

void BanListModelTests::unbanCapturesSubnetAcrossRefreshAndCoalescesNotifications()
{
    MockNode node;
    const auto original = ParseSubnet("10.0.0.0/8");
    const auto replacement = ParseSubnet("127.0.0.1/32");
    QSemaphore read_entered, read_release, action_entered, action_release;
    std::atomic<bool> correct_subnet{false};
    std::atomic<bool> worker_thread{false};
    const auto* gui_thread = QThread::currentThread();
    node.get_banned_fn = [&](banmap_t& result) {
        if (node.calls.getBanned.load() == 2) { read_entered.release(); read_release.acquire(); }
        result.emplace(node.calls.getBanned.load() == 1 ? original : replacement, MakeBanEntry(1'900'000'000));
        return true;
    };
    node.unban_fn = [&](const CSubNet& target) {
        correct_subnet = target.ToString() == original.ToString();
        worker_thread = QThread::currentThread() != gui_thread;
        action_entered.release(); action_release.acquire();
        return true;
    };
    DrainingBanListModel model{node};
    const auto unblock = qScopeGuard([&] { read_release.release(); action_release.release(); });
    model.refresh();
    QTRY_VERIFY(model.ready());
    model.refresh();
    QTRY_VERIFY(read_entered.available() > 0);
    QSignalSpy completed{&model, &BanListModel::unbanFinished};
    QVERIFY(model.unbanAt(0));
    QVERIFY(!model.unbanAt(0));
    read_release.release();
    QTRY_VERIFY(action_entered.available() > 0);
    QTRY_COMPARE(model.data(model.index(0, 0), static_cast<int>(BanListModel::BanRoles::AddressRole)).toString(), QString::fromStdString(replacement.ToString()));
    QVERIFY(correct_subnet.load());
    QVERIFY(worker_thread.load());
    for (int i = 0; i < 100; ++i) model.refresh();
    bool gui_progress{false};
    QTimer::singleShot(0, this, [&] { gui_progress = true; });
    QTRY_VERIFY(gui_progress);
    QCOMPARE(node.calls.getBanned.load(), 2);
    QCOMPARE(completed.count(), 0);
    action_release.release();
    QTRY_COMPARE(completed.count(), 1);
    QTRY_VERIFY(!model.refreshPending());
    QCOMPARE(node.calls.getBanned.load(), 3);
    QCOMPARE(node.calls.unban.load(), 1);
}

void BanListModelTests::unbanAcknowledgesNotificationsBeforeFinalRead()
{
    MockNode node;
    std::atomic<bool> newer_ban{false};
    const auto original = ParseSubnet("10.0.0.0/8");
    const auto replacement = ParseSubnet("127.0.0.1/32");
    node.get_banned_fn = [&](banmap_t& result) {
        result.emplace(newer_ban.load() ? replacement : original, MakeBanEntry(1'900'000'000));
        return true;
    };
    node.unban_fn = [](const CSubNet&) { return true; };
    DrainingBanListModel model{node};
    model.refresh();
    QTRY_VERIFY(model.ready());
    bool acknowledged_before_read{false};
    connect(&model, &BanListModel::unbanFinished, &model, [&](bool, const QString&) {
        // This observer stands in for notification acknowledgement followed by
        // an external ban. The final read must not have been queued yet.
        acknowledged_before_read = !model.refreshPending();
        newer_ban = true;
    });
    QSignalSpy completed{&model, &BanListModel::unbanFinished};
    QVERIFY(model.unbanAt(0));
    QTRY_COMPARE(completed.count(), 1);
    QTRY_VERIFY(!model.refreshPending());
    QVERIFY(acknowledged_before_read);
    QCOMPARE(node.calls.getBanned.load(), 2);
    QCOMPARE(model.data(model.index(0, 0), static_cast<int>(BanListModel::BanRoles::AddressRole)).toString(), QString::fromStdString(replacement.ToString()));
}

void BanListModelTests::refreshFailureKeepsCachedEntries()
{
    MockNode node;
    node.get_banned_fn = [&](banmap_t& result) {
        if (node.calls.getBanned.load() > 1) return false;
        result.emplace(ParseSubnet("10.0.0.0/8"), MakeBanEntry(1'900'000'000));
        return true;
    };
    DrainingBanListModel model{node};
    model.refresh();
    QTRY_VERIFY(model.ready());
    model.refresh();
    QTRY_VERIFY(!model.refreshPending());
    QCOMPARE(model.count(), 1);
    QVERIFY(!model.refreshError().isEmpty());
}

void BanListModelTests::shutdownWaitsForCommandAndSuppressesResults()
{
    MockNode node;
    QSemaphore entered, release;
    node.get_banned_fn = [](banmap_t& result) {
        result.emplace(ParseSubnet("10.0.0.0/8"), MakeBanEntry(1'900'000'000));
        return true;
    };
    node.unban_fn = [&](const CSubNet&) { entered.release(); release.acquire(); return true; };
    DrainingBanListModel model{node};
    const auto unblock = qScopeGuard([&] { release.release(); });
    model.refresh();
    QTRY_VERIFY(model.ready());
    QSignalSpy completed{&model, &BanListModel::unbanFinished};
    QSignalSpy drained{&model, &BanListModel::drained};
    QVERIFY(model.unbanAt(0));
    QTRY_VERIFY(entered.available() > 0);
    model.beginShutdown();
    QVERIFY(!model.unbanAt(0));
    model.refresh();
    bool gui_progress{false};
    QTimer::singleShot(0, this, [&] { gui_progress = true; });
    QTRY_VERIFY(gui_progress);
    QVERIFY(!model.isDrained());
    release.release();
    QTRY_COMPARE(drained.count(), 1);
    QCOMPARE(completed.count(), 0);
    QCOMPARE(node.calls.getBanned.load(), 1);
}

void BanListModelTests::externalMutationDefersNotificationsUntilCompletion()
{
    MockNode node;
    node.get_banned_fn = [](banmap_t&) { return true; };
    DrainingBanListModel model{node};
    model.setExternalActionPending(true);
    for (int i = 0; i < 100; ++i) model.refresh();
    QTest::qWait(20);
    QCOMPARE(node.calls.getBanned.load(), 0);
    model.finishExternalAction(true);
    QTRY_VERIFY(!model.refreshPending());
    QCOMPARE(node.calls.getBanned.load(), 1);
}

void BanListModelTests::disconnectCompletionFlushesDeferredBanNotification()
{
    MockNode node;
    node.get_banned_fn = [](banmap_t& result) {
        result.emplace(ParseSubnet("10.0.0.0/8"), MakeBanEntry(1'900'000'000));
        return true;
    };
    DrainingBanListModel model{node};
    model.setExternalActionPending(true);
    for (int i = 0; i < 100; ++i) model.refresh();
    QCOMPARE(node.calls.getBanned.load(), 0);
    // Disconnect does not itself change bans, but an RPC notification arrived
    // while its shared command queue was busy.
    model.finishExternalAction(false);
    QTRY_VERIFY(!model.refreshPending());
    QCOMPARE(node.calls.getBanned.load(), 1);
    QCOMPARE(model.count(), 1);
    QVERIFY(!model.actionPending());
    model.setExternalActionPending(true);
    model.finishExternalAction(false);
    QTest::qWait(20);
    QCOMPARE(node.calls.getBanned.load(), 1);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(BanListModelTests)
#else
QTEST_MAIN(BanListModelTests)
#endif
#include "test_banlistmodel.moc"
