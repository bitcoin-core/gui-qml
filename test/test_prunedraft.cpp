// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/core_settings.h>
#include <qml/models/core_settings_model.h>
#include <qml/onboarding_storage.h>
#include <qml/guiconstants.h>
#include <test/mocks/manualstartupexecution.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <deque>
#include <limits>
namespace {
QmlCoreSettings::Values Original(){QmlCoreSettings::Values result;result.prune=true;result.prune_target_mib=550;result.prune_size_gb=1;return result;}
bool Settle(ManualStartupExecution& transport, std::deque<std::function<void()>>& gui)
{
    for (int turn = 0; turn < 4096; ++turn) {
        transport.drain();
        if (gui.empty()) return transport.quiescent();
        auto publication = std::move(gui.front());
        gui.pop_front();
        publication();
    }
    return false;
}
}
class PruneDraftTests:public QObject {
    Q_OBJECT
private Q_SLOTS:
    void customSelectionRetainsTheExactNativeTarget_data(){QTest::addColumn<qint64>("target");QTest::newRow("minimum-550")<<qint64{550};QTest::newRow("non-round-1000")<<qint64{1000};QTest::newRow("manual-one")<<qint64{1};}
    void customSelectionRetainsTheExactNativeTarget(){QFETCH(qint64,target);QmlCoreSettings::Session session{Original()};QVERIFY(session.changePruneTargetText(QString::number(target)).accepted);QCOMPARE(session.values().prune_target_mib,target);QVERIFY(session.chooseStorage(QmlCoreSettings::StorageChoice::Reduced).accepted);QCOMPARE(session.values().prune_size_gb,2);QCOMPARE(session.customPruneTarget(),target);QVERIFY(session.chooseStorage(QmlCoreSettings::StorageChoice::Custom).accepted);QCOMPARE(session.values().prune_target_mib,target);QCOMPARE(session.pruneTargetText(),QString::number(target));QVERIFY(session.chooseStorage(QmlCoreSettings::StorageChoice::Full).accepted);QVERIFY(session.chooseStorage(QmlCoreSettings::StorageChoice::Custom).accepted);QCOMPARE(session.values().prune_target_mib,target);}
    void invalidInputIsOwnedOutsideTheCanonicalValue_data(){QTest::addColumn<QString>("text");QTest::newRow("numeric-prefix")<<QStringLiteral("550x");QTest::newRow("below-minimum")<<QStringLiteral("549");QTest::newRow("empty")<<QString{};QTest::newRow("bounded-rejection")<<QString(100,'9');}
    void invalidInputIsOwnedOutsideTheCanonicalValue(){QFETCH(QString,text);QmlCoreSettings::Session session{Original()};QVERIFY(!session.changePruneTargetText(text).accepted);QCOMPARE(session.values().prune_target_mib,int64_t{550});QCOMPARE(session.pruneTargetText(),text.left(65));QVERIFY(!session.pruneTargetError().isEmpty());auto newer=Original();newer.prune_target_mib=1000;session.applyPreviewValuesPreservingTouched(newer,{});QCOMPARE(session.values().prune_target_mib,int64_t{550});QCOMPARE(session.pruneTargetText(),text.left(65));QVERIFY(session.discardPruneInput().accepted);QCOMPARE(session.pruneTargetText(),QStringLiteral("550"));QVERIFY(session.pruneTargetError().isEmpty());}
    void nativeModelPublishesRawInputAfterDomainWork()
    {
        ManualStartupExecution transport;
        std::deque<std::function<void()>> gui;
        {
            CoreSettingsModel model{Original()};
            model.configureServices({{}, [&](auto turn) { gui.push_back(std::move(turn)); }}, &transport.executor());
            model.refresh();
            QVERIFY(Settle(transport, gui));
            QCOMPARE(model.pruneTargetText(), QStringLiteral("550"));

            QVERIFY(model.changePruneTargetText("0550"));
            QCOMPARE(model.pruneTargetText(), QStringLiteral("550"));
            QVERIFY(model.pending());
            QVERIFY(transport.step(execution::Lane::Utility));
            QCOMPARE(model.pruneTargetText(), QStringLiteral("550"));
            QVERIFY(Settle(transport, gui));
            QCOMPARE(model.pruneTargetText(), QStringLiteral("0550"));
            QCOMPARE(model.values().prune_target_mib, int64_t{550});
        }
        QVERIFY(Settle(transport, gui));
    }

