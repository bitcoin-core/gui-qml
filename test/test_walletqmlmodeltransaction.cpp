// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <QLocale>
#include <QCoreApplication>
#include <QEvent>
#include <QPointer>
#include <QScopeGuard>
#include <QSignalSpy>

#include <qml/bitcoinamount.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/walletqmlmodeltransaction.h>
#include <qml/retainedvalue.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/admissionhookscheduler.h>

#include <consensus/amount.h>
#include <key_io.h>
#include <addresstype.h>
#include <stdexcept>

namespace {
struct FrozenReview {
    ManualExecutionEnvironment environment;
    const std::shared_ptr<AdmissionHookScheduler> scheduler{std::make_shared<AdmissionHookScheduler>(environment.scheduler)};
    std::shared_ptr<walletopening::Session> opening{std::make_shared<walletopening::Session>(scheduler, environment.scheduler, std::make_shared<std::atomic_bool>(false), [] {})};
    std::unique_ptr<WalletQmlModelTransaction> create(walletreview::Snapshot snapshot) {
        std::optional<walletreview::Snapshot> prepared;
        const auto accepted = execution::Submit(*environment.scheduler, [snapshot = std::move(snapshot)]() mutable {
            snapshot.presentation = walletreview::FormatRecipients(snapshot.recipients, snapshot.display_unit); return std::move(snapshot);
        }, [&prepared](auto value) { prepared = std::move(value); });
        if (!accepted) { QTest::qFail("Fixture preparation was rejected", __FILE__, __LINE__); return {}; }
        environment.drain();
        return std::make_unique<WalletQmlModelTransaction>(*prepared, opening, [this](auto callback) { environment.post(std::move(callback)); });
    }
    void settle() {
        for (int turn{0}; turn < 64; ++turn) {
            environment.drain();
            environment.publishAll();
            if (environment.turnsIdle() && environment.publicationsIdle()) return;
        }
        QTest::qFail("The original review presentation graph did not settle", __FILE__, __LINE__);
    }
    ~FrozenReview() { scheduler->disarm(); opening->closeAdmission(); settle(); }
};
walletreview::Snapshot AmountSnapshot(CAmount amount, int unit = 0) { walletreview::Snapshot value; value.amount = amount; value.display_unit = unit; return value; }
} // namespace

class WalletQmlModelTransactionTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void constructorRetentionSurvivesOriginalParentLoss()
    {
        FrozenReview f;
        struct Parent : QObject { walletreview::Snapshot snapshot; };
        auto parent = std::make_unique<Parent>();
        parent->snapshot.amount = 1000;
        CMutableTransaction tx; tx.vout.emplace_back(1000, CScript{});
        parent->snapshot.transaction = MakeTransactionRef(tx);
        const std::weak_ptr<const CTransaction> original = parent->snapshot.transaction;
        auto trigger = f.scheduler->armCleanup(1, [&] { parent.reset(); });
        const auto cleanup = qScopeGuard([&] { f.scheduler->disarm(); parent.reset(); f.settle(); });
        bool rejected{false};
        try {
            auto review = std::make_unique<WalletQmlModelTransaction>(parent->snapshot, f.opening,
                [&](auto callback) { f.environment.post(std::move(callback)); }, parent.get());
        } catch (const std::runtime_error& error) {
            rejected = true;
            QCOMPARE(QString::fromUtf8(error.what()), QStringLiteral("The original review owner is no longer available"));
        }
        QCOMPARE(trigger->calls.load(), size_t{1}); QVERIFY(trigger->originalId.load() != 0);
        QVERIFY(rejected); QVERIFY(!parent); QVERIFY(!original.expired());
        f.settle();
        QVERIFY(original.expired()); QCOMPARE(f.environment.scheduler->pending(), size_t{0});
    }
    void throwingPosterKeepsOriginalPresentation_data()
    {
        QTest::addColumn<bool>("enqueue");
        QTest::newRow("before-enqueue") << false; QTest::newRow("after-enqueue") << true;
    }
    void feeCloneDefersAdmissionUntilConstructionCompletes()
    {
        FrozenReview f; auto original = f.create(AmountSnapshot(1000));
        auto parent = std::make_unique<QObject>();
        const auto cleanup = qScopeGuard([&] {
            f.scheduler->disarm(); parent.reset(); original->closeAdmission(); original.reset(); f.settle();
        });
        original->setDisplayUnit(3);
        auto trigger = f.scheduler->armCleanup(1, [&] { parent.reset(); });
        QPointer<WalletQmlModelTransaction> clone = new WalletQmlModelTransaction(*original, 25, parent.get());
        QVERIFY(clone); QVERIFY(clone->presentationPending()); QCOMPARE(trigger->calls.load(), size_t{0});
        QCOMPARE(clone->getTransactionFee(), CAmount{25});
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(trigger->calls.load(), size_t{1}); QVERIFY(trigger->originalId.load() != 0);
        QVERIFY(!parent); QVERIFY(!clone);
        f.scheduler->disarm(); original->closeAdmission(); f.settle();
        QCOMPARE(f.environment.scheduler->pending(), size_t{0});
    }
    void throwingPosterKeepsOriginalPresentation()
    {
        QFETCH(bool, enqueue); FrozenReview f;
        auto retained = walletopening::RetainValues<walletreview::Snapshot>(f.opening, [] {
            auto snapshot = std::make_unique<walletreview::Snapshot>(); snapshot->amount = 1000; return snapshot;
        });
        QVERIFY(retained);
        std::deque<std::function<void()>> duplicates;
        auto review = std::make_unique<WalletQmlModelTransaction>(std::move(retained), f.opening,
            [&](auto callback) { if (enqueue) duplicates.push_back(std::move(callback)); throw std::runtime_error{"post failed"}; });
        const auto cleanup = qScopeGuard([&] { duplicates.clear(); if (review) review->closeAdmission(); review.reset(); f.settle(); });
        QSignalSpy changed{review.get(), &WalletQmlModelTransaction::reviewedRecipientsChanged};
        const auto trigger = f.scheduler->armSubmit(execution::Lane::Domain, 1, [] {});
        review->setDisplayUnit(3); f.environment.drain();
        QCOMPARE(trigger->calls.load(), size_t{1}); QVERIFY(trigger->originalId.load() != 0);
        QVERIFY(!f.environment.scheduler->phase(execution::TaskId{trigger->originalId.load()}));
        QVERIFY(f.environment.turnsIdle()); QVERIFY(review->presentationPending());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(!review->presentationPending()); QVERIFY(review->presentationError().isEmpty()); QCOMPARE(changed.count(), 1);
        while (!duplicates.empty()) { auto callback = std::move(duplicates.front()); duplicates.pop_front(); callback(); }
        QCOMPARE(changed.count(), 1); review->closeAdmission(); review.reset(); f.settle();
        QCOMPARE(f.environment.scheduler->pending(), size_t{0});
    }
    void unknownFeeIsDistinctFromKnownZero()
    {
        FrozenReview fixture; auto review = fixture.create(AmountSnapshot(1250));
        auto& transaction = *review;
        QVERIFY(!transaction.feeKnown()); QVERIFY(!transaction.reviewedFee());
        QCOMPARE(transaction.fee(), QString("Unknown")); QCOMPARE(transaction.total(), QString("Unknown"));
        QSignalSpy fee_changed{&transaction, &WalletQmlModelTransaction::feeChanged};
        QSignalSpy total_changed{&transaction, &WalletQmlModelTransaction::totalChanged};
        transaction.setTransactionFee(0);
        QVERIFY(transaction.feeKnown()); QCOMPARE(transaction.reviewedFee(), std::optional<CAmount>{0});
        QVERIFY(transaction.fee() != QString("Unknown")); QVERIFY(transaction.total() != QString("Unknown"));
        QCOMPARE(fee_changed.count(), 1); QCOMPARE(total_changed.count(), 1);
        transaction.setTransactionFee(0);
        QCOMPARE(fee_changed.count(), 1); QCOMPARE(total_changed.count(), 1);
    }

    void manualDisplayUnitProjectionRejectsStalePublicationAndQuietClose();
    void presentationAdmissionKeepsOriginalReview_data();
    void presentationAdmissionKeepsOriginalReview();
    void singleRecipientReviewAmountsInheritRecipientUnit();
    void multipleRecipientReviewAmountsInheritSharedUnit();
    void recipientRolesExposeFormattedAddressAndUnitLabel();
    void reviewDisplayStringsUseLocale();
    void removingRecipientUpdatesTotalOnce();
    void txidTracksAssignedTransaction();
    void reviewedRecipientsStayBoundToPreparedOutputs();
};

void WalletQmlModelTransactionTests::presentationAdmissionKeepsOriginalReview_data()
{
    QTest::addColumn<bool>("domain");
    QTest::addColumn<int>("action");
    for (const bool domain : {false, true}) {
        for (int action{0}; action < 3; ++action) {
            const char* outcome = action == 0 ? "close" : action == 1 ? "delete" : "new-unit";
            const auto tag = QStringLiteral("%1-%2").arg(domain ? "Domain" : "Cleanup").arg(outcome).toUtf8();
            QTest::newRow(tag.constData()) << domain << action;
        }
    }
}

