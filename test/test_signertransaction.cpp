// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/signertransaction.h>
#include <test/mocks/signertransactions.h>
#include <test/qt_test_registry.h>

#include <QtTest/QtTest>

class SignerTransactionTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void rejectsChangedTerms_data() {
        QTest::addColumn<int>("mutation");
        for (size_t i = 0; i < signerfixture::CASES.size(); ++i)
            QTest::newRow(signerfixture::CASES[i].first) << int(i);
    }
    void rejectsChangedTerms() {
        QFETCH(int, mutation);
        const auto approved = *signerfixture::Review().GetUnsignedTx();
        auto returned = approved;
        signerfixture::Change(returned, signerfixture::CASES.at(mutation).second);
        QVERIFY(returned.GetHash() != approved.GetHash()); // Independent serialization/hash oracle.
        QVERIFY(!MatchesUnsignedTransaction(approved, returned));
    }
    void permitsSigningDataWithoutChangingTerms() {
        const auto approved = *signerfixture::Review().GetUnsignedTx();
        auto returned = approved;
        for (auto& input : returned.vin) {
            input.scriptSig = CScript{} << std::vector<unsigned char>{1, 2};
            input.scriptWitness.stack = {{3, 4}};
        }
        QVERIFY(MatchesUnsignedTransaction(approved, returned));
        for (auto& input : returned.vin) { input.scriptSig.clear(); input.scriptWitness.SetNull(); }
        QCOMPARE(returned.GetHash(), approved.GetHash());
    }
};
BITCOINQML_REGISTER_QT_TEST(SignerTransactionTests)
#include <test_signertransaction.moc>
