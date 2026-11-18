// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <test/qt_test_registry.h>
#include <qml/settingssession.h>
#include <QtTest/QtTest>
namespace {
using namespace effectivesettings;
Snapshot Original() {
    Snapshot result;result.ready=true;result.revision="original";
    for (size_t i{0}; i < settingsedit::FIELDS; ++i) {
        const auto key = static_cast<Key>(i);
        const auto kind = Type(key);
        switch (kind) {
        case Kind::Boolean:
            result.fields.push_back({key, kind, true, true, true});
            break;
        case Kind::Integer:
            result.fields.push_back({key, kind, int64_t{20}, int64_t{20}, int64_t{20}});
            break;
        case Kind::Text:
            result.fields.push_back({key, kind, QStringLiteral("original"),
                QStringLiteral("original"), QStringLiteral("original")});
            break;
        }
    }
    return result;
}
settingsedit::Session Editor(Snapshot snapshot=Original()) {
    settingsedit::Reducer::Mask mask;mask.set();settingsedit::Session result{{"editor","settings"},mask};
    if(!result.observe(std::move(snapshot)))qFatal("Original settings fixture rejected");
    return result;
}
Result Saved(const settingsedit::Save& save) {
    Result result;result.outcome=Outcome::Applied;result.write_completed=true;result.persisted=true;result.runtime_confirmed=true;
    for(const auto& edit:save.batch.edits)result.persisted_values[edit.key]=edit.intent==Intent::ExplicitOverride?std::make_optional(edit.value):std::nullopt;
    return result;
}
}
class SettingsSessionTests:public QObject {
    Q_OBJECT
private Q_SLOTS:
    void disabledDraftRetainsTheExactPruneTarget_data(){QTest::addColumn<qint64>("target");QTest::newRow("minimum-550")<<qint64{550};QTest::newRow("non-round-target")<<qint64{1000};QTest::newRow("manual-one")<<qint64{1};}
    void disabledDraftRetainsTheExactPruneTarget(){QFETCH(qint64,target);auto snapshot=Original();snapshot.fields[3].committed=int64_t{target};snapshot.fields[3].effective=int64_t{target};snapshot.fields[3].has_override=true;auto e=Editor(snapshot);QVERIFY(e.useValue(Key::Prune,int64_t{0}));QVERIFY(e.editor().draft()[3].remembered);QCOMPARE(std::get<int64_t>(*e.editor().draft()[3].remembered),target);QVERIFY(e.useValue(Key::Prune,int64_t{target}));QVERIFY(!e.editor().dirty());}
    void equalNumericValueStillCreatesExplicitOverrideIntent(){auto e=Editor();QVERIFY(e.editText(Key::Mempool,"020"));QVERIFY(e.canSave());const auto save=e.check();QVERIFY(save);QCOMPARE(save->batch.edits.size(),size_t{1});QCOMPARE(std::get<int64_t>(save->batch.edits.front().value),20);QCOMPARE(save->batch.edits.front().intent,Intent::ExplicitOverride);}
    void alreadyCommittedValueDoesNotInventAnotherWrite(){auto snapshot=Original();snapshot.fields[8].has_override=true;auto e=Editor(snapshot);QVERIFY(e.editText(Key::Mempool,"020"));QVERIFY(!e.editor().dirty());QVERIFY(!e.check());QCOMPARE(e.literal(Key::Mempool),QStringLiteral("020"));}
    void invalidRawIntegerIsRetainedWithoutAWrite(){auto e=Editor();QVERIFY(e.editText(Key::Mempool,"20x"));QCOMPARE(e.literal(Key::Mempool),QStringLiteral("20x"));QVERIFY(!e.valid());QVERIFY(!e.check());QVERIFY(!e.fieldError(Key::Mempool).isEmpty());}
    void resetIsDifferentFromWritingTheSameNumericDefault(){auto snapshot=Original();snapshot.fields[8].has_override=true;auto e=Editor(snapshot);QVERIFY(e.useConfiguration(Key::Mempool));const auto save=e.check();QVERIFY(save);QCOMPARE(save->batch.edits.front().intent,Intent::RemoveOverride);}
    void editDuringCheckingInvalidatesTheOldValidation(){auto e=Editor();QVERIFY(e.editText(Key::Mempool,"30"));const auto save=e.check();QVERIFY(save);QVERIFY(e.editText(Key::Mempool,"40"));QVERIFY(!e.validated(save->attempt.token,true));QCOMPARE(e.editor().phase(),persistentedit::Phase::Editing);QCOMPARE(e.literal(Key::Mempool),QStringLiteral("40"));QVERIFY(e.canSave());}
    void anyNewConfigurationFenceRequiresFreshValidation(){auto e=Editor();QVERIFY(e.editText(Key::Mempool,"30"));const auto save=e.check();QVERIFY(save);auto newer=Original();newer.revision="new-config";QVERIFY(e.observe(newer));QVERIFY(!e.validated(save->attempt.token,true));QVERIFY(e.canSave());}
    void successorEditSurvivesTheOriginalReceipt(){auto e=Editor();QVERIFY(e.editText(Key::Mempool,"30"));const auto save=e.check();QVERIFY(save);QVERIFY(e.validated(save->attempt.token,true));QVERIFY(e.editText(Key::Mempool,"40"));QVERIFY(e.finished(Saved(*save),false));QCOMPARE(e.literal(Key::Mempool),QStringLiteral("40"));QVERIFY(e.editor().dirty());QCOMPARE(std::get<int64_t>(e.editor().base()[8].value),30);}
    void aReadHeldBeforeWritingCannotOverwriteTheWriteReceipt(){auto e=Editor();QVERIFY(e.editText(Key::Mempool,"30"));const auto save=e.check();QVERIFY(save);QVERIFY(e.validated(save->attempt.token,true));QVERIFY(!e.observe(Original()));QVERIFY(e.finished(Saved(*save),false));QVERIFY(!e.editor().dirty());QCOMPARE(std::get<int64_t>(e.editor().base()[8].value),30);QVERIFY(e.needsRefresh());}
    void unknownOutcomeCannotBeResolvedByMatchingReadValues(){auto e=Editor();QVERIFY(e.editText(Key::Mempool,"30"));const auto save=e.check();QVERIFY(save);QVERIFY(e.validated(save->attempt.token,true));Result unknown;unknown.outcome=Outcome::Uncertain;QVERIFY(e.finished(unknown,false));auto read=Original();read.fields[8].committed=int64_t{30};read.fields[8].has_override=true;QVERIFY(!e.observe(read));QCOMPARE(e.editor().phase(),persistentedit::Phase::Uncertain);QVERIFY(e.active());QVERIFY(!e.canSave());}
    void retainedQueryDoesNotReuseItsOldReadback(){auto e=Editor();QVERIFY(e.editText(Key::Mempool,"30"));const auto save=e.check();QVERIFY(save);QVERIFY(e.validated(save->attempt.token,true));Result unknown;unknown.outcome=Outcome::Uncertain;QVERIFY(e.finished(unknown,false));auto result=Saved(*save);result.snapshot=Original();QVERIFY(e.reconciled(result));QCOMPARE(std::get<int64_t>(e.editor().base()[8].value),30);QVERIFY(e.needsRefresh());}
    void knownPersistenceAndRuntimeFailureRemainSeparate(){auto e=Editor();QVERIFY(e.editText(Key::Mempool,"30"));const auto save=e.check();QVERIFY(save);QVERIFY(e.validated(save->attempt.token,true));auto result=Saved(*save);result.outcome=Outcome::Uncertain;result.runtime_confirmed=false;result.error="Runtime failed";QVERIFY(e.finished(result,false));QVERIFY(e.result().persisted);QVERIFY(e.result().known);QCOMPARE(e.editor().result()->outcome,persistentedit::Outcome::Partial);QVERIFY(!e.editor().dirty());QCOMPARE(e.error(),QStringLiteral("Runtime failed"));}
    void missingNativeSnapshotPreservesDirtyIntent(){auto e=Editor();QVERIFY(e.editText(Key::Mempool,"30"));Snapshot failed;failed.error="read failed";QVERIFY(!e.observe(failed));QVERIFY(!e.fresh());QVERIFY(e.editor().dirty());QCOMPARE(e.literal(Key::Mempool),QStringLiteral("30"));QVERIFY(!e.canSave());}
    void parentRetirementStillAccountsForAnAdmittedWrite(){auto e=Editor();QVERIFY(e.editText(Key::Mempool,"30"));const auto save=e.check();QVERIFY(save);QVERIFY(e.validated(save->attempt.token,true));e.retire();QCOMPARE(e.editor().phase(),persistentedit::Phase::Retiring);QVERIFY(e.finished(Saved(*save),false));QCOMPARE(e.editor().phase(),persistentedit::Phase::Closed);QVERIFY(e.result().persisted);QVERIFY(!e.active());}
    void closeConsentIsBoundToTheOriginalEdit(){auto e=Editor();QVERIFY(e.editText(Key::Mempool,"30"));QVERIFY(!e.requestClose());const auto token=e.editor().closeToken();QVERIFY(e.editText(Key::Mempool,"40"));QVERIFY(!e.answerClose(token,persistentedit::CloseChoice::Discard));QVERIFY(e.editor().dirty());QVERIFY(e.answerClose(e.editor().closeToken(),persistentedit::CloseChoice::KeepEditing));QVERIFY(e.canSave());}
    void oversizedInputIsInvalidRatherThanAPersistablePrefix(){auto e=Editor();QVERIFY(e.editText(Key::Signer,QString(40000,'x')));QCOMPARE(e.literal(Key::Signer).size(),32769);QVERIFY(!e.valid());QVERIFY(!e.check());QVERIFY(!e.fieldError(Key::Signer).isEmpty());}
};
BITCOINQML_REGISTER_QT_TEST(SettingsSessionTests)
#include <test_settingssession.moc>
