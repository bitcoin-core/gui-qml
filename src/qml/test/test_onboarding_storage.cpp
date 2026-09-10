// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/guiconstants.h>
#include <qml/onboarding_storage.h>

namespace {

uint64_t GB(int gb)
{
    return static_cast<uint64_t>(gb) * GB_BYTES;
}

QmlOnboardingStorage::State MakeState()
{
    QmlOnboardingStorage::State state;
    state.result_valid = true;
    state.available_bytes = GB(700);
    state.assumed_blockchain_size_gb = 600;
    state.assumed_chainstate_size_gb = 12;
    state.prune = false;
    state.prune_size_gb = DEFAULT_PRUNE_TARGET_GB;
    return state;
}

} // namespace

class OnboardingStorageTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void pendingCheckReturnsCheckingStatus();
    void errorReturnsErrorStatus();
    void fullStorageEnoughReturnsOk();
    void prunedSelectionUsesPrunedRequirement();
    void insufficientSelectedStorageWarns();
    void nearMinimumStorageWarns();
    void pruneRecommendationUsesValidityAndMargin();
    void existingProfileDoesNotRecommendPruneOrRequireFreshChainSpace();
    void existingProfileBelowOperationalMinimumWarns();
};

void OnboardingStorageTests::pendingCheckReturnsCheckingStatus()
{
    auto state = MakeState();
    state.check_pending = true;
    state.result_valid = false;
    state.available_bytes = 0;

    const QmlOnboardingStorage::Info info = QmlOnboardingStorage::Evaluate(state);

    QCOMPARE(info.status, QStringLiteral("checking"));
    QCOMPARE(info.available_text, QStringLiteral("Checking available storage..."));
    QVERIFY(!info.enough_for_selected);
    QVERIFY(!info.enough_for_full);
}

void OnboardingStorageTests::errorReturnsErrorStatus()
{
    auto state = MakeState();
    state.result_valid = false;
    state.error_text = QStringLiteral("storage probe failed");

    const QmlOnboardingStorage::Info info = QmlOnboardingStorage::Evaluate(state);

    QCOMPARE(info.status, QStringLiteral("error"));
    QVERIFY(info.available_text.isEmpty());
    QVERIFY(info.warning_text.isEmpty());
    QVERIFY(!info.enough_for_selected);
    QVERIFY(!info.enough_for_full);
}

void OnboardingStorageTests::fullStorageEnoughReturnsOk()
{
    const QmlOnboardingStorage::Info info = QmlOnboardingStorage::Evaluate(MakeState());

    QCOMPARE(info.status, QStringLiteral("ok"));
    QCOMPARE(info.available_gb, 700);
    QCOMPARE(info.available_text, QStringLiteral("700GB available"));
    QCOMPARE(info.full_required_gb, 612);
    QCOMPARE(info.pruned_required_gb, 14);
    QCOMPARE(info.selected_required_gb, 612);
    QVERIFY(info.enough_for_selected);
    QVERIFY(info.enough_for_full);
    QVERIFY(info.warning_text.isEmpty());
}

void OnboardingStorageTests::prunedSelectionUsesPrunedRequirement()
{
    auto state = MakeState();
    state.available_bytes = GB(100);
    state.prune = true;

    const QmlOnboardingStorage::Info info = QmlOnboardingStorage::Evaluate(state);

    QCOMPARE(info.status, QStringLiteral("ok"));
    QCOMPARE(info.full_required_gb, 612);
    QCOMPARE(info.pruned_required_gb, 14);
    QCOMPARE(info.selected_required_gb, 14);
    QVERIFY(info.enough_for_selected);
    QVERIFY(!info.enough_for_full);
    QVERIFY(info.warning_text.isEmpty());
}

void OnboardingStorageTests::insufficientSelectedStorageWarns()
{
    auto state = MakeState();
    state.available_bytes = GB(10);
    state.prune = true;

    const QmlOnboardingStorage::Info info = QmlOnboardingStorage::Evaluate(state);

    QCOMPARE(info.status, QStringLiteral("warning"));
    QCOMPARE(info.available_gb, 10);
    QCOMPARE(info.selected_required_gb, 14);
    QVERIFY(!info.enough_for_selected);
    QVERIFY(!info.enough_for_full);
    QCOMPARE(info.warning_text, QStringLiteral("About 14GB is needed for the selected storage option."));
}

void OnboardingStorageTests::nearMinimumStorageWarns()
{
    auto state = MakeState();
    state.available_bytes = GB(20);
    state.prune = true;

    const QmlOnboardingStorage::Info info = QmlOnboardingStorage::Evaluate(state);

    QCOMPARE(info.status, QStringLiteral("warning"));
    QCOMPARE(info.available_gb, 20);
    QCOMPARE(info.selected_required_gb, 14);
    QVERIFY(info.enough_for_selected);
    QVERIFY(!info.enough_for_full);
    QCOMPARE(info.warning_text, QStringLiteral("About 14GB is needed, so this disk is close to the recommended minimum."));
}

void OnboardingStorageTests::pruneRecommendationUsesValidityAndMargin()
{
    auto state = MakeState();

    state.result_valid = false;
    QVERIFY(!QmlOnboardingStorage::ShouldRecommendPrune(state));

    state.result_valid = true;
    state.error_text = QStringLiteral("storage probe failed");
    QVERIFY(!QmlOnboardingStorage::ShouldRecommendPrune(state));

    state.error_text.clear();
    state.available_bytes = GB(621);
    QVERIFY(QmlOnboardingStorage::ShouldRecommendPrune(state));

    state.available_bytes = GB(622);
    QVERIFY(!QmlOnboardingStorage::ShouldRecommendPrune(state));
}

void OnboardingStorageTests::existingProfileDoesNotRecommendPruneOrRequireFreshChainSpace()
{
    auto state = MakeState();
    state.available_bytes = GB(5);
    state.existing_profile = true;

    const QmlOnboardingStorage::Info info = QmlOnboardingStorage::Evaluate(state);

    QCOMPARE(info.status, QStringLiteral("ok"));
    QCOMPARE(info.minimum_required_gb, 1);
    QCOMPARE(info.selected_required_gb, 1);
    QCOMPARE(info.full_required_gb, 612);
    QVERIFY(info.enough_for_selected);
    QVERIFY(info.enough_for_full);
    QVERIFY(info.warning_text.isEmpty());
    QVERIFY(!QmlOnboardingStorage::ShouldRecommendPrune(state));
}

void OnboardingStorageTests::existingProfileBelowOperationalMinimumWarns()
{
    auto state = MakeState();
    state.available_bytes = 0;
    state.existing_profile = true;

    const QmlOnboardingStorage::Info info = QmlOnboardingStorage::Evaluate(state);

    QCOMPARE(info.status, QStringLiteral("warning"));
    QCOMPARE(info.minimum_required_gb, 1);
    QCOMPARE(info.selected_required_gb, 1);
    QVERIFY(!info.enough_for_selected);
    QVERIFY(!info.enough_for_full);
    QCOMPARE(info.warning_text, QStringLiteral("About 1GB is needed for the selected storage option."));
    QVERIFY(!QmlOnboardingStorage::ShouldRecommendPrune(state));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <qml/test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(OnboardingStorageTests)
#else
QTEST_MAIN(OnboardingStorageTests)
#endif
#include "test_onboarding_storage.moc"