void WalletQmlModelTransactionTests::presentationAdmissionKeepsOriginalReview()
{
    QFETCH(bool, domain);
    QFETCH(int, action);
    FrozenReview f;
    auto snapshot = AmountSnapshot(1000);
    snapshot.recipients.append({QStringLiteral("original address"), QStringLiteral("original note"), 1000, false});
    CMutableTransaction transaction;
    transaction.vout.emplace_back(1000, CScript{});
    snapshot.transaction = MakeTransactionRef(std::move(transaction));
    const std::weak_ptr<const CTransaction> original_transaction{snapshot.transaction};
    auto review = f.create(std::move(snapshot));
    QVERIFY(review);
    const QPointer<WalletQmlModelTransaction> original{review.get()};
    QSignalSpy changes{review.get(), &WalletQmlModelTransaction::presentationChanged};
    QSignalSpy recipients{review.get(), &WalletQmlModelTransaction::reviewedRecipientsChanged};
    int changes_after{-1}, recipients_after{-1};
    const auto disarm = qScopeGuard([&] { f.scheduler->disarm(); });
    auto change = [&] {
        if (action == 0) review->closeAdmission();
        else if (action == 1) review.reset();
        else review->setDisplayUnit(0);
        changes_after = changes.count();
        recipients_after = recipients.count();
    };
    const auto trigger = domain ? f.scheduler->armSubmit(execution::Lane::Domain, 1, change)
                                : f.scheduler->armCleanup(1, change);
    review->setDisplayUnit(3);
    QCOMPARE(trigger->calls.load(), size_t{1});
    QVERIFY(trigger->originalId.load() != 0);
    const execution::TaskId admitted{trigger->originalId.load()};
    QVERIFY(f.environment.scheduler->phase(admitted));
    QCOMPARE(changes.count(), changes_after);
    QCOMPARE(recipients.count(), recipients_after);
    QVERIFY(!original_transaction.expired()); // Native snapshot still owns Core data.
    f.settle();
    if (action == 2) {
        QVERIFY(original);
        QVERIFY(!review->presentationPending());
        QVERIFY(review->presentationError().isEmpty());
        QCOMPARE(review->amountAmount()->unit(), BitcoinAmount::Unit::BTC);
        QCOMPARE(review->amountAmount()->satoshi(), qint64{1000});
        QCOMPARE(review->reviewedRecipients().size(), qsizetype{1});
        const auto row = review->reviewedRecipients().first().toMap();
        QCOMPARE(row.value("address").toString(), QStringLiteral("original address"));
        QCOMPARE(row.value("label").toString(), QStringLiteral("original note"));
        QCOMPARE(row.value("amountSat").toLongLong(), qint64{1000});
        QCOMPARE(row.value("amount").toString(), QStringLiteral("0.00001000 BTC"));
    } else {
        QCOMPARE(bool(original), action == 0);
        QCOMPARE(changes.count(), changes_after);
        QCOMPARE(recipients.count(), recipients_after);
        if (original) {
            QVERIFY(!review->presentationPending());
            QVERIFY(review->reviewedRecipients().isEmpty());
        }
    }
    if (review) review->closeAdmission();
    review.reset();
    f.settle();
    QVERIFY(!f.environment.scheduler->phase(admitted));
    QVERIFY(original_transaction.expired());
    QCOMPARE(f.environment.scheduler->pending(), size_t{0});
    QVERIFY(f.environment.quiescent());
}

void WalletQmlModelTransactionTests::manualDisplayUnitProjectionRejectsStalePublicationAndQuietClose()
{
    FrozenReview fixture; auto snapshot = AmountSnapshot(1000); snapshot.recipients.append({QStringLiteral("address"), QStringLiteral("frozen note"), 1000, false});
    auto review = fixture.create(snapshot);
    review->setDisplayUnit(3); QVERIFY(review->presentationPending()); QCOMPARE(review->amountAmount()->unit(), BitcoinAmount::Unit::BTC);
    fixture.environment.drain(); QCOMPARE(review->amountAmount()->unit(), BitcoinAmount::Unit::BTC); // Outcome is held before GUI delivery.
    review->setDisplayUnit(0); fixture.environment.publishAll(); QCOMPARE(review->amountAmount()->unit(), BitcoinAmount::Unit::BTC);
    fixture.settle(); QVERIFY(!review->presentationPending()); QVERIFY(review->presentationError().isEmpty());
    QCOMPARE(review->reviewedRecipients().first().toMap().value("label").toString(), QStringLiteral("frozen note"));
    review->setDisplayUnit(3); fixture.environment.drain();
    QSignalSpy quiet(review.get(), &WalletQmlModelTransaction::reviewedRecipientsChanged);
    review->closeAdmission(); fixture.environment.publishAll();
    QCOMPARE(quiet.size(), 0); QVERIFY(!review->presentationPending()); QVERIFY(review->reviewedRecipients().isEmpty());
}

