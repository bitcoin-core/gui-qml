// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/transaction.h>

#include <chainparams.h>
#include <interfaces/wallet.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <uint256.h>
#include <wallet/types.h>

#include <QtTest/QtTest>

namespace {
constexpr CAmount COIN_VALUE{100'000'000};

CTxDestination Destination(unsigned char value)
{
    uint160 id;
    id.begin()[0] = value;
    return PKHash{id};
}

interfaces::WalletTx MakeWalletTx(
    const std::vector<bool>& txin_is_mine,
    const std::vector<CAmount>& output_values,
    const std::vector<bool>& txout_is_mine,
    const std::vector<bool>& txout_is_change,
    CAmount debit,
    CAmount credit = 0)
{
    CMutableTransaction mtx;
    mtx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256{1}), 0});

    std::vector<CTxDestination> addresses;
    std::vector<bool> address_is_mine;
    for (size_t i = 0; i < output_values.size(); ++i) {
        mtx.vout.emplace_back(output_values[i], CScript{});
        addresses.push_back(Destination(static_cast<unsigned char>(i + 1)));
        address_is_mine.push_back(txout_is_mine[i]);
    }

    interfaces::WalletTx wtx;
    wtx.tx = MakeTransactionRef(std::move(mtx));
    wtx.txin_is_mine = txin_is_mine;
    wtx.txout_is_mine = txout_is_mine;
    wtx.txout_is_change = txout_is_change;
    wtx.txout_address = addresses;
    wtx.txout_address_is_mine = address_is_mine;
    wtx.credit = credit;
    wtx.debit = debit;
    wtx.change = 0;
    wtx.time = 0;
    wtx.is_coinbase = false;
    return wtx;
}
} // namespace

class TransactionTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void fromWalletTx_hidesSenderChangeOutput();
    void fromWalletTx_mixedDebitKeepsNegativeNetAmount();
    void fromWalletTx_showsIncomingPaymentToChangeAddress();
};

void TransactionTests::initTestCase()
{
    SelectParams(ChainType::REGTEST);
}

void TransactionTests::fromWalletTx_hidesSenderChangeOutput()
{
    const interfaces::WalletTx wtx = MakeWalletTx(
        /*txin_is_mine=*/{true},
        /*output_values=*/{70 * COIN_VALUE, 29 * COIN_VALUE},
        /*txout_is_mine=*/{false, true},
        /*txout_is_change=*/{false, true},
        /*debit=*/100 * COIN_VALUE);

    const auto parts = Transaction::fromWalletTx(wtx);

    QCOMPARE(parts.size(), 1);
    QCOMPARE(parts.at(0)->type, Transaction::SendToAddress);
    QCOMPARE(parts.at(0)->idx, 0);
    QCOMPARE(parts.at(0)->debit, -71 * COIN_VALUE);
    QCOMPARE(parts.at(0)->credit, 0);
    QCOMPARE(parts.at(0)->netAmount(), -71 * COIN_VALUE);
    QCOMPARE(parts.at(0)->prettyAmount(), QStringLiteral("-71.00000000"));
}

void TransactionTests::fromWalletTx_mixedDebitKeepsNegativeNetAmount()
{
    const interfaces::WalletTx wtx = MakeWalletTx(
        /*txin_is_mine=*/{true, false},
        /*output_values=*/{80 * COIN_VALUE},
        /*txout_is_mine=*/{false},
        /*txout_is_change=*/{false},
        /*debit=*/100 * COIN_VALUE);

    const auto parts = Transaction::fromWalletTx(wtx);

    QCOMPARE(parts.size(), 1);
    QCOMPARE(parts.at(0)->type, Transaction::Other);
    QCOMPARE(parts.at(0)->idx, 0);
    QCOMPARE(parts.at(0)->debit, -100 * COIN_VALUE);
    QCOMPARE(parts.at(0)->credit, 0);
    QCOMPARE(parts.at(0)->netAmount(), -100 * COIN_VALUE);
    QCOMPARE(parts.at(0)->prettyAmount(), QStringLiteral("-100.00000000"));
}

void TransactionTests::fromWalletTx_showsIncomingPaymentToChangeAddress()
{
    const interfaces::WalletTx wtx = MakeWalletTx(
        /*txin_is_mine=*/{false},
        /*output_values=*/{5 * COIN_VALUE},
        /*txout_is_mine=*/{true},
        /*txout_is_change=*/{true},
        /*debit=*/0,
        /*credit=*/5 * COIN_VALUE);

    const auto parts = Transaction::fromWalletTx(wtx);

    QCOMPARE(parts.size(), 1);
    QCOMPARE(parts.at(0)->type, Transaction::RecvWithAddress);
    QCOMPARE(parts.at(0)->idx, 0);
    QCOMPARE(parts.at(0)->debit, 0);
    QCOMPARE(parts.at(0)->credit, 5 * COIN_VALUE);
    QCOMPARE(parts.at(0)->netAmount(), 5 * COIN_VALUE);
    QCOMPARE(parts.at(0)->prettyAmount(), QStringLiteral("+5.00000000"));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(TransactionTests)
#else
QTEST_MAIN(TransactionTests)
#endif
#include "test_transaction.moc"
