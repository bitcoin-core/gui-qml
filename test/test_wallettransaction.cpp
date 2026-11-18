// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/wallettransaction.h>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>
#include <chainparams.h>
#include <key_io.h>
#include <util/fees.h>
#include <QtTest/QtTest>
#include <stdexcept>
#include <vector>

namespace {
// Independent public-wallet policy oracle. Presets deliberately bypass the
// automatic filters, as Core does: enumerating listCoins then selecting it
// must fail these tests, rather than accidentally looking equivalent.
struct PolicyWallet : StubWallet {
    struct Coin { COutPoint id; CAmount value; bool safe{true}, mature{true}, reused{false}, owned{true}, locked{false}, spent{false}; };
    std::vector<Coin> coins;
    CScript own{CScript{} << OP_TRUE};
    bool fail_presets{false}, inconsistent_balance{false}, unexpected_change{false};
    int lists{0}, creates{0};
    std::vector<COutPoint> last_selected;
    bool last_allow{false};
    const Coin* find(const COutPoint& id) const {
        for (const auto& coin : coins) if (coin.id == id) return &coin;
        return nullptr;
    }
    bool eligible(const Coin& coin, const wallet::CCoinControl& control) const {
        return coin.owned && coin.mature && !coin.locked && !coin.spent && coin.value > 0 &&
            (coin.safe || control.m_include_unsafe_inputs) && (!coin.reused || !control.m_avoid_address_reuse);
    }
    CAmount getAvailableBalance(const wallet::CCoinControl& control) override {
        CAmount total{0};
        if (!fail_presets) for (const auto& id : control.ListSelected()) if (const auto* coin = find(id)) total += coin->value;
        if (control.m_allow_other_inputs) for (const auto& coin : coins) if (!control.IsSelected(coin.id) && eligible(coin, control)) total += coin.value;
        return inconsistent_balance && control.HasSelected() ? total - 1 : total;
    }
    CTransactionRef getTx(const Txid& id) override {
        CMutableTransaction previous; bool found{false};
        for (const auto& coin : coins) if (coin.id.hash == id) {
            found = true;
            if (previous.vout.size() <= coin.id.n) previous.vout.resize(coin.id.n + 1);
            previous.vout[coin.id.n] = CTxOut{coin.value, own};
        }
        return found ? MakeTransactionRef(std::move(previous)) : CTransactionRef{};
    }
    CoinsList listCoins() override { ++lists; throw std::logic_error{"Candidate enumeration must not become manual presets"}; }
    bool txoutIsMine(const CTxOut& output) override { return output.scriptPubKey == own; }
    std::vector<interfaces::WalletTxOut> getCoins(const std::vector<COutPoint>& inputs) override {
        std::vector<interfaces::WalletTxOut> result;
        for (const auto& input : inputs) {
            const auto* coin = find(input);
            if (!coin) return {};
            interfaces::WalletTxOut out{};
            out.txout = CTxOut{coin->value, own}; out.is_spent = coin->spent; out.depth_in_main_chain = coin->mature ? 6 : 1;
            result.push_back(out);
        }
        return result;
    }
    util::Result<wallet::CreatedTransactionResult> createTransaction(const std::vector<wallet::CRecipient>& recipients,
        const wallet::CCoinControl& control, bool, std::optional<unsigned int>) override {
        ++creates; last_selected = control.ListSelected(); last_allow = control.m_allow_other_inputs;
        CMutableTransaction tx; CAmount total{0}, sent{0};
        for (const auto& coin : coins) if (control.IsSelected(coin.id) || (control.m_allow_other_inputs && eligible(coin, control))) {
            tx.vin.emplace_back(coin.id); total += coin.value;
        }
        for (const auto& recipient : recipients) {
            const auto amount = recipient.nAmount - (recipient.fSubtractFeeFromAmount ? 100 : 0);
            if (amount <= 0) return util::Error{Untranslated("Dust")};
            tx.vout.emplace_back(amount, GetScriptForDestination(recipient.dest)); sent += amount;
        }
        if (sent + 100 != total) return util::Error{Untranslated("Policy or balance changed")};
        std::optional<unsigned int> change;
        if (unexpected_change) { change = tx.vout.size(); tx.vout.emplace_back(1, own); }
        return wallet::CreatedTransactionResult{MakeTransactionRef(tx), CAmount{100}, change, FeeReason::FALLBACK};
    }
};
COutPoint Id(unsigned int index) { return {Txid::FromUint256(uint256::ONE), index}; }
std::vector<wallet::CRecipient> Recipients() {
    return {{DecodeDestination("1BoatSLRHtKNngkdXEeobR76b53LETtpyT"), 0, false}};
}
}
class WalletTransactionTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { SelectParams(ChainType::MAIN); }
    void automaticMaximumPreservesPolicy_data() {
        QTest::addColumn<int>("policy");
        QTest::newRow("unsafe") << 0; QTest::newRow("immature") << 1;
        QTest::newRow("reused") << 2; QTest::newRow("watchonly") << 3;
        QTest::newRow("locked") << 4; QTest::newRow("zero") << 5;
    }
    void automaticMaximumPreservesPolicy() {
        QFETCH(int, policy);
        PolicyWallet wallet; wallet.coins = {{Id(0), 50'000}, {Id(1), 70'000}};
        auto& excluded = wallet.coins[1];
        if (policy == 0) excluded.safe = false;
        if (policy == 1) excluded.mature = false;
        if (policy == 2) excluded.reused = true;
        if (policy == 3) excluded.owned = false;
        if (policy == 4) excluded.locked = true;
        if (policy == 5) excluded.value = 0;
        wallet::CCoinControl control; control.m_avoid_address_reuse = true;
        const auto result = wallettransaction::Create(wallet, Recipients(), control, 0, false);
        QVERIFY(result); QCOMPARE(result->tx->vin.size(), size_t{1}); QCOMPARE(result->tx->vin[0].prevout, Id(0));
        QCOMPARE(result->tx->vout[0].nValue, CAmount{49'900}); QVERIFY(wallet.last_selected.empty());
        QVERIFY(wallet.last_allow); QCOMPARE(wallet.lists, 0);
        QCOMPARE(wallettransaction::Sweeps(wallet, *result->tx, control), std::optional<bool>{true});
    }
    void originalUnsafeOptInAndManualPresetsRemainExplicit() {
        PolicyWallet wallet; wallet.coins = {{Id(0), 50'000}, {Id(1), 70'000, false}};
        wallet::CCoinControl automatic; automatic.m_include_unsafe_inputs = true;
        const auto all = wallettransaction::Create(wallet, Recipients(), automatic, 0, false);
        QVERIFY(all); QCOMPARE(all->tx->vin.size(), size_t{2}); QCOMPARE(all->tx->vout[0].nValue, CAmount{119'900});
        wallet::CCoinControl manual; manual.Select(Id(1)); manual.m_allow_other_inputs = false;
        const auto selected = wallettransaction::Create(wallet, Recipients(), manual, 0, false);
        QVERIFY(selected); QCOMPARE(wallet.last_selected, std::vector<COutPoint>{Id(1)}); QVERIFY(!wallet.last_allow);
        QCOMPARE(selected->tx->vout[0].nValue, CAmount{69'900});
        QCOMPARE(wallettransaction::Sweeps(wallet, *selected->tx, manual), std::optional<bool>{false});
    }
    void missingAndIncoherentObservationsAreUnknown() {
        PolicyWallet wallet; wallet.coins = {{Id(0), 50'000}, {Id(1), 50'000}};
        CMutableTransaction tx; tx.vin.emplace_back(Id(0)); tx.vout.emplace_back(49'900, CScript{} << OP_FALSE);
        wallet::CCoinControl control;
        QCOMPARE(wallettransaction::Sweeps(wallet, CTransaction{tx}, control), std::optional<bool>{false});
        wallet.fail_presets = true;
        QVERIFY(!wallettransaction::Sweeps(wallet, CTransaction{tx}, control)); // Equal leftover cannot fake sweep.
        wallet.fail_presets = false; wallet.inconsistent_balance = true;
        QVERIFY(!wallettransaction::Sweeps(wallet, CTransaction{tx}, control));
        wallet.inconsistent_balance = false; tx.vin[0].prevout = Id(99);
        QVERIFY(!wallettransaction::Sweeps(wallet, CTransaction{tx}, control));
        tx.vin.clear(); QVERIFY(!wallettransaction::Sweeps(wallet, CTransaction{tx}, control));
        tx.vout.emplace_back(1, wallet.own);
        QCOMPARE(wallettransaction::Sweeps(wallet, CTransaction{tx}, control), std::optional<bool>{false});
    }
    void selectedSpentMissingAndBadIndexNeverReachConstruction() {
        PolicyWallet wallet; wallet.coins = {{Id(0), 50'000}};
        wallet::CCoinControl control; control.m_allow_other_inputs = false;
        control.Select(Id(99));
        QVERIFY(!wallettransaction::Create(wallet, Recipients(), control, 0, false));
        control.UnSelectAll(); control.Select({Txid{}, 0});
        QVERIFY(!wallettransaction::Create(wallet, Recipients(), control, 0, false));
        control.UnSelectAll(); control.Select(Id(0)); wallet.coins[0].spent = true;
        QVERIFY(!wallettransaction::Create(wallet, Recipients(), control, 0, false));
        QCOMPARE(wallet.creates, 0); QCOMPARE(wallet.lists, 0);
    }
    void maximumRejectsUnexpectedChangeAndKeepsFixedRecipient() {
        PolicyWallet wallet; wallet.coins = {{Id(0), 50'000}};
        auto recipients = Recipients();
        recipients.push_back({DecodeDestination("1BitcoinEaterAddressDontSendf59kuE"), 5000, true});
        wallet::CCoinControl control;
        const auto result = wallettransaction::Create(wallet, recipients, control, 0, false);
        QVERIFY(result); QCOMPARE(result->tx->vout[0].nValue, CAmount{44'900}); QCOMPARE(result->tx->vout[1].nValue, CAmount{5000});
        wallet.unexpected_change = true; QVERIFY(!wallettransaction::Create(wallet, recipients, control, 0, false));
    }
};
BITCOINQML_REGISTER_QT_TEST(WalletTransactionTests)
#include <test_wallettransaction.moc>
