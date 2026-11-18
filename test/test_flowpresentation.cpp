// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/flowpresentation.h>
#include <qml/executor.h>
#include <qml/snapshotquery.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>
#include <consensus/amount.h>
#include <QScopeGuard>
#include <QtTest/QtTest>

#include <atomic>
#include <cmath>

namespace {
QVariantMap Entry(QString id, qint64 amount, QString ownership = "external")
{
    return {{"id", id}, {"kind", "output"}, {"amountKnown", true}, {"amountSat", amount}, {"ownership", ownership}};
}
QVariantMap Flow(int outputs)
{
    QVariantList values;
    for (int i = 0; i < outputs; ++i) values.append(Entry(QString::number(i), 10000, i < 2 ? "wallet" : "external"));
    return {{"inputs", QVariantList{Entry("input", qint64(outputs) * 10000 + 800, "wallet")}}, {"outputs", values},
        {"inputCount", 1}, {"outputCount", outputs}, {"complete", true}, {"feeKnown", true}, {"feeSat", 800}};
}
qint64 Total(const QVariantList& values)
{
    qint64 amount{0}; for (const auto& value : values) amount += value.toMap().value("amountSat").toLongLong(); return amount;
}
}
class FlowPresentationTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void completeGraphGroupsMoneyWithoutLosingOriginalCountOrSource() {
        const auto source = Flow(1001); const auto result = PrepareTransactionFlowPresentation(source);
        const auto expanded = result.value("expandedOutputs").toList(), collapsed = result.value("collapsedOutputs").toList();
        QCOMPARE(expanded.size(), qsizetype{16}); QCOMPARE(collapsed.size(), qsizetype{4});
        QCOMPARE(result.value("outputCount").toInt(), 1001); QVERIFY(result.value("presentationLimited").toBool());
        QCOMPARE(expanded[14].toMap().value("outputCount").toInt(), 987);
        QCOMPARE(collapsed[2].toMap().value("outputCount").toInt(), 999);
        QCOMPARE(Total(expanded), qint64{10'010'800}); QCOMPARE(Total(collapsed), Total(expanded));
        QVERIFY(result.value("expandedFacts").toMap().value("proportional").toBool());
        QCOMPARE(source.value("outputs").toList().size(), qsizetype{1001});
    }
    void unknownGroupNeverPresentsPartialMoneyAsKnown() {
        auto source = Flow(20); auto values = source.value("outputs").toList();
        auto missing = values[18].toMap(); missing["amountKnown"] = false; missing["amountSat"] = QVariant{};
        values[18] = missing; source["outputs"] = values; source["complete"] = false;
        const auto result = PrepareTransactionFlowPresentation(source);
        const auto group = result.value("expandedOutputs").toList()[14].toMap();
        QVERIFY(!group.value("amountKnown").toBool()); QVERIFY(!group.value("amountSat").isValid());
        QVERIFY(!result.value("expandedFacts").toMap().value("amountsKnown").toBool());
    }
    void unknownInputsOnlyInferDisplayWidthsWithoutInventingMoney() {
        auto source = Flow(3); auto input = Entry("unknown", 0); input["amountKnown"] = false; input["amountSat"] = QVariant{};
        source["inputs"] = QVariantList{input}; source["feeKnown"] = false; source["feeSat"] = QVariant{}; source["complete"] = false;
        const auto result = PrepareTransactionFlowPresentation(source), facts = result.value("expandedFacts").toMap();
        QVERIFY(facts.value("inferredInputs").toBool()); QVERIFY(!facts.value("proportional").toBool());
        QVERIFY(!result.value("inputs").toList()[0].toMap().value("amountSat").isValid());
        double right{0}; for (const auto& width : facts.value("outputWidths").toList()) right += width.toDouble();
        QCOMPARE(facts.value("inputWidths").toList()[0].toDouble(), right);
    }
    void invalidAggregateKeepsGeometryFinite_data()
    {
        QTest::addColumn<QString>("side");
        QTest::newRow("input-total-outside-money-range") << QStringLiteral("inputs");
        QTest::newRow("output-total-outside-money-range") << QStringLiteral("outputs");
    }
    void invalidAggregateKeepsGeometryFinite()
    {
        QFETCH(QString, side);
        auto source = Flow(2);
        const QVariantList invalid{Entry("maximum", MAX_MONEY), Entry("one-more", 1)};
        source[side] = invalid;

        const auto result = PrepareTransactionFlowPresentation(source);
        for (const auto* name : {"expandedFacts", "collapsedFacts"}) {
            const auto facts = result.value(name).toMap();
            QVERIFY(!facts.value("amountsKnown").toBool());
            QVERIFY(!facts.value("proportional").toBool());
            for (const auto* widths_name : {"inputWidths", "outputWidths"}) {
                const auto widths = facts.value(widths_name).toList();
                QVERIFY(!widths.isEmpty());
                for (const auto& width : widths) {
                    QVERIFY(std::isfinite(width.toDouble()));
                    QVERIFY(width.toDouble() >= 1.0);
                }
            }
        }
        QCOMPARE(source.value(side).toList(), invalid);
    }
    void unknownFeeDiscardsCachedAmountButKnownZeroRemainsKnown() {
        auto source = Flow(3);
        source["feeKnown"] = false;
        // Keep the previous numeric observation, exactly as the QML regression does.
        QCOMPARE(source.value("feeSat").toLongLong(), qint64{800});
        const auto unknown = PrepareTransactionFlowPresentation(source);
        for (const auto* side : {"expandedOutputs", "collapsedOutputs"}) {
            const auto fee = unknown.value(side).toList().last().toMap();
            QVERIFY(!fee.value("amountKnown").toBool());
            QVERIFY(fee.value("amountSat").isNull());
            QCOMPARE(fee.value("amountSat").metaType(), QMetaType::fromType<std::nullptr_t>());
        }
        QVERIFY(!unknown.value("expandedFacts").toMap().value("amountsKnown").toBool());
        QCOMPARE(source.value("feeSat").toLongLong(), qint64{800});
        source["feeKnown"] = true;
        source["feeSat"] = qint64{0};
        const auto zero = PrepareTransactionFlowPresentation(source).value("expandedOutputs").toList().last().toMap();
        QVERIFY(zero.value("amountKnown").toBool());
        QVERIFY(zero.value("amountSat").isValid());
        QCOMPARE(zero.value("amountSat").toLongLong(), qint64{0});
    }
    void previewCapsTextAndRequestsWhileOriginalFieldsRemainIntact() {
        auto source = Flow(1); auto entries = source.value("outputs").toList(), requests = QVariantList{};
        for (int i = 0; i < 100; ++i) requests.append(QVariantMap{{"requestId", QString::number(i)}});
        auto entry = entries[0].toMap(); entry["dataText"] = QString(100000, 'x'); entry["paymentRequests"] = requests;
        entries[0] = entry; source["outputs"] = entries;
        const auto result = PrepareTransactionFlowPresentation(source).value("expandedOutputs").toList()[0].toMap();
        QVERIFY(result.value("textTruncated").toBool()); QVERIFY(result.value("dataText").toString().size() <= 1024);
        QCOMPARE(result.value("paymentRequests").toList().size(), qsizetype{4}); QCOMPARE(result.value("paymentRequestCount").toInt(), 100);
        QCOMPARE(source.value("outputs").toList()[0].toMap().value("dataText").toString().size(), qsizetype{100000});
    }
    void manualDispatchOutcomeAndGuiPublicationRemainSeparate() {
        ManualRoutedExecution commands; std::deque<std::function<void()>> turns;
        auto operation = std::make_shared<execution::OwnedCommand<QVariantMap>>(commands.scheduler, [&] { turns.emplace_back([] {}); });
        const auto source = Flow(1001);
        QVERIFY(operation->start([source] { return PrepareTransactionFlowPresentation(source); }, {execution::Lane::Domain, {}, execution::WorkClass::Observation}));
        QVERIFY(!operation->result()); QVERIFY(commands.step(execution::Lane::Domain)); QVERIFY(!operation->result());
        QVERIFY(commands.step(execution::Lane::Domain)); QVERIFY(operation->result()); QCOMPARE(turns.size(), size_t{1});
        QCOMPARE(operation->result()->value->value("outputCount").toInt(), 1001); commands.drain();
    }
    void realPreparationUsesTheInjectedDomainLane() {
        execution::Executor executor{execution::Executor::Config{}};
        auto utility = executor.makeQueue(execution::Lane::Utility);
        std::atomic<QThread*> prepared_on{nullptr};
        const auto cleanup = qScopeGuard([&] { executor.stopAndJoin(); });
        auto operation = std::make_shared<execution::OwnedCommand<QVariantMap>>(nullptr, [] {});
        QVERIFY(operation->startWith([&](auto done, auto failed) {
            auto task = execution::MakeTask([&] {
                prepared_on = QThread::currentThread();
                return PrepareTransactionFlowPresentation(Flow(1001));
            }, std::move(done), std::move(failed));
            const bool accepted = utility.post([task = std::move(task)] { task->execute(); task->deliver(); });
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
        }));
        QTRY_VERIFY(operation->result());
        QVERIFY(operation->result()->value);
        QCOMPARE(operation->result()->value->value("outputCount").toInt(), 1001);
        QVERIFY(prepared_on.load());
        QVERIFY(prepared_on.load() != QThread::currentThread());
    }
};
BITCOINQML_REGISTER_QT_TEST(FlowPresentationTests)
#include <test_flowpresentation.moc>
