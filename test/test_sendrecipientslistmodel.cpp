// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/models/sendrecipient.h>
#include <qml/backendexecutor.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <test/mocks/admissionhookscheduler.h>
#include <test/mocks/manualexecutionenvironment.h>

#include <QCoreApplication>
#include <QEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QUrl>
#include <QScopeGuard>

#include <memory>

class SendRecipientsListModelTests : public QObject
{
    Q_OBJECT
    std::unique_ptr<BackendExecutor> m_executor;
    void bind(SendRecipientsListModel& model) { model.setOpening({}, {m_executor->scheduler(), {}}); }
private Q_SLOTS:
    void recipientBudgetIsEnforcedWithoutAQmlButton()
    {
        SendRecipientsListModel model;
        for (int row{0}; row < 31; ++row) model.add();
        QCOMPARE(model.count(), 25);
    }
    void childCleanupAdmissionCannotRetargetListIntent_data()
    {
        QTest::addColumn<bool>("initializing"); QTest::addColumn<int>("outcome");
        for (bool initializing : {false, true}) for (int outcome : {0, 1, 2})
            QTest::newRow(qPrintable((initializing ? QStringLiteral("initial-chunk-") : QStringLiteral("add-")) + QString::number(outcome))) << initializing << outcome;
    }
    void childCleanupAdmissionCannotRetargetListIntent()
    {
        QFETCH(bool, initializing); QFETCH(int, outcome);
        ManualRoutedExecution env;
        auto scheduler = std::make_shared<AdmissionHookScheduler>(env.scheduler);
        std::deque<std::function<void()>> posts;
        auto model = std::make_unique<SendRecipientsListModel>(nullptr, senddraft::Limits{initializing ? 128 : 25});
        const auto settle = [&] {
            for (int turn{0}; turn < 1024 && (turn == 0 || !env.queuedIdle() || !posts.empty()); ++turn) {
                env.drain(); while (!posts.empty()) { auto work = std::move(posts.front()); posts.pop_front(); work(); }
                QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
        };
        const auto cleanup = qScopeGuard([&] { scheduler->disarm(); if (model) model->close(); model.reset(); settle(); });
        if (initializing) {
            // More than one bounded initialization turn. Reentrant clear
            // replaces the original inventory while its first chunk is open.
            for (int row{1}; row < 65; ++row) model->add();
            QCOMPARE(model->count(), 65);
        } else {
            model->setOpening({}, {scheduler, [&](auto work) { posts.push_back(std::move(work)); }}); settle();
            model->currentRecipient()->amount()->setUnit(BitcoinAmount::Unit::MilliBTC); settle();
        }
        QPointer<SendRecipient> successor;
        auto trigger = scheduler->armCleanup(initializing ? 4 : 1, [&] {
            if (outcome == 0) model->close();
            else if (outcome == 1) model.reset();
            else {
                model->clear(); successor = model->currentRecipient();
                successor->setLabel(QStringLiteral("original clear successor"));
                successor->amount()->setUnit(BitcoinAmount::Unit::MicroBTC);
            }
        });
        if (initializing) model->setOpening({}, {scheduler, [&](auto work) { posts.push_back(std::move(work)); }});
        else model->add();
        QCOMPARE(trigger->calls.load(), size_t{1}); QVERIFY(trigger->originalId.load() != 0);
        settle();
        if (outcome == 2) {
            QVERIFY(model && successor); QCOMPARE(model->count(), 1); QCOMPARE(model->currentRecipient(), successor.data());
            QCOMPARE(successor->label(), QStringLiteral("original clear successor"));
            QCOMPARE(successor->amount()->unit(), BitcoinAmount::Unit::MicroBTC); QVERIFY(!model->validationPending());
        } else if (outcome == 0) { QVERIFY(model); QCOMPARE(model->count(), 0); }
        else QVERIFY(!model);
        scheduler->disarm(); if (model) model->close(); model.reset(); settle();
        QCOMPARE(env.scheduler->pending(), size_t{0});
    }
    void cleanupAdmissionKeepsOriginalSetup_data()
    {
        QTest::addColumn<int>("reservation"); QTest::addColumn<int>("outcome");
        for (int reservation : {1, 2, 3}) for (int outcome : {0, 1, 2})
            QTest::newRow(qPrintable(QString::number(reservation) + QLatin1Char('-') + QString::number(outcome))) << reservation << outcome;
    }
    void cleanupAdmissionKeepsOriginalSetup()
    {
        QFETCH(int, reservation); QFETCH(int, outcome);
        ManualRoutedExecution env;
        auto scheduler = std::make_shared<AdmissionHookScheduler>(env.scheduler);
        std::deque<std::function<void()>> old_posts, current_posts;
        size_t old_posted{0}, current_posted{0};
        auto model = std::make_unique<SendRecipientsListModel>();
        const QPointer<SendRecipient> original{model->currentRecipient()};
        const auto publish = [&] {
            for (auto* queue : {&old_posts, &current_posts}) while (!queue->empty()) {
                auto turn = std::move(queue->front()); queue->pop_front(); turn();
            }
        };
        const auto settle = [&] {
            for (int turn = 0; turn < 512 && (turn == 0 || !env.queuedIdle() || !old_posts.empty() || !current_posts.empty()); ++turn) {
                env.drain(); publish(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
        };
        const auto cleanup = qScopeGuard([&] { scheduler->disarm(); if (model) model->close(); model.reset(); settle(); });
        auto trigger = scheduler->armCleanup(reservation, [&] {
            if (outcome == 0) model->close();
            else if (outcome == 1) model.reset();
            else model->setOpening({}, {scheduler, [&](auto turn) { ++current_posted; current_posts.push_back(std::move(turn)); }});
        });
        model->setOpening({}, {scheduler, [&](auto turn) { ++old_posted; old_posts.push_back(std::move(turn)); }});
        QCOMPARE(trigger->calls.load(), size_t{1}); QVERIFY(trigger->originalId.load() != 0);
        if (outcome == 2) {
            QVERIFY(model); QCOMPARE(model->currentRecipient(), original.data());
            model->currentRecipient()->setLabel(QStringLiteral("successor"));
            settle(); QCOMPARE(old_posted, size_t{0}); QVERIFY(current_posted > 0);
            QCOMPARE(model->currentRecipient()->label(), QStringLiteral("successor"));
            QVERIFY(!model->validationPending());
        } else if (outcome == 0) { QVERIFY(model); QCOMPARE(model->count(), 0); }
        else QVERIFY(!model);
        scheduler->disarm(); if (model) model->close(); model.reset(); settle();
        QCOMPARE(env.scheduler->pending(), size_t{0});
    }
    void clearCleanupAdmissionKeepsNewerIntent_data()
    {
        QTest::addColumn<int>("reservation"); QTest::addColumn<int>("outcome");
        for (int reservation : {1, 2}) for (int outcome : {0, 1, 2})
            QTest::newRow(qPrintable(QString::number(reservation) + QLatin1Char('-') + QString::number(outcome))) << reservation << outcome;
    }
    void clearCleanupAdmissionKeepsNewerIntent()
    {
        QFETCH(int, reservation); QFETCH(int, outcome);
        ManualRoutedExecution env;
        auto scheduler = std::make_shared<AdmissionHookScheduler>(env.scheduler);
        std::deque<std::function<void()>> posts;
        auto model = std::make_unique<SendRecipientsListModel>();
        const auto settle = [&] {
            for (int turn = 0; turn < 512 && (turn == 0 || !env.queuedIdle() || !posts.empty()); ++turn) {
                env.drain(); while (!posts.empty()) { auto work = std::move(posts.front()); posts.pop_front(); work(); }
                QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            }
        };
        const auto cleanup = qScopeGuard([&] { scheduler->disarm(); if (model) model->close(); model.reset(); settle(); });
        model->setOpening({}, {scheduler, [&](auto turn) { posts.push_back(std::move(turn)); }}); settle();
        const QPointer<SendRecipient> original{model->currentRecipient()};
        const auto original_identity = original->identity();
        QPointer<SendRecipient> successor;
        auto trigger = scheduler->armCleanup(reservation, [&] {
            if (outcome == 0) model->close();
            else if (outcome == 1) model.reset();
            else {
                model->clear(); successor = model->currentRecipient();
                successor->setLabel(QStringLiteral("new clear intent"));
            }
        });
        model->clear();
        QCOMPARE(trigger->calls.load(), size_t{1}); QVERIFY(trigger->originalId.load() != 0);
        settle();
        if (outcome == 2) {
            QVERIFY(model && successor); QVERIFY(successor->identity() != original_identity);
            QCOMPARE(model->currentRecipient(), successor.data());
            QCOMPARE(successor->label(), QStringLiteral("new clear intent")); QCOMPARE(model->count(), 1);
        } else if (outcome == 0) { QVERIFY(model); QCOMPARE(model->count(), 0); }
        else QVERIFY(!model);
        scheduler->disarm(); if (model) model->close(); model.reset(); settle();
        QCOMPARE(env.scheduler->pending(), size_t{0});
    }
    void initTestCase() { m_executor = std::make_unique<BackendExecutor>(); }
    void cleanupTestCase() { m_executor->shutdown(); QTRY_VERIFY(m_executor->isDrained()); m_executor.reset(); }
    void invalidDraftAndAddressHaveIndependentAdmissionReasons()
    {
        SendRecipientsListModel recipients; bind(recipients);
        auto* recipient = recipients.currentRecipient();
        recipient->setAddress("malformed-address");
        recipient->amount()->setSatoshi(2'000'000);
        QTRY_VERIFY(!recipient->validationPending());
        QTRY_VERIFY(!recipients.validationPending());
        QVERIFY(!recipient->addressError().isEmpty());
        QVERIFY(recipient->amount()->inputAcceptable());
        QVERIFY(!recipient->isValid());
        QVERIFY(!recipients.allValid());
        QVERIFY(recipients.validationError().isEmpty()); // Existing single-address presentation.

        const auto address_error = recipient->addressError();
        recipient->amount()->editInput("1.");
        QVERIFY(!recipient->amount()->inputAcceptable());
        QCOMPARE(recipient->addressError(), address_error);
        QCOMPARE(recipient->amount()->satoshi(), qint64{2'000'000});
        QVERIFY(!recipient->amountError().isEmpty());
        QTRY_COMPARE(recipients.validationError(), recipient->amountError());
        QVERIFY(!recipient->isValid());

        recipient->amount()->editInput("0.02");
        QTRY_VERIFY(!recipient->validationPending());
        QTRY_VERIFY(!recipients.validationPending());
        QVERIFY(recipient->amount()->inputAcceptable());
        QCOMPARE(recipient->addressError(), address_error);
        QVERIFY(recipient->amountError().isEmpty());
        QVERIFY(!recipient->isValid());
        QVERIFY(recipients.validationError().isEmpty());
        recipients.add();
        QTRY_VERIFY(!recipients.validationError().isEmpty());
    }
    void clearKeepsCurrentRecipientValidForQmlBindings();
    void clearToFrontReportsRemovedRows();
    void clearToFrontDetachesRecipientsBeforeDestroyingThem();
    void unitChangesApplyToEveryRecipient();
    void paymentRequestMetadataStaysSeparateFromPrivateNote() {
        SendRecipientsListModel recipients; bind(recipients);
        auto* first = recipients.currentRecipient();
        first->setLabel("Private note");
        first->applyPaymentRequest("address-one", "Alice", "Lunch");
        QCOMPARE(first->label(), QString("Private note"));
        QCOMPARE(first->paymentRequestLabel(), QString("Alice"));
        QCOMPARE(first->message(), QString("Lunch"));
        QVERIFY(first->hasPaymentRequest());
        recipients.add();
        auto* second = recipients.currentRecipient();
        second->applyPaymentRequest("address-two", "Bob", "Coffee");
        recipients.setCurrentIndex(0);
        QCOMPARE(recipients.currentRecipient(), first);
        QCOMPARE(first->message(), QString("Lunch"));
        first->address()->setAddress("edited-address");
        // Provenance is retired by the Domain validation of the settled editor.
        QTRY_VERIFY(!first->validationPending());
        QTRY_VERIFY(!first->hasPaymentRequest());
        QVERIFY(first->message().isEmpty());
        QCOMPARE(first->label(), QString("Private note"));
        recipients.removeAt(0);
        QCOMPARE(recipients.currentRecipient(), second);
        QCOMPARE(second->paymentRequestLabel(), QString("Bob"));
        QCOMPARE(recipients.data(recipients.index(0), SendRecipientsListModel::RecipientRole).value<SendRecipient*>(), second);
    }
};

void SendRecipientsListModelTests::clearKeepsCurrentRecipientValidForQmlBindings()
{
    SendRecipientsListModel recipients; bind(recipients);
    recipients.currentRecipient()->address()->setAddress(QStringLiteral("first"));
    recipients.currentRecipient()->setLabel(QStringLiteral("First recipient"));
    recipients.currentRecipient()->amount()->setSatoshi(1'000);
    recipients.add();
    recipients.currentRecipient()->address()->setAddress(QStringLiteral("second"));
    recipients.currentRecipient()->setLabel(QStringLiteral("Second recipient"));
    recipients.currentRecipient()->amount()->setSatoshi(2'000);

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("recipientsModel"), &recipients);
    QQmlComponent component{&engine};
    component.setData(R"QML(
        import QtQml

        QtObject {
            property QtObject currentRecipient: recipientsModel.current
            property bool sawNullRecipient: false
            property string address: currentRecipient.address.address
            property string label: currentRecipient.label
            property QtObject amount: currentRecipient.amount

            onCurrentRecipientChanged: {
                if (currentRecipient === null) sawNullRecipient = true
            }
        }
    )QML", QUrl{});
    std::unique_ptr<QObject> observer{component.create()};
    QVERIFY2(observer, qPrintable(component.errorString()));
    QCOMPARE(observer->property("address").toString(), QStringLiteral("second"));
    QCOMPARE(observer->property("label").toString(), QStringLiteral("Second recipient"));

    recipients.clear();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    QVERIFY(!observer->property("sawNullRecipient").toBool());
    QCOMPARE(observer->property("currentRecipient").value<QObject*>(), recipients.currentRecipient());
    QVERIFY(observer->property("address").toString().isEmpty());
    QVERIFY(observer->property("label").toString().isEmpty());
    QCOMPARE(observer->property("amount").value<QObject*>(), recipients.currentRecipient()->amount());
}

void SendRecipientsListModelTests::clearToFrontReportsRemovedRows()
{
    SendRecipientsListModel recipients; bind(recipients);
    recipients.add();
    recipients.add();
    QCOMPARE(recipients.rowCount(), 3);

    QSignalSpy about_to_be_removed{&recipients, &QAbstractItemModel::rowsAboutToBeRemoved};
    QSignalSpy removed{&recipients, &QAbstractItemModel::rowsRemoved};
    QSignalSpy count_changed{&recipients, &SendRecipientsListModel::countChanged};

    recipients.clearToFront();

    QCOMPARE(recipients.rowCount(), 1);
    QCOMPARE(count_changed.size(), 1);
    QCOMPARE(about_to_be_removed.size(), 1);
    QCOMPARE(about_to_be_removed.at(0).at(1).toInt(), 1);
    QCOMPARE(about_to_be_removed.at(0).at(2).toInt(), 2);
    QCOMPARE(removed.size(), 1);
    QCOMPARE(removed.at(0).at(1).toInt(), 1);
    QCOMPARE(removed.at(0).at(2).toInt(), 2);

    recipients.clearToFront();

    QCOMPARE(about_to_be_removed.size(), 1);
    QCOMPARE(removed.size(), 1);
    QCOMPARE(count_changed.size(), 1);
}

void SendRecipientsListModelTests::clearToFrontDetachesRecipientsBeforeDestroyingThem()
{
    SendRecipientsListModel recipients; bind(recipients);
    recipients.add();
    recipients.add();

    const QList<SendRecipient*> removed_recipients{recipients.recipients().mid(1)};
    QCOMPARE(removed_recipients.size(), 2);

    int destroyed_count = 0;
    bool destroyed_recipient_still_listed = false;
    for (auto* recipient : removed_recipients) {
        QObject::connect(recipient, &QObject::destroyed, &recipients, [&](QObject* object) {
            ++destroyed_count;
            for (auto* listed : recipients.recipients()) {
                if (static_cast<QObject*>(listed) == object) {
                    destroyed_recipient_still_listed = true;
                }
            }
        });
    }

    recipients.clearToFront();
    QCOMPARE(destroyed_count, 0);

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    QCOMPARE(destroyed_count, 2);
    QVERIFY(!destroyed_recipient_still_listed);
    QCOMPARE(recipients.count(), 1);
    QCOMPARE(recipients.currentRecipient(), recipients.recipients().at(0));
}

void SendRecipientsListModelTests::unitChangesApplyToEveryRecipient()
{
    SendRecipientsListModel recipients; bind(recipients);
    auto* first = recipients.currentRecipient();
    first->amount()->setSatoshi(COIN / 2);

    recipients.add();
    auto* second = recipients.currentRecipient();
    second->amount()->setSatoshi(2'000);
    second->amount()->setUnit(BitcoinAmount::Unit::SAT);

    QCOMPARE(first->amount()->unit(), BitcoinAmount::Unit::SAT);
    QCOMPARE(second->amount()->unit(), BitcoinAmount::Unit::SAT);
    QCOMPARE(
        recipients.data(recipients.index(0, 0), SendRecipientsListModel::AmountRole).toString(),
        QStringLiteral("50000000"));
    QCOMPARE(
        recipients.data(recipients.index(0, 0), SendRecipientsListModel::AmountUnitLabelRole).toString(),
        QStringLiteral("sats"));

    first->amount()->setUnit(BitcoinAmount::Unit::BTC);
    QCOMPARE(first->amount()->unit(), BitcoinAmount::Unit::BTC);
    QCOMPARE(second->amount()->unit(), BitcoinAmount::Unit::BTC);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(SendRecipientsListModelTests)
#else
QTEST_MAIN(SendRecipientsListModelTests)
#endif
#include <test_sendrecipientslistmodel.moc>