void WalletQmlModelTransactionTests::singleRecipientReviewAmountsInheritRecipientUnit()
{
    SendRecipientsListModel recipients;
    auto* recipient = recipients.currentRecipient();
    recipient->amount()->setUnit(BitcoinAmount::Unit::SAT);
    recipient->amount()->setSatoshi(1250);

    FrozenReview fixture; auto review = fixture.create(AmountSnapshot(1250, int(recipient->amount()->unit())));
    auto& transaction = *review;
    transaction.setTransactionFee(50);

    QCOMPARE(transaction.feeAmount()->unit(), BitcoinAmount::Unit::SAT);
    QCOMPARE(transaction.totalAmount()->unit(), BitcoinAmount::Unit::SAT);
    QCOMPARE(transaction.feeAmount()->toDisplay(), QString("50"));
    QCOMPARE(transaction.totalAmount()->toDisplay(), QString("1300"));
}

void WalletQmlModelTransactionTests::multipleRecipientReviewAmountsInheritSharedUnit()
{
    SendRecipientsListModel recipients;
    auto* first = recipients.currentRecipient();
    first->amount()->setUnit(BitcoinAmount::Unit::BTC);
    first->amount()->setSatoshi(COIN / 2);

    recipients.add();
    auto* second = recipients.currentRecipient();
    second->amount()->setUnit(BitcoinAmount::Unit::SAT);
    second->amount()->setSatoshi(2000);

    FrozenReview fixture; auto review = fixture.create(AmountSnapshot(COIN / 2 + 2000, int(first->amount()->unit())));
    auto& transaction = *review;
    transaction.setTransactionFee(1000);

    QCOMPARE(first->amount()->unit(), BitcoinAmount::Unit::SAT);
    QCOMPARE(transaction.feeAmount()->unit(), BitcoinAmount::Unit::SAT);
    QCOMPARE(transaction.totalAmount()->unit(), BitcoinAmount::Unit::SAT);
    QCOMPARE(transaction.feeAmount()->toDisplay(), QString("1000"));
    QCOMPARE(transaction.totalAmount()->toDisplay(), QString("50003000"));
}

void WalletQmlModelTransactionTests::recipientRolesExposeFormattedAddressAndUnitLabel()
{
    SendRecipientsListModel recipients;
    auto* recipient = recipients.currentRecipient();
    recipient->address()->setAddress("abcd1234efgh5678", 0);
    recipient->amount()->setUnit(BitcoinAmount::Unit::SAT);
    recipient->amount()->setSatoshi(42);

    const QModelIndex index = recipients.index(0, 0);
    QCOMPARE(
        recipients.data(index, SendRecipientsListModel::FormattedAddressRole).toString(),
        QString("abcd 1234 efgh 5678"));
    QCOMPARE(
        recipients.data(index, SendRecipientsListModel::AmountUnitLabelRole).toString(),
        QString("sats"));
}

