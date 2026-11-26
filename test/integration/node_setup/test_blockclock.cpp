// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_util.h>
#include <qml/models/blockclockmodel.h>

#include <QDateTime>
#include <QScopeGuard>
#include <QSignalSpy>

#include <algorithm>
#include <cmath>

class BlockClockIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

public:
    explicit BlockClockIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void blockClockTracksNetworkActivity()
    {
        auto* clock = qmltest::Find(m_app, "blockClock");
        QVERIFY(clock);
        QTRY_COMPARE(clock->property("state").toString(), QStringLiteral("CONNECTING"));
        QCOMPARE(clock->property("header").toString(), QStringLiteral("Connecting"));
        QCOMPARE(clock->property("subText").toString(), QStringLiteral("Please wait"));
        m_app.nodeModel().setPause(true);
        QTRY_COMPARE(clock->property("state").toString(), QStringLiteral("PAUSE"));
        QTRY_VERIFY(!qmltest::Call(m_app, &interfaces::Node::getNetworkActive));
        QCOMPARE(clock->property("header").toString(), QStringLiteral("Paused"));
        QCOMPARE(clock->property("subText").toString(), QString{});
        m_app.nodeModel().setPause(false);
        QTRY_COMPARE(clock->property("state").toString(), QStringLiteral("CONNECTING"));
        QTRY_VERIFY(qmltest::Call(m_app, &interfaces::Node::getNetworkActive));
        QCOMPARE(clock->property("header").toString(), QStringLiteral("Connecting"));
        QCOMPARE(clock->property("subText").toString(), QStringLiteral("Please wait"));
    }

    void chainSyncFollowsRealCoreBlockNotifications()
    {
        auto* model = &m_app.nodeModel();
        QVERIFY(model);
        const int previous_height = qmltest::Call(m_app, &interfaces::Node::getNumBlocks);
        QSignalSpy tips{model, &NodeModel::blockTipHeightChanged};
        UniValue params{UniValue::VARR};
        params.push_back(1);
        params.push_back("raw(51)");
        QCOMPARE(qmltest::Call(m_app, &interfaces::Node::executeRpc, "generatetodescriptor", params, "").size(), 1U);
        QTRY_COMPARE(model->blockTipHeight(), previous_height + 1);
        QCOMPARE(model->blockTipHeight(), qmltest::Call(m_app, &interfaces::Node::getNumBlocks));
        QVERIFY(!tips.isEmpty());
        QVERIFY(model->verificationProgress() > 0.0);
    }

    void historyTracksEqualTimestampsDisconnectsAndReplacementBlocks()
    {
        auto* model = qmltest::Model<BlockClockModel>(m_app, "blockClockModel");
        auto* dial = qmltest::Find(m_app, "blockClockDial");
        QVERIFY(model);
        QVERIFY(dial);
        const auto rpc = [&](const char* method, const UniValue& params) {
            return qmltest::Call(m_app, &interfaces::Node::executeRpc, method, params, "");
        };
        const auto mock_time = [&](qint64 timestamp) {
            UniValue params{UniValue::VARR};
            params.push_back(timestamp);
            rpc("setmocktime", params);
        };
        const auto restore_time = qScopeGuard([&] { mock_time(0); });
        const auto generate = [&](int count) {
            UniValue params{UniValue::VARR};
            params.push_back(count);
            params.push_back("raw(51)");
            return rpc("generatetodescriptor", params);
        };
        const auto matches = [&](const QList<qint64>& timestamps) {
            // Recalculate at each poll, including across noon/midnight.
            QList<qreal> expected;
            for (const auto timestamp : timestamps) {
                if (timestamp >= model->periodStart() && timestamp < model->periodStart() + BlockClockModel::PERIOD_SECONDS) {
                    expected.append(static_cast<qreal>(timestamp - model->periodStart()) / BlockClockModel::PERIOD_SECONDS);
                }
            }
            std::sort(expected.begin(), expected.end());
            const auto actual = model->blockTimeFractions();
            if (actual.size() != expected.size()) return false;
            for (qsizetype i{0}; i < actual.size(); ++i) {
                if (!std::isfinite(actual[i]) || std::abs(actual[i] - expected[i]) >= 1e-10) return false;
            }
            return dial->property("blockTimeFractions").value<QList<qreal>>() == actual;
        };

        mock_time(1'300'000'000);
        QCOMPARE(generate(100).size(), 100U);
        QTRY_VERIFY(matches({}));

        const auto timestamp = QDateTime::currentSecsSinceEpoch();
        mock_time(timestamp);
        const auto blocks = generate(3);
        QCOMPARE(blocks.size(), 3U);
        QTRY_VERIFY(matches({timestamp, timestamp, timestamp}));

        UniValue invalidate{UniValue::VARR};
        invalidate.push_back(blocks[1].get_str());
        rpc("invalidateblock", invalidate);
        QTRY_VERIFY(matches({timestamp}));

        mock_time(timestamp + 1);
        QCOMPARE(generate(2).size(), 2U);
        QTRY_VERIFY(matches({timestamp, timestamp + 1, timestamp + 1}));
    }
};

std::unique_ptr<QObject> CreateBlockClockTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<BlockClockIntegrationTests>(app);
}

#include <test_blockclock.moc>
