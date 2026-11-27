// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <qml/wallettransaction.h>
#include <core_io.h>
#include <key_io.h>
#include <util/string.h>
#include <rpc/util.h>
#include <wallet/wallet.h>
#include <set>

namespace {
UniValue Args(std::initializer_list<UniValue> values) {
    UniValue result{UniValue::VARR}; for (const auto& value : values) result.push_back(value); return result;
}
struct PolicyResult { bool created{false}; std::optional<bool> sweeps; CAmount amount{0}, fee{0}; std::set<std::string> inputs; };
}
class TransactionPolicyIntegrationTests : public QObject {
    Q_OBJECT
    BitcoinQmlApplication& m_app;
public:
    explicit TransactionPolicyIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void publicMaximumRespectsIndependentRpcEligibility_data() {
        QTest::addColumn<QString>("policy");
        for (const char* policy : {"unsafe", "immature", "reuse", "zero", "selected"}) QTest::newRow(policy) << QString::fromLatin1(policy);
    }
    void publicMaximumRespectsIndependentRpcEligibility() {
        QFETCH(QString, policy);
        auto* miner = qmltest::CreateWallet(m_app, "policy-miner"); QVERIFY(miner);
        QVERIFY(qmltest::FundWallet(m_app, *miner));
        auto* target = qmltest::CreateWallet(m_app, "policy-target"); QVERIFY(target);
        auto* outside = qmltest::CreateWallet(m_app, "policy-outside"); QVERIFY(outside);
        const auto mining = qmltest::NewAddress(m_app, miner->name()).toStdString();
        const auto receive = qmltest::NewAddress(m_app, target->name()).toStdString();
        const auto destination = qmltest::NewAddress(m_app, outside->name()).toStdString();
        const auto mine = [&] { qmltest::Rpc(m_app, "generatetoaddress", Args({UniValue{1}, UniValue{mining}})); };
        if (policy == "reuse") qmltest::Rpc(m_app, "setwalletflag", Args({UniValue{"avoid_reuse"}, UniValue{true}}), target->name());
        qmltest::Rpc(m_app, "sendtoaddress", Args({UniValue{receive}, UniValue{10}}), miner->name()); mine();
        if (policy == "unsafe") {
            const auto unsafe_txid = qmltest::Rpc(m_app, "sendtoaddress", Args({UniValue{receive}, UniValue{7}}), miner->name()).get_str();
            // A sender's RPC receipt does not acknowledge another wallet's
            // asynchronous mempool notification. Observe the exact unsafe coin
            // through the independent recipient RPC before testing exclusion.
            QVERIFY2(QTest::qWaitFor([&] {
                const auto coins = qmltest::Rpc(m_app, "listunspent", Args({UniValue{0}}), target->name());
                for (const auto& coin : coins.getValues()) {
                    if (coin.find_value("txid").get_str() == unsafe_txid &&
                        coin.find_value("confirmations").getInt<int>() == 0 &&
                        !coin.find_value("safe").get_bool() && coin.find_value("spendable").get_bool() &&
                        AmountFromValue(coin.find_value("amount")) == 7 * COIN) return true;
                }
                return false;
            }, 5'000), "Independent recipient RPC did not observe the original unsafe output");
        } else if (policy == "immature") {
            qmltest::Rpc(m_app, "generatetoaddress", Args({UniValue{1}, UniValue{receive}}));
            const auto balances = qmltest::Rpc(m_app, "getbalances", {}, target->name());
            QVERIFY(AmountFromValue(balances.find_value("mine").find_value("immature")) > 0);
        } else if (policy == "reuse") {
            qmltest::Rpc(m_app, "sendtoaddress", Args({UniValue{mining}, UniValue{1}}), target->name()); mine();
            qmltest::Rpc(m_app, "sendtoaddress", Args({UniValue{receive}, UniValue{7}}), miner->name()); mine();
        } else if (policy == "zero") {
            const auto coins = qmltest::Rpc(m_app, "listunspent", {}, miner->name()); QVERIFY(coins.size() > 0);
            const auto& coin = coins[0];
            UniValue input{UniValue::VOBJ}; input.pushKV("txid", coin.find_value("txid")); input.pushKV("vout", coin.find_value("vout"));
            UniValue inputs{UniValue::VARR}; inputs.push_back(input);
            UniValue outputs{UniValue::VOBJ}; outputs.pushKV(receive, ValueFromAmount(0));
            outputs.pushKV(mining, ValueFromAmount(AmountFromValue(coin.find_value("amount")) - 100'000));
            const auto raw = qmltest::Rpc(m_app, "createrawtransaction", Args({inputs, outputs}));
            const auto signed_tx = qmltest::Rpc(m_app, "signrawtransactionwithwallet", Args({raw}), miner->name());
            QVERIFY(signed_tx.find_value("complete").get_bool());
            UniValue transactions{UniValue::VARR}; transactions.push_back(signed_tx.find_value("hex"));
            // generateblock accepts consensus-valid raw transactions without
            // mempool dust policy: the zero owned output is real, not a mock.
            qmltest::Rpc(m_app, "generateblock", Args({UniValue{mining}, transactions}));
        } else {
            qmltest::Rpc(m_app, "sendtoaddress", Args({UniValue{receive}, UniValue{7}}), miner->name()); mine();
        }
        const auto all = qmltest::Rpc(m_app, "listunspent", Args({UniValue{0}}), target->name());
        std::set<std::string> expected; CAmount gross{0}; bool saw_excluded{policy == "immature"};
        std::optional<std::pair<std::string, unsigned int>> selected;
        for (const auto& coin : all.getValues()) {
            const auto amount = AmountFromValue(coin.find_value("amount"));
            const bool eligible = coin.find_value("safe").get_bool() && coin.find_value("spendable").get_bool() && amount > 0 &&
                (policy != "reuse" || !coin.find_value("reused").get_bool());
            if (!eligible) { saw_excluded = true; continue; }
            const auto txid = coin.find_value("txid").get_str(); const auto vout = coin.find_value("vout").getInt<unsigned int>();
            if (policy == "selected" && selected) continue;
            if (policy == "selected") selected = std::pair{txid, vout};
            expected.insert(txid + ":" + util::ToString(vout)); gross += amount;
        }
        QVERIFY(!expected.empty()); if (policy != "selected") QVERIFY(saw_excluded);
        const auto result = qmltest::Call(m_app, [name = target->name().toStdString(), destination, selected, reuse = policy == "reuse"](interfaces::Node* node) {
            for (auto& wallet : node->walletLoader().getWallets()) if (wallet->getWalletName() == name) {
                wallet::CCoinControl control; control.m_feerate = CFeeRate{1000}; control.m_avoid_address_reuse = reuse;
                if (selected) { control.Select({*Txid::FromHex(selected->first), selected->second}); control.m_allow_other_inputs = false; }
                const auto created = wallettransaction::Create(*wallet, {{DecodeDestination(destination), 0, false}}, control, 0, false);
                PolicyResult result; result.created = bool(created);
                if (!created) return result;
                for (const auto& input : created->tx->vin) result.inputs.insert(input.prevout.hash.ToString() + ":" + util::ToString(input.prevout.n));
                result.amount = created->tx->vout[0].nValue; result.fee = created->fee;
                result.sweeps = wallettransaction::Sweeps(*wallet, *created->tx, control);
                return result;
            }
            return PolicyResult{};
        });
        QVERIFY(result.created); QCOMPARE(result.inputs, expected); QCOMPARE(result.amount + result.fee, gross);
        QVERIFY(result.sweeps.has_value()); QCOMPARE(*result.sweeps, policy != "selected");
    }
};
std::unique_ptr<QObject> CreateTransactionPolicyTests(BitcoinQmlApplication& app, WalletMode) { return std::make_unique<TransactionPolicyIntegrationTests>(app); }
#include <test_transactionpolicy.moc>
