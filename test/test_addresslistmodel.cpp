// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mockwallet.h>

#include <QVariantMap>

#include <addresstype.h>
#include <chainparams.h>
#include <key_io.h>
#include <outputtype.h>
#include <primitives/transaction.h>
#include <qml/models/addresslistmodel.h>
#include <qml/models/walletqmlmodel.h>
#include <uint256.h>
#include <wallet/coincontrol.h>
#include <wallet/types.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace {
CTxDestination TestDestination(uint8_t seed)
{
    std::vector<unsigned char> bytes(20, seed);
    return WitnessV0KeyHash{uint160{bytes}};
}

CTransactionRef TransactionWithOutputs(const std::vector<CTxDestination>& destinations, const std::vector<CAmount>& amounts)
{
    CMutableTransaction tx;
    tx.version = 2;
    for (size_t i{0}; i < destinations.size(); ++i) {
        tx.vout.emplace_back(amounts.at(i), GetScriptForDestination(destinations.at(i)));
    }
    return MakeTransactionRef(tx);
}

class TestAddressWallet final : public StubWallet
{
public:
    std::string m_name{"test_wallet"};
    std::vector<interfaces::WalletAddress> m_addresses;
    std::map<CTxDestination, std::string> m_labels;
    interfaces::Wallet::CoinsList m_coins;
    std::set<interfaces::WalletTx> m_txs;
    CTxDestination m_next_destination{TestDestination(100)};

    std::string getWalletName() override { return m_name; }
    util::Result<CTxDestination> getNewDestination(const OutputType, const std::string& label) override
    {
        m_labels[m_next_destination] = label;
        m_addresses.emplace_back(m_next_destination, true, wallet::AddressPurpose::RECEIVE, label);
        return m_next_destination;
    }
    bool isSpendable(const CTxDestination&) override { return true; }
    bool setAddressBook(const CTxDestination& dest, const std::string& name, const std::optional<wallet::AddressPurpose>&) override
    {
        m_labels[dest] = name;
        for (auto& address : m_addresses) {
            if (address.dest == dest) {
                address.name = name;
            }
        }
        return true;
    }
    bool delAddressBook(const CTxDestination&) override { return false; }
    bool getAddress(const CTxDestination& dest, std::string* name, wallet::AddressPurpose* purpose) override
    {
        for (const auto& address : m_addresses) {
            if (address.dest != dest) continue;
            if (name) *name = address.name;
            if (purpose) *purpose = address.purpose;
            return true;
        }
        return false;
    }
    std::vector<interfaces::WalletAddress> getAddresses() override { return m_addresses; }
    bool setAddressReceiveRequest(const CTxDestination&, const std::string&, const std::string&) override { return true; }
    std::set<interfaces::WalletTx> getWalletTxs() override { return m_txs; }
    interfaces::Wallet::CoinsList listCoins() override { return m_coins; }
    bool hdEnabled() override { return true; }
    bool canGetAddresses() override { return true; }
    bool taprootEnabled() override { return true; }
};

interfaces::WalletTx WalletTxFor(const std::vector<CTxDestination>& destinations, const std::vector<CAmount>& amounts, const std::vector<bool>& is_change)
{
    interfaces::WalletTx wallet_tx;
    wallet_tx.tx = TransactionWithOutputs(destinations, amounts);
    wallet_tx.txout_address = destinations;
    wallet_tx.txout_address_is_mine.assign(destinations.size(), true);
    wallet_tx.txout_is_change = is_change;
    return wallet_tx;
}
} // namespace

class AddressListModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void receiveAddressesHideUsedUntilEnabled();
    void labelsCanBeEdited();
    void changeAddressesComeFromUnspentChangeOutputs();
};

void AddressListModelTests::initTestCase()
{
    SelectParams(ChainType::REGTEST);
}

void AddressListModelTests::receiveAddressesHideUsedUntilEnabled()
{
    auto wallet{std::make_unique<TestAddressWallet>()};
    const CTxDestination unused{TestDestination(1)};
    const CTxDestination used{TestDestination(2)};
    wallet->m_addresses = {
        {unused, true, wallet::AddressPurpose::RECEIVE, "unused"},
        {used, true, wallet::AddressPurpose::RECEIVE, "used"},
        {TestDestination(3), true, wallet::AddressPurpose::SEND, "send"},
    };

    TestAddressWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    AddressListModel* model{wallet_model.addressListModel()};
    wallet_ptr->m_txs.insert(WalletTxFor({used}, {COIN}, {false}));
    model->refresh();

    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->data(model->index(0), AddressListModel::LabelRole).toString(), QStringLiteral("unused"));
    QCOMPARE(model->data(model->index(0), AddressListModel::DisplayAmountRole).toString(), QStringLiteral("₿ 0.0"));
    QCOMPARE(model->data(model->index(0), AddressListModel::HasAmountRole).toBool(), false);

    model->setShowUsed(true);
    QCOMPARE(model->rowCount(), 2);
}

void AddressListModelTests::labelsCanBeEdited()
{
    auto wallet{std::make_unique<TestAddressWallet>()};
    const CTxDestination dest{TestDestination(1)};
    wallet->m_addresses = {
        {dest, true, wallet::AddressPurpose::RECEIVE, "first label"},
    };
    WalletQmlModel wallet_model{std::move(wallet)};
    AddressListModel* model{wallet_model.addressListModel()};
    model->refresh();

    const QString address{QString::fromStdString(EncodeDestination(dest))};
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->data(model->index(0), AddressListModel::LabelRole).toString(), QStringLiteral("first label"));

    QVERIFY(model->setAddressLabel(address, QStringLiteral("updated label")));
    QCOMPARE(model->data(model->index(0), AddressListModel::LabelRole).toString(), QStringLiteral("updated label"));
}

void AddressListModelTests::changeAddressesComeFromUnspentChangeOutputs()
{
    auto wallet{std::make_unique<TestAddressWallet>()};
    const CTxDestination receive{TestDestination(10)};
    const CTxDestination change{TestDestination(11)};
    const CTxDestination spent_change{TestDestination(12)};
    const interfaces::WalletTx change_tx{WalletTxFor({receive, change, spent_change}, {COIN, 2 * COIN, 3 * COIN}, {false, true, true})};
    const Txid txid{change_tx.tx->GetHash()};
    wallet->m_addresses = {
        {receive, true, wallet::AddressPurpose::RECEIVE, "receive"},
    };
    interfaces::WalletTxOut change_out;
    change_out.txout = CTxOut{2 * COIN, GetScriptForDestination(change)};
    change_out.time = 0;

    TestAddressWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    AddressListModel* model{wallet_model.addressListModel()};
    wallet_ptr->m_txs.insert(change_tx);
    wallet_ptr->m_coins[receive].push_back({COutPoint{txid, 1}, change_out});
    model->setCategory(AddressListModel::Change);

    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->data(model->index(0), AddressListModel::AddressRole).toString(), QString::fromStdString(EncodeDestination(change)));
    QCOMPARE(model->data(model->index(0), AddressListModel::CategoryRole).toString(), QStringLiteral("change"));
    QCOMPARE(model->data(model->index(0), AddressListModel::DisplayAmountRole).toString(), QStringLiteral("₿ 2.00000000"));
    QCOMPARE(model->data(model->index(0), AddressListModel::HasAmountRole).toBool(), true);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(AddressListModelTests)
#else
QTEST_MAIN(AddressListModelTests)
#endif
#include "test_addresslistmodel.moc"
