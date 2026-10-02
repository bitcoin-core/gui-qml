// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/components/widgets/halvingmodel.h>
#include <QtTest/QtTest>

class HalvingModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void boundariesReorgAndUnavailable()
    {
        HalvingModel model{210000, 600};
        QVERIFY(!model.available());
        model.setHeight(839999);
        QCOMPARE(model.blocksLeft(), 1);
        QCOMPARE(model.subsidy(), 6.25);
        QCOMPARE(model.nextSubsidy(), 3.125);
        QCOMPARE(model.secondsRemaining(), 600.0);
        model.setHeight(840000);
        QCOMPARE(model.blocksLeft(), 210000);
        QCOMPARE(model.periodStart(), 840000);
        QCOMPARE(model.nextHeight(), 1050000);
        QCOMPARE(model.progress(), 0.0);
        QCOMPARE(model.subsidy(), 3.125);
        QCOMPARE(model.nextSubsidy(), 1.5625);
        model.setHeight(839999); // Reorg across the subsidy boundary.
        QCOMPARE(model.nextHeight(), 840000);
        model.setHeight(-1);
        QVERIFY(!model.available());
        QCOMPARE(model.blocksLeft(), -1);
    }
    void networkParametersAndFinalSatoshi()
    {
        HalvingModel model{150, 600};
        model.setHeight(149);
        QCOMPARE(model.blocksLeft(), 1);
        model.setHeight(150);
        QCOMPARE(model.subsidy(), 25.0);
        model.setHeight(32 * 150);
        QCOMPARE(model.subsidy(), 0.00000001);
        QCOMPARE(model.nextSubsidy(), 0.0);
        QVERIFY(!model.complete());
        model.setHeight(33 * 150);
        QVERIFY(model.complete());
        QCOMPARE(model.blocksLeft(), 0);
        QCOMPARE(model.nextHeight(), -1);
        QCOMPARE(model.progress(), 1.0);
        model.setHeight(64 * 150);
        QCOMPARE(model.subsidy(), 0.0);
    }
};
QTEST_GUILESS_MAIN(HalvingModelTests)
#include "test_halvingmodel.moc"
