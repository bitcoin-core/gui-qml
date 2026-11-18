// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <QSet>
#include <QDateTime>
#include <QLocale>
#include <QScopeGuard>

#include <test/mocks/mocknode.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <QPersistentModelIndex>
#include <QThread>
#include <atomic>
#include <test/mocks/drainexecutors.h>
#include <qml/models/banlistmodel.h>
#include <util/translation.h>

#include <netbase.h>

#include <map>
#include <stdexcept>
#include <utility>

namespace {
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
    void localizedBanExpiryUsesTheLocaleTimeConvention_data();
    void localizedBanExpiryUsesTheLocaleTimeConvention();
    void refreshPopulatesRolesAndRows();
    void unbanAtTargetsSelectedSubnet();
    void unbanOutcomeReportsNodeRejection();
    void unbanAtIgnoresInvalidRows();
    void manualReadFailureAndRejection_data();
    void manualReadFailureAndRejection();
    void manualUnbanKeepsOriginalSubnetAndAcceptedEffect();
    void realThreadReadsNeverRunOnGui();
};

void BanListModelTests::localizedBanExpiryUsesTheLocaleTimeConvention_data()
{
    QTest::addColumn<QString>("name"); QTest::newRow("de-DE-24-hour") << QStringLiteral("de_DE"); QTest::newRow("en-US-12-hour") << QStringLiteral("en_US");
}
void BanListModelTests::localizedBanExpiryUsesTheLocaleTimeConvention()
{
    QFETCH(QString, name); const QLocale previous; const QLocale locale{name}; QLocale::setDefault(locale);
    const auto restore = qScopeGuard([&] { QLocale::setDefault(previous); });
    MockNode node; const auto drain = DrainExecutorsBeforeNativeOwnerDestruction();
    const auto timestamp = QDateTime(QDate{2026, 11, 15}, QTime{18, 30}).toSecsSinceEpoch();
    node.get_banned_fn = [&](banmap_t& result) { result = {{ParseSubnet("10.0.0.0/8"), MakeBanEntry(timestamp)}}; return true; };
    BanListModel model{node, nullptr}; model.refresh(); QTRY_COMPARE(model.rowCount(), 1);
    const auto expected = locale.toString(QDateTime::fromSecsSinceEpoch(timestamp), QLocale::ShortFormat);
    QCOMPARE(model.index(0).data(int(BanListModel::BanRoles::BanUntilRole)).toString(), expected);
    QCOMPARE(node.calls.getBanned.load(), 1);
}

