// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <QLocale>
#include <QScopeGuard>

#include <qml/bitcoinamount.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/walletqmlmodeltransaction.h>

#include <consensus/amount.h>
#include <key_io.h>
#include <addresstype.h>

class WalletQmlModelTransactionTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void singleRecipientReviewAmountsInheritRecipientUnit();
    void multipleRecipientReviewAmountsInheritSharedUnit();
    void recipientRolesExposeFormattedAddressAndUnitLabel();
    void reviewDisplayStringsUseLocale();
    void removingRecipientUpdatesTotalOnce();
    void txidTracksAssignedTransaction();
    void reviewedRecipientsStayBoundToPreparedOutputs();
};

void WalletQmlModelTransactionTests::singleRecipientReviewAmountsInheritRecipientUnit()
{
    SendRecipientsListModel recipients;
    auto* recipient = recipients.currentRecipient();
    recipient->amount()->setUnit(BitcoinAmount::Unit::SAT);
    recipient->amount()->setSatoshi(1250);

    WalletQmlModelTransaction transaction(&recipients);
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

    WalletQmlModelTransaction transaction(&recipients);
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

    SendRecipientsListModel recipients;
    recipients.currentRecipient()->amount()->setSatoshi(123'456'789);
    WalletQmlModelTransaction transaction(&recipients);
    transaction.setTransactionFee(1'000);

    QCOMPARE(transaction.amount(), QStringLiteral("1,23456789 BTC"));
    QCOMPARE(transaction.fee(), QStringLiteral("0,00001000 BTC"));
    QCOMPARE(transaction.total(), QStringLiteral("1,23457789 BTC"));
}

void WalletQmlModelTransactionTests::removingRecipientUpdatesTotalOnce()
{
    SendRecipientsListModel recipients;
    recipients.currentRecipient()->amount()->setSatoshi(1000);
    recipients.add();
    recipients.currentRecipient()->amount()->setSatoshi(2000);

    QSignalSpy total_changed_spy{&recipients, &SendRecipientsListModel::totalAmountChanged};
    recipients.remove();

    QCOMPARE(recipients.totalAmountSatoshi(), 1000);
    QCOMPARE(total_changed_spy.count(), 1);
}

void WalletQmlModelTransactionTests::txidTracksAssignedTransaction()
{
    SendRecipientsListModel recipients;
    WalletQmlModelTransaction transaction(&recipients);
    QVERIFY(transaction.txid().isEmpty());

    CMutableTransaction mutable_transaction;
    mutable_transaction.vout.emplace_back(1'000, CScript{});
    const CTransactionRef wallet_transaction = MakeTransactionRef(std::move(mutable_transaction));
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
    WalletQmlModelTransaction transaction(&recipients);
    transaction.setWtx(MakeTransactionRef(std::move(prepared)));
    QVERIFY(transaction.captureReviewedRecipients(recipients));

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
#include "test_walletqmlmodeltransaction.moc"
