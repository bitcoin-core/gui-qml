// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/nodelifecycle.h>
#include <QtTest/QtTest>

class NodeLifecycleTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initializationHasOneOriginalAttempt() {
        nodelifecycle::Flow flow;
        QCOMPARE(flow.phase(),nodelifecycle::Phase::Constructed);QVERIFY(!flow.ready());
        const auto id=flow.start();QVERIFY(id);QVERIFY(!flow.start());QVERIFY(!flow.ready());
        QVERIFY(!flow.initialized({*id+1,true,true,false}));QVERIFY(!flow.initialization());
        QVERIFY(!flow.initialized({*id,false,true,false}));QVERIFY(!flow.ready());
        QVERIFY(flow.initialized({*id,true,true,false}));QVERIFY(flow.ready());
        QVERIFY(!flow.initialized({*id,true,false,false}));QVERIFY(flow.ready());
    }
    void actualInitializationOutcome_data() {
        QTest::addColumn<bool>("attempted");QTest::addColumn<bool>("success");QTest::addColumn<bool>("cancelled");
        QTest::newRow("not-started")<<false<<false<<false;
        QTest::newRow("actual-failure")<<true<<false<<false;
        QTest::newRow("actual-ready")<<true<<true<<false;
        QTest::newRow("cancelled-before-ready")<<true<<false<<true;
        QTest::newRow("successful-effect-after-cancel")<<true<<true<<true;
    }
    void actualInitializationOutcome() {
        QFETCH(bool,attempted);QFETCH(bool,success);QFETCH(bool,cancelled);
        nodelifecycle::Flow flow;const auto id=flow.start();QVERIFY(id);
        QVERIFY(flow.initialized({*id,attempted,success,cancelled}));
        QVERIFY(flow.initialization());QCOMPARE(flow.initialization()->attempted,attempted);
        QCOMPARE(flow.initialization()->success,success);QCOMPARE(flow.initialization()->cancelled,cancelled);
        QCOMPARE(flow.ready(),success&&!cancelled);QCOMPARE(flow.startupFailed(),!success&&!cancelled);
        QCOMPARE(flow.phase(),cancelled?nodelifecycle::Phase::Stopping:success?nodelifecycle::Phase::Ready:nodelifecycle::Phase::StartupFailed);
    }
    void stopFencesQueuedSuccessButRetainsItsFact() {
        nodelifecycle::Flow flow;const auto id=flow.start();QVERIFY(id);QVERIFY(flow.stop());
        QVERIFY(!flow.stop());QVERIFY(flow.initialized({*id,true,true,false}));
        QVERIFY(flow.initialization()->success);QVERIFY(!flow.ready());
        QCOMPARE(flow.phase(),nodelifecycle::Phase::Stopping);QVERIFY(!flow.start());
    }
    void stopBeforeStartNeverAdmitsInitialization() {
        nodelifecycle::Flow flow;QVERIFY(flow.stop());QVERIFY(!flow.start());QVERIFY(!flow.initialization());
        QVERIFY(flow.beginInterrupt());QVERIFY(!flow.beginInterrupt());
        QVERIFY(flow.interrupted());QVERIFY(flow.beginShutdown());QVERIFY(flow.coreStopped(0));
        QVERIFY(flow.drained());QCOMPARE(flow.phase(),nodelifecycle::Phase::Stopped);
    }
    void cleanupRequiresOrderedPhysicalReceipts_data() {
        QTest::addColumn<int>("status");QTest::newRow("normal")<<0;QTest::newRow("Core-failure")<<1;QTest::newRow("retained-nonzero")<<7;
    }
    void cleanupRequiresOrderedPhysicalReceipts() {
        QFETCH(int,status);nodelifecycle::Flow flow;const auto id=flow.start();QVERIFY(id);
        QVERIFY(!flow.beginShutdown());QVERIFY(!flow.coreStopped(status));QVERIFY(!flow.drained());
        QVERIFY(flow.beginInterrupt());QVERIFY(!flow.beginShutdown());QVERIFY(!flow.drained());
        QVERIFY(flow.interrupted());QVERIFY(!flow.interrupted());QVERIFY(!flow.coreStopped(status));
        QVERIFY(flow.beginShutdown());QVERIFY(!flow.beginShutdown());QVERIFY(!flow.drained());
        QVERIFY(flow.coreStopped(status));QVERIFY(!flow.coreStopped(0));
        QVERIFY(flow.coreExitStatus());QCOMPARE(*flow.coreExitStatus(),status);QVERIFY(flow.drained());
        QVERIFY(!flow.drained());QVERIFY(!flow.beginInterrupt());QVERIFY(!flow.beginShutdown());QVERIFY(!flow.start());
        QVERIFY(flow.coreExitStatus());QCOMPARE(*flow.coreExitStatus(),status);
    }
    void cancelledNativeInitializationStillAllowsOneControlTurn() {
        nodelifecycle::Flow flow;const auto id=flow.start();QVERIFY(id);
        QVERIFY(flow.initialized({*id,true,false,true}));QVERIFY(!flow.stop());
        QVERIFY(flow.beginInterrupt());QVERIFY(flow.interrupted());QVERIFY(!flow.ready());
    }
    void fatalCannotBeReopened_data() {
        QTest::addColumn<int>("before");for(int before=0;before<5;++before)QTest::newRow(qPrintable(QString::number(before)))<<before;
    }
    void fatalCannotBeReopened() {
        QFETCH(int,before);nodelifecycle::Flow flow;std::optional<uint64_t> id;
        if(before>0)id=flow.start();
        if(before>1)QVERIFY(flow.initialized({*id,true,true,false}));
        if(before>2)QVERIFY(flow.beginInterrupt());
        if(before>3)QVERIFY(flow.interrupted());
        QVERIFY(flow.fatal(QStringLiteral("original fatal reason")));QVERIFY(!flow.fatal(QStringLiteral("later reason")));
        QCOMPARE(flow.error(),QStringLiteral("original fatal reason"));QCOMPARE(flow.phase(),nodelifecycle::Phase::Fatal);
        QVERIFY(!flow.ready());QVERIFY(!flow.stop());QVERIFY(!flow.start());QVERIFY(!flow.beginInterrupt());
        QVERIFY(!flow.interrupted());QVERIFY(!flow.beginShutdown());QVERIFY(!flow.coreStopped(0));QVERIFY(!flow.drained());
        if(id&&!flow.initialization()){QVERIFY(flow.initialized({*id,true,true,false}));QVERIFY(flow.initialization()->success);}
        QCOMPARE(flow.phase(),nodelifecycle::Phase::Fatal);
    }
    void fatalSummaryIsBoundedAndTerminalResultDoesNotChange() {
        nodelifecycle::Flow fatal;QVERIFY(fatal.fatal(QString(100'000,QLatin1Char('x'))));QCOMPARE(fatal.error().size(),qsizetype{16'384});
        nodelifecycle::Flow stopped;QVERIFY(stopped.beginInterrupt());QVERIFY(stopped.interrupted());
        QVERIFY(stopped.beginShutdown());QVERIFY(stopped.coreStopped(1));QVERIFY(stopped.drained());
        QVERIFY(!stopped.fatal(QStringLiteral("late event")));QVERIFY(stopped.coreExitStatus());QCOMPARE(*stopped.coreExitStatus(),1);
    }
};
#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(NodeLifecycleTests)
#else
QTEST_MAIN(NodeLifecycleTests)
#endif
#include <test_nodelifecycle.moc>