void BanListModelTests::refreshPopulatesRolesAndRows()
{
    banmap_t banned;
    const CSubNet subnet_a = ParseSubnet("10.0.0.0/8");
    const CSubNet subnet_b = ParseSubnet("127.0.0.1/32");
    banned.emplace(subnet_a, MakeBanEntry(1'900'000'000));
    banned.emplace(subnet_b, MakeBanEntry(2'000'000'000));

    MockNode node;
    const auto drain_native_owner = DrainExecutorsBeforeNativeOwnerDestruction();
    node.get_banned_fn = [&](banmap_t& result) {
        result = banned;
        return true;
    };

    BanListModel model{node, nullptr};
    QSignalSpy count_spy(&model, &BanListModel::countChanged);

    model.refresh();
    QTRY_VERIFY(model.count() > 0);

    QCOMPARE(node.calls.getBanned.load(), 1);
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);
    QCOMPARE(count_spy.count(), 2);

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
    const auto drain_native_owner = DrainExecutorsBeforeNativeOwnerDestruction();
    node.get_banned_fn = [&](banmap_t& result) {
        result = banned;
        return true;
    };

    BanListModel model{node, nullptr};
    model.refresh();
    QTRY_VERIFY(model.count() > 0);

    bool received_expected_subnet{false};
    node.unban_fn = [&](const CSubNet& value) {
        received_expected_subnet = value.ToString() == subnet.ToString();
        return true;
    };
    QSignalSpy finished{&model, &BanListModel::unbanFinished};
    QVERIFY(model.unbanAt(0));
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(finished.front().front().toBool(), true);
    QTRY_COMPARE(node.calls.getBanned.load(), 2);
    QCOMPARE(node.calls.unban.load(), 1);
    QVERIFY(received_expected_subnet);
}

void BanListModelTests::unbanOutcomeReportsNodeRejection()
{
    banmap_t banned;
    banned.emplace(ParseSubnet("10.0.0.0/8"), MakeBanEntry(1'900'000'000));

    MockNode node;
    const auto drain_native_owner = DrainExecutorsBeforeNativeOwnerDestruction();
    node.get_banned_fn = [&](banmap_t& result) {
        result = banned;
        return true;
    };

    BanListModel model{node, nullptr};
    model.refresh();
    QTRY_VERIFY(model.count() > 0);

    node.unban_fn = [](const CSubNet&) { return false; };
    QSignalSpy finished{&model, &BanListModel::unbanFinished};
    QVERIFY(model.unbanAt(0)); // Accepted request; the actual failure is asynchronous.
    QTRY_COMPARE(finished.size(), 1);
    QCOMPARE(finished.front().front().toBool(), false);
    QTRY_COMPARE(node.calls.getBanned.load(), 2);
    QCOMPARE(node.calls.unban.load(), 1);
}

void BanListModelTests::unbanAtIgnoresInvalidRows()
{
    banmap_t banned;
    banned.emplace(ParseSubnet("10.0.0.0/8"), MakeBanEntry(1'900'000'000));

    MockNode node;
    const auto drain_native_owner = DrainExecutorsBeforeNativeOwnerDestruction();
    node.get_banned_fn = [&](banmap_t& result) {
        result = banned;
        return true;
    };

    BanListModel model{node, nullptr};
    model.refresh();
    QTRY_VERIFY(model.count() > 0);

    QVERIFY(!model.unbanAt(-1));
    QVERIFY(!model.unbanAt(42));
    QCOMPARE(node.calls.getBanned.load(), 1);
    QCOMPARE(node.calls.unban.load(), 0);
}

void BanListModelTests::manualReadFailureAndRejection_data()
{
    QTest::addColumn<bool>("reject");
    QTest::newRow("native-failure")<<false;
    QTest::newRow("transport-rejection")<<true;
}
void BanListModelTests::manualReadFailureAndRejection()
{
    QFETCH(bool,reject);
    ManualRoutedExecution transport;
    std::deque<std::function<void()>> gui;
    bool fail{false};
    banmap_t banned{{ParseSubnet("10.0.0.0/8"),MakeBanEntry(1'900'000'000)}};
    MockNode node;
    const auto drain_native_owner=DrainExecutorsBeforeNativeOwnerDestruction();
    node.get_banned_fn=[&](auto& result) { result=banned;return !fail; };
    BanListModel model{node,nullptr,true,{transport.scheduler,[&](auto turn) { gui.push_back(std::move(turn)); }}};
    auto publish=[&] { if(gui.empty())return false;auto turn=std::move(gui.front());gui.pop_front();turn();return true; };
    model.refresh(); QCOMPARE(model.count(),0); QCOMPARE(node.calls.getBanned.load(),0);
    QVERIFY(transport.step(execution::Lane::Core));QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(publish());
    QCOMPARE(model.count(),1);QPersistentModelIndex persistent{model.index(0)};
    const auto address=model.data(persistent,int(BanListModel::BanRoles::AddressRole)).toString();
    fail=true;if(reject)transport.rejectLane(execution::Lane::Core);model.refresh();
    if(!reject)QVERIFY(transport.step(execution::Lane::Core));
    // Rejected admission owns an explicit outcome too; deliver it before GUI publication.
    transport.drain();while(publish()){}
    QVERIFY(!model.readError().isEmpty());QVERIFY(!model.projectionPending());QCOMPARE(model.count(),1);
    QVERIFY(persistent.isValid());QCOMPARE(model.data(persistent,int(BanListModel::BanRoles::AddressRole)).toString(),address);
    QSignalSpy changes{&model,&QAbstractItemModel::rowsRemoved};
    fail=false;banned.clear();model.refresh();QVERIFY(transport.step(execution::Lane::Core));QVERIFY(transport.step(execution::Lane::Domain));
    model.closeAdmission();while(publish()){}QCOMPARE(changes.size(),0);transport.drain();QVERIFY(transport.quiescent());
}
void BanListModelTests::manualUnbanKeepsOriginalSubnetAndAcceptedEffect()
{
    ManualRoutedExecution transport;
    std::deque<std::function<void()>> gui;
    const auto original=ParseSubnet("10.0.0.0/8");
    banmap_t banned{{original,MakeBanEntry(1'900'000'000)}};
    MockNode node;
    const auto drain_native_owner=DrainExecutorsBeforeNativeOwnerDestruction();
    node.get_banned_fn=[&](auto& result){result=banned;return true;};
    std::string actual;
    node.unban_fn=[&](const auto& subnet){actual=subnet.ToString();return true;};
    BanListModel model{node,nullptr,true,{transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}}};
    model.refresh();QVERIFY(transport.step(execution::Lane::Core));QVERIFY(transport.step(execution::Lane::Domain));
    auto turn=std::move(gui.front());gui.pop_front();turn();QCOMPARE(model.count(),1);
    QSignalSpy finished{&model,&BanListModel::unbanFinished};
    QVERIFY(model.unbanAt(0));QVERIFY(!model.unbanAt(0));
    banned.clear();banned.emplace(ParseSubnet("192.0.2.0/24"),MakeBanEntry(2'000'000'000));
    model.closeAdmission();QCOMPARE(node.calls.unban.load(),0);
    QVERIFY(transport.step(execution::Lane::Core));QCOMPARE(actual,original.ToString());
    QVERIFY(transport.step(execution::Lane::Domain));
    while(!gui.empty()){auto publish=std::move(gui.front());gui.pop_front();publish();}
    QCOMPARE(finished.size(),0);QCOMPARE(node.calls.unban.load(),1);
    transport.drain();QVERIFY(transport.quiescent());
}
void BanListModelTests::realThreadReadsNeverRunOnGui()
{
    MockNode node;
    const auto drain_native_owner=DrainExecutorsBeforeNativeOwnerDestruction();
    const auto* gui=QThread::currentThread();std::atomic<bool> off_gui{false};
    node.get_banned_fn=[&](auto& result){off_gui=QThread::currentThread()!=gui;
        result={{ParseSubnet("10.0.0.0/8"),MakeBanEntry(1'900'000'000)}};return true;};
    BanListModel model{node,nullptr};model.refresh();QTRY_COMPARE(model.count(),1);QVERIFY(off_gui.load());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(BanListModelTests)
#else
QTEST_MAIN(BanListModelTests)
#endif
#include <test_banlistmodel.moc>