void WalletQmlModelTransactionTests::reviewDisplayStringsUseLocale()
{
    const QLocale previous;
    const auto restore_locale = qScopeGuard([previous] { QLocale::setDefault(previous); });
    QLocale::setDefault(QLocale{"de_DE"});

    FrozenReview fixture; auto review = fixture.create(AmountSnapshot(123'456'789));
    auto& transaction = *review;
    transaction.setTransactionFee(1'000);

    QCOMPARE(transaction.amount(), QStringLiteral("1,23456789 BTC"));
    QCOMPARE(transaction.fee(), QStringLiteral("0,00001000 BTC"));
    QCOMPARE(transaction.total(), QStringLiteral("1,23457789 BTC"));
}

void WalletQmlModelTransactionTests::removingRecipientUpdatesTotalOnce()
{
    FrozenReview fixture;
    auto recipients = std::make_unique<SendRecipientsListModel>();
    const auto settle = [&] {
        fixture.settle();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        fixture.settle();
    };
    const auto cleanup = qScopeGuard([&] {
        if (recipients) recipients->closeAdmission();
        recipients.reset(); settle();
        if (!fixture.environment.quiescent())
            QTest::qFail("Recipient removal did not physically retire its original owners", __FILE__, __LINE__);
    });
    recipients->setOpening({}, {fixture.scheduler, [&](auto callback) { fixture.environment.post(std::move(callback)); }});
    settle();
    recipients->currentRecipient()->amount()->setSatoshi(1000); settle();
    QCOMPARE(recipients->totalAmountSatoshi(), 1000);
    recipients->add(); settle();
    recipients->currentRecipient()->amount()->setSatoshi(2000); settle();
    QCOMPARE(recipients->totalAmountSatoshi(), 3000);

    QSignalSpy total_changed_spy{recipients.get(), &SendRecipientsListModel::totalAmountChanged};
    recipients->remove();
    // Removal admits the original aggregate work; it cannot calculate or
    // publish that Domain result on the calling GUI turn.
    QCOMPARE(recipients->totalAmountSatoshi(), 3000);
    QCOMPARE(total_changed_spy.count(), 0);
    settle();
    QCOMPARE(recipients->totalAmountSatoshi(), 1000);
    QCOMPARE(total_changed_spy.count(), 1);
    settle();
    QCOMPARE(total_changed_spy.count(), 1);
    recipients->closeAdmission(); recipients.reset(); settle();
    QVERIFY(fixture.environment.quiescent());
}

void WalletQmlModelTransactionTests::txidTracksAssignedTransaction()
{
    FrozenReview fixture; auto review = fixture.create(AmountSnapshot(0));
    auto& transaction = *review;
    QVERIFY(transaction.txid().isEmpty());

    CMutableTransaction mutable_transaction;
    mutable_transaction.vout.emplace_back(1'000, CScript{});
    const auto original = MakeTransactionRef(std::move(mutable_transaction));
    const auto retained = walletopening::RetainValues<CTransactionRef>(fixture.opening, [&] { return std::make_unique<CTransactionRef>(original); });
    const CTransactionRef wallet_transaction{retained, original.get()};
    QSignalSpy txid_changed_spy{&transaction, &WalletQmlModelTransaction::txidChanged};

    transaction.setWtx(wallet_transaction);
    QCOMPARE(transaction.txid(), QString::fromStdString(wallet_transaction->GetHash().ToString()));
    QCOMPARE(txid_changed_spy.count(), 1);

    transaction.setWtx(wallet_transaction);
    QCOMPARE(txid_changed_spy.count(), 1);
}

void WalletQmlModelTransactionTests::reviewedRecipientsStayBoundToPreparedOutputs()
{
    const QString address{QStringLiteral("1BoatSLRHtKNngkdXEeobR76b53LETtpyT")};
    const QString edited_address{QStringLiteral("1BitcoinEaterAddressDontSendf59kuE")};
    SendRecipientsListModel recipients;
    auto* recipient = recipients.currentRecipient();
    recipient->setAddress(address);
    recipient->setLabel(QStringLiteral("Original note"));
    recipient->amount()->setSatoshi(1'000);

    CMutableTransaction prepared;
    prepared.vout.emplace_back(900, GetScriptForDestination(DecodeDestination(address.toStdString())));
    FrozenReview fixture;
    auto frozen = AmountSnapshot(900); frozen.transaction = MakeTransactionRef(std::move(prepared));
    frozen.recipients.append({address, QStringLiteral("Original note"), 900, false}); frozen.destinations.insert(address); frozen.notes.insert(address, QStringLiteral("Original note"));
    auto review = fixture.create(std::move(frozen)); auto& transaction = *review;

    recipient->setAddress(edited_address);
    recipient->setLabel(QStringLiteral("Edited note"));
    recipient->amount()->setSatoshi(1'500);

    const QVariantMap snapshot{transaction.reviewedRecipients().at(0).toMap()};
    QCOMPARE(snapshot.value(QStringLiteral("address")).toString(), address);
    QCOMPARE(snapshot.value(QStringLiteral("label")).toString(), QStringLiteral("Original note"));
    QCOMPARE(snapshot.value(QStringLiteral("amount")).toString(), QStringLiteral("0.00000900 BTC"));
    QVERIFY(transaction.isReviewedRecipient(address));
    QVERIFY(!transaction.isReviewedRecipient(edited_address));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(WalletQmlModelTransactionTests)
#else
QTEST_MAIN(WalletQmlModelTransactionTests)
#endif
#include <test_walletqmlmodeltransaction.moc>