    void oldWriteReceiptCannotEraseTheSuccessorInvalidLiteral()
    {
        ManualStartupExecution transport;
        std::deque<std::function<void()>> gui;
        {
            CoreSettingsModel model{Original()};
            model.configureServices({{}, [&](auto turn) { gui.push_back(std::move(turn)); }}, &transport.executor());
            std::function<void(CoreSettingsModel::EffectResult)> complete;
            QmlCoreSettings::Values persisted;
            int admissions{0};
            model.setEffectPort([&](const QmlCoreSettings::Change& change, auto, auto, auto done, auto) {
                if (!change.accepted) {
                    done(CoreSettingsModel::EffectResult{});
                    return;
                }
                ++admissions;
                persisted = change.after;
                complete = std::move(done);
            }, {}, {});
            model.refresh();
            QVERIFY(Settle(transport, gui));
            QVERIFY(model.changePruneTargetText("1000"));
            QVERIFY(Settle(transport, gui));
            QVERIFY(complete);
            QCOMPARE(admissions, 1);
            QVERIFY(model.changePruneTargetText("1000x"));
            QVERIFY(Settle(transport, gui));
            QCOMPARE(model.pruneTargetText(), QStringLiteral("1000x"));
            QVERIFY(!model.pruneTargetError().isEmpty());

            auto done = std::exchange(complete, {});
            done({{}, persisted, {}, {}});
            QVERIFY(Settle(transport, gui));
            QCOMPARE(model.pruneTargetText(), QStringLiteral("1000x"));
            QVERIFY(!model.pruneTargetError().isEmpty());
            QCOMPARE(model.values().prune_target_mib, int64_t{1000});
            QCOMPARE(admissions, 1);
        }
        QVERIFY(Settle(transport, gui));
    }
    void manualModeDoesNotPromiseAnAutomaticStorageLimit(){QmlOnboardingStorage::State state;state.result_valid=true;state.available_bytes=100*GB_BYTES;state.assumed_blockchain_size_gb=600;state.assumed_chainstate_size_gb=12;state.prune=true;state.prune_size_gb=2;state.manual_pruning=true;state.custom_manual_pruning=true;const auto info=QmlOnboardingStorage::Evaluate(state);QCOMPARE(info.selected_required_gb,612);QVERIFY(!info.enough_for_selected);QVERIFY(info.enough_for_reduced);QVERIFY(!info.enough_for_custom);}
    void reducedPresetBudgetNeverUsesTheCustomTarget(){QmlOnboardingStorage::State state;state.result_valid=true;state.available_bytes=15*GB_BYTES;state.assumed_blockchain_size_gb=600;state.assumed_chainstate_size_gb=12;state.prune=true;state.prune_size_gb=state.custom_prune_size_gb=7;const auto info=QmlOnboardingStorage::Evaluate(state);QCOMPARE(info.pruned_required_gb,19);QCOMPARE(info.reduced_required_gb,14);QVERIFY(info.enough_for_reduced);QVERIFY(!info.enough_for_selected);QVERIFY(!info.enough_for_custom);}
    void storageBudgetArithmeticSaturatesBeforeProjection(){QmlOnboardingStorage::State state;state.result_valid=true;state.available_bytes=std::numeric_limits<uint64_t>::max();state.assumed_blockchain_size_gb=state.assumed_chainstate_size_gb=state.prune_size_gb=state.custom_prune_size_gb=std::numeric_limits<int>::max();state.prune=true;const auto info=QmlOnboardingStorage::Evaluate(state);QCOMPARE(info.full_required_gb,std::numeric_limits<int>::max());QCOMPARE(info.pruned_required_gb,std::numeric_limits<int>::max());QCOMPARE(info.selected_required_gb,std::numeric_limits<int>::max());QVERIFY(info.enough_for_selected);}
};
BITCOINQML_REGISTER_QT_TEST(PruneDraftTests)
#include <test_prunedraft.moc>
