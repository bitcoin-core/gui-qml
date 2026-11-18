// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/feebumpcontroller.h>
#include <qml/models/feebumps.h>
#include <qml/models/walletpagecontroller.h>
#include <test/qt_test_registry.h>

#include <QPointer>
#include <QRegularExpression>
#include <QtTest/QtTest>

#include <memory>

namespace {
class TestWalletInterface : public WalletInterface
{
public:
    TestWalletInterface() : WalletInterface(nullptr) {}
    bool available() const override { return true; }
};

class TestPageController : public WalletPageController
{
public:
    TestPageController() : WalletPageController(nullptr) {}
    using WalletPageController::bindWallet;
    unsigned stops{0};

private:
    void stop() override { ++stops; }
};

//! One attempt whose transitions the test drives.
class FakeAttempt : public FeeBumpOperation
{
public:
    explicit FakeAttempt(QObject* parent) : FeeBumpOperation(parent) {}
    State m_state{Idle};
    QString m_txid, m_new_txid;
    int reviews{0}, confirms{0}, rechecks{0}, discards{0}, retries{0}, closes{0}, unit{-1};
    bool m_committed{false};
    void set(State state) { m_state = state; Q_EMIT stateChanged(); }
    State state() const override { return m_state; }
    bool busy() const override { return m_state == Preparing || m_state == Committing; }
    QString oldFee() const override { return {}; }
    QString newFee() const override { return {}; }
    QString feeIncrease() const override { return {}; }
    QString oldTxid() const override { return m_txid; }
    QString newTxid() const override { return m_new_txid; }
    WalletRecordState walletRecordState() const override { return WalletRecordUnknown; }
    WalletSubmissionState walletSubmissionState() const override { return WalletSubmissionUnknown; }
    QString errorText() const override { return {}; }
    QString unlockErrorText() const override { return {}; }
    bool needsUnlock() const override { return false; }
    bool canRecheckOutcome() const override { return m_state == Uncertain; }
    bool committed() const override { return m_committed; }
    bool review(const QString& txid, unsigned) override { ++reviews; m_txid = txid; set(NeedsConfirmation); return true; }
    bool prepareAgain(const QString& txid, unsigned target) override { return review(txid, target); }
    bool confirm() override { ++confirms; m_committed = true; set(Committing); return true; }
    bool confirmWithPassphrase(const QString&) override { return confirm(); }
    bool recheck() override { ++rechecks; return true; }
    void discard() override { ++discards; if (!m_committed) set(Idle); }
    void setDisplayUnit(int value) override { unit = value; }
    void retryPublications() override { ++retries; }
    void close() override { ++closes; }
};

struct Wallet {
    bool available{true};
    QList<QPointer<FakeAttempt>> created;
    std::unique_ptr<FeeBumps> bumps{std::make_unique<FeeBumps>([this] { return available; }, [this](QObject* parent) {
        auto* attempt = new FakeAttempt(parent); created.append(attempt); return attempt;
    }, nullptr)};
    FakeAttempt* last() const { return created.isEmpty() ? nullptr : created.last().data(); }
};

std::unique_ptr<FeeBumpController> Page(Wallet& wallet)
{
    auto page = std::make_unique<FeeBumpController>();
    page->setFeeBumps(wallet.bumps.get());
    page->setActive(true);
    return page;
}
} // namespace

class WalletPageControllerTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void hidingAReviewBeforeItsCommitDiscardsIt()
    {
        Wallet wallet; auto page = Page(wallet);
        QVERIFY(page->open("tx")); QVERIFY(page->readyToConfirm());
        QPointer<FakeAttempt> attempt = wallet.last();
        page->setActive(false);
        QVERIFY(!page->operation()); QCOMPARE(attempt->discards, 1);
        QTRY_VERIFY(!attempt); // Forgotten by the wallet.
        QVERIFY(!wallet.bumps->kept("tx"));
    }

    void destroyingThePageAfterTheCommitKeepsTheAttemptForTheNextOpen_data()
    {
        QTest::addColumn<int>("trigger");
        QTest::newRow("hidden") << 0;
        QTest::newRow("wallet-unavailable") << 1;
        QTest::newRow("destroyed") << 2;
    }
    void destroyingThePageAfterTheCommitKeepsTheAttemptForTheNextOpen()
    {
        QFETCH(int, trigger);
        Wallet wallet; auto page = Page(wallet);
        QVERIFY(page->open("tx")); QVERIFY(page->proceed());
        QPointer<FakeAttempt> attempt = wallet.last(); QVERIFY(attempt->committed());
        if (trigger == 0) page->setActive(false);
        else if (trigger == 1) { wallet.available = false; wallet.bumps->refresh(); }
        else page.reset();
        QVERIFY(!page || !page->operation());
        QVERIFY(attempt); QCOMPARE(attempt->discards, 0);
        QCOMPARE(wallet.bumps->kept("tx"), attempt.data());
        wallet.available = true; wallet.bumps->refresh();
        // The uncertain outcome is shown again and can be rechecked, never committed twice.
        attempt->set(FeeBumpOperation::Uncertain);
        auto next = Page(wallet);
        QVERIFY(next->open("tx"));
        QCOMPARE(next->operation(), attempt.data()); QCOMPARE(wallet.created.size(), 1);
        QVERIFY(next->uncertainOutcome());
        QVERIFY(next->proceed()); QCOMPARE(attempt->rechecks, 1); QCOMPARE(attempt->confirms, 1);
    }

    void aResultFinishedWhileClosedIsShownOnce()
    {
        Wallet wallet; auto page = Page(wallet);
        QVERIFY(page->open("tx")); QVERIFY(page->proceed());
        QPointer<FakeAttempt> attempt = wallet.last();
        page->setActive(false);
        attempt->m_new_txid = QStringLiteral("replacement"); attempt->set(FeeBumpOperation::Succeeded);
        page->setActive(true);
        QVERIFY(page->open("tx")); QVERIFY(page->succeeded());
        QCOMPARE(page->operation()->newTxid(), QStringLiteral("replacement"));
        page->dismiss();
        QTRY_VERIFY(!attempt);
        QVERIFY(page->open("tx"));
        QVERIFY(page->readyToConfirm()); QCOMPARE(wallet.created.size(), 2); QCOMPARE(wallet.last()->reviews, 1);
    }

    void aFailureTheOpenPageShowedIsNotShownAgain()
    {
        // The signer changed the replacement: the commit failed while shown.
        Wallet wallet; auto page = Page(wallet);
        QVERIFY(page->open("tx")); QVERIFY(page->proceed());
        QPointer<FakeAttempt> attempt = wallet.last();
        attempt->set(FeeBumpOperation::Failed);
        page->setActive(false);
        QTRY_VERIFY(!attempt); QVERIFY(!wallet.bumps->kept("tx"));
        page->setActive(true);
        QVERIFY(page->open("tx")); QVERIFY(page->readyToConfirm()); QCOMPARE(wallet.created.size(), 2);
    }

    void theWalletForwardsRetriesAndUnitAndClosesItsAttempts()
    {
        Wallet wallet; auto first = Page(wallet); auto second = Page(wallet);
        QVERIFY(first->open("a")); QVERIFY(first->proceed());
        QVERIFY(second->open("b"));
        first.reset(); // "a" is kept, "b" is live.
        wallet.bumps->setDisplayUnit(3);
        wallet.bumps->retryPublications();
        for (const auto& attempt : wallet.created) { QVERIFY(attempt); QCOMPARE(attempt->unit, 3); QCOMPARE(attempt->retries, 1); }
        wallet.bumps->close();
        for (const auto& attempt : wallet.created) QCOMPARE(attempt->closes, 1);
        QVERIFY(!second->available()); QVERIFY(!second->operation());
        QVERIFY(!second->open("b"));
    }

    void walletDestructionCannotAllowASecondBinding()
    {
        TestPageController page;
        page.setActive(true);
        auto first = std::make_unique<TestWalletInterface>();
        QVERIFY(page.bindWallet(first.get()));
        QVERIFY(page.bindWallet(first.get()));
        QVERIFY(page.available());

        first.reset();
        QVERIFY(!page.available());
        QCOMPARE(page.stops, 1U);

        TestWalletInterface second;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("WalletPageController was given a second wallet"));
        QVERIFY(!page.bindWallet(&second));
        QVERIFY(!page.available());
        QCOMPARE(page.stops, 1U);
    }

    void aSecondWalletDisablesThePage()
    {
        Wallet a, b; auto page = Page(a);
        QVERIFY(page->open("tx")); QPointer<FakeAttempt> attempt = a.last();
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("FeeBumpController was given a second wallet"));
        page->setFeeBumps(b.bumps.get());
        QCOMPARE(page->feeBumps(), static_cast<FeeBumpInterface*>(a.bumps.get()));
        QVERIFY(!page->available()); QVERIFY(!page->operation());
        QCOMPARE(attempt ? attempt->discards : 1, 1);
        QVERIFY(!page->open("tx")); QVERIFY(b.created.isEmpty()); QCOMPARE(a.created.size(), 1);
    }
};

BITCOINQML_REGISTER_QT_TEST(WalletPageControllerTests)
#include <test_walletpagecontrollers.moc>
