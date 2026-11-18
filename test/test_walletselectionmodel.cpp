// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletselectionmodel.h>
#include <test/qt_test_registry.h>

#include <QtTest/QtTest>

class WalletSelectionModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void defaultWalletHasAnExplicitPendingIntent();
    void staleCompletionCannotClearNewIntent();
    void invalidFacadeDoesNotAdvanceSelection();
    void reentrantPendingIntentRetainsItsIdentity();
    void stoppingRejectsFurtherAdmission();
};

void WalletSelectionModelTests::defaultWalletHasAnExplicitPendingIntent()
{
    WalletSelectionModel selection{nullptr, {}};
    QSignalSpy pending{&selection, &WalletSelectionModel::pendingChanged};
    const auto request = selection.beginSelection({});
    QVERIFY(request != 0);
    QVERIFY(selection.pending());
    QCOMPARE(selection.pendingTargetId(), QString{});
    QCOMPARE(pending.size(), 1);
    selection.finishSelection(request);
    QVERIFY(!selection.pending());
    QCOMPARE(pending.size(), 2);
    QCOMPARE(selection.selectedWallet(), nullptr);
}

void WalletSelectionModelTests::staleCompletionCannotClearNewIntent()
{
    WalletSelectionModel selection{nullptr, {}};
    const auto first = selection.beginSelection("A");
    const auto second = selection.beginSelection("B");
    selection.finishSelection(first);
    QVERIFY(selection.pending());
    QCOMPARE(selection.pendingTargetId(), QString{"B"});
    // Clearing the presentation is a newer selection intent; it does not
    // rewrite or complete an independently owned opening request.
    selection.clear();
    QVERIFY(selection.revision() > second);
    selection.finishSelection(second);
    QVERIFY(!selection.pending());
    QCOMPARE(selection.selectedWallet(), nullptr);
}

void WalletSelectionModelTests::invalidFacadeDoesNotAdvanceSelection()
{
    WalletSelectionModel selection{nullptr, {}};
    const auto request = selection.beginSelection("A");
    QSignalSpy rejected{&selection, &WalletSelectionModel::commandRejected};
    QVERIFY(!selection.select(nullptr));
    QCOMPARE(rejected.size(), 1);
    QVERIFY(!selection.error().isEmpty());
    QCOMPARE(selection.revision(), request);
    QVERIFY(selection.pending());
}

void WalletSelectionModelTests::reentrantPendingIntentRetainsItsIdentity()
{
    WalletSelectionModel selection{nullptr, {}};
    quint64 newer{0};
    connect(&selection, &WalletSelectionModel::pendingChanged, &selection, [&] {
        if (selection.pendingTargetId() == "A") newer = selection.beginSelection("B");
    });
    const auto older = selection.beginSelection("A");
    QVERIFY(newer > older);
    selection.finishSelection(older);
    QCOMPARE(selection.pendingTargetId(), QString{"B"});
    QVERIFY(selection.pending());
    selection.finishSelection(newer);
    QVERIFY(!selection.pending());
}

void WalletSelectionModelTests::stoppingRejectsFurtherAdmission()
{
    WalletSelectionModel selection{nullptr, {}};
    const auto request = selection.beginSelection("A");
    selection.closeAdmission();
    QVERIFY(!selection.pending());
    QCOMPARE(selection.beginSelection("B"), quint64{0});
    QVERIFY(!selection.select(nullptr));
    selection.finishSelection(request);
    QVERIFY(!selection.pending());
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(WalletSelectionModelTests)
#else
QTEST_MAIN(WalletSelectionModelTests)
#endif
#include <test_walletselectionmodel.moc>
