// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_WALLET_H
#define BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_WALLET_H

#include <util/string.h>
#include <test/integration/fixture/test_util.h>
#include <qml/walletcontroller.h>
#include <script/descriptor.h>
#include <consensus/amount.h>

#include <QUrl>

#include <stdexcept>

namespace qmltest {

// Wallet unlock and encryption run through the background security worker;
// sanitizer builds can exceed QtTest's default five-second QTRY budget by a
// few hundred milliseconds. The assertion is unchanged; only the wait budget
// grows, and successful runs still return immediately.
inline constexpr int kSecurityStateTimeoutMs{15000};
inline UniValue Rpc(BitcoinQmlApplication& app, const std::string& method,
                    const UniValue& params = UniValue{UniValue::VARR}, const QString& wallet = {})
{
    try {
        return Call(app, &interfaces::Node::executeRpc, method, params.isNull() ? UniValue{UniValue::VARR} : params,
                    wallet.isEmpty() ? std::string{} : "/wallet/" + QUrl::toPercentEncoding(wallet).toStdString());
    } catch (const UniValue& error) {
        throw std::runtime_error{"RPC " + method + " failed: " + error.write()};
    }
}

inline UniValue Json(const std::string& text)
{
    UniValue value;
    if (!value.read(text)) throw std::runtime_error{"Invalid test JSON: " + text};
    return value;
}

inline WalletModel* WaitForWallet(BitcoinQmlApplication& app, const QString& name)
{
    auto* controller = Model<WalletController>(app, "walletController");
    if (!controller || !QTest::qWaitFor([&] {
            return controller->isWalletLoaded() && !controller->walletLoadInProgress() &&
                   controller->selectedWallet()->name() == name && controller->selectedWallet()->capabilitiesKnown();
        }, 30'000)) return nullptr;
    return controller->selectedWallet();
}

// Wait for backend publication without assuming that load completion may
// override a newer GUI selection. Test setup can select this exact facade
// explicitly when later steps require it; behavior assertions should continue
// to use WaitForWallet() so they verify the actual selected wallet.
inline WalletModel* WaitForOpenWallet(BitcoinQmlApplication& app, const QString& name)
{
    auto* controller = Model<WalletController>(app, "walletController");
    if (!controller || !QTest::qWaitFor([&] {
            auto* wallet = controller->walletForName(name);
            return wallet && wallet->available() && wallet->name() == name && wallet->capabilitiesKnown();
        }, 30'000)) return nullptr;
    return controller->walletForName(name);
}

// Fixture setup makes selection an explicit operation rather than inferring it
// from the Core load result.
inline WalletModel* SelectOpenWalletForSetup(BitcoinQmlApplication& app, const QString& name)
{
    auto* controller = Model<WalletController>(app, "walletController");
    auto* wallet = WaitForOpenWallet(app, name);
    if (!controller || !wallet || !controller->selectWallet(wallet) ||
        !QTest::qWaitFor([&] { return controller->selectedWallet() == wallet; }, 30'000)) return nullptr;
    return wallet;
}

inline WalletModel* CreateWallet(BitcoinQmlApplication& app, const QString& name = "integration")
{
    UniValue params{UniValue::VOBJ};
    params.pushKV("wallet_name", name.toStdString());
    Rpc(app, "createwallet", params);
    return SelectOpenWalletForSetup(app, name);
}

// Only for cases explicitly opted into a prepared profile in their module.
inline WalletModel* LoadFundedWallet(BitcoinQmlApplication& app, const QString& name = "integration")
{
    UniValue params{UniValue::VARR};
    params.push_back(name.toStdString());
    Rpc(app, "loadwallet", params);
    auto* wallet = SelectOpenWalletForSetup(app, name);
    if (!wallet || !QTest::qWaitFor([&] { return wallet->balanceSatoshi() == 50 * COIN; }, 30'000)) return nullptr;
    const auto coins = Rpc(app, "listunspent", {}, name);
    if (Rpc(app, "getblockcount").getInt<int>() != 101 || coins.size() != 1 ||
        !Rpc(app, "getrawmempool").empty() || !Rpc(app, "listlockunspent", {}, name).empty()) return nullptr;
    return wallet;
}

// Hardened derivation cannot replenish an exhausted keypool while locked.
inline WalletModel* CreateLockedHardenedWallet(BitcoinQmlApplication& app, const QString& name, const QString& passphrase)
{
    UniValue params{UniValue::VOBJ};
    params.pushKV("wallet_name", name.toStdString());
    params.pushKV("blank", true);
    Rpc(app, "createwallet", params);
    auto* wallet = SelectOpenWalletForSetup(app, name);
    if (!wallet || !wallet->encryptWallet(passphrase)) return nullptr;
    if (!QTest::qWaitFor([&] {
            return wallet->securityState() == WalletModel::SecurityState::SecuritySucceeded && wallet->isLocked();
        }, 30'000)) return nullptr;
    UniValue unlock{UniValue::VARR};
    unlock.push_back(passphrase.toStdString());
    unlock.push_back(60);
    Rpc(app, "walletpassphrase", unlock, name);
    if (!QTest::qWaitFor([&] { return !wallet->isLocked(); }, 30'000)) return nullptr;

    const std::string key{"tprv8ZgxMBicQKsPdYeeZbPSKd2KYLmeVKtcFA7kqCxDvDR13MQ6us8HopUR2wLcS2ZKPhLyKsqpDL2FtL73LMHcgoCL7DXsciA8eX8nbjCR2eG"};
    UniValue descriptors{UniValue::VARR};
    for (int branch{0}; branch < 2; ++branch) {
        const auto descriptor = "wpkh(" + key + "/" + util::ToString(branch) + "h/*h)";
        UniValue entry{UniValue::VOBJ};
        entry.pushKV("desc", descriptor + "#" + GetDescriptorChecksum(descriptor));
        entry.pushKV("timestamp", "now");
        entry.pushKV("active", true);
        entry.pushKV("internal", branch == 1);
        entry.pushKV("range", Json("[0,0]"));
        descriptors.push_back(entry);
    }
    const auto imported = Rpc(app, "importdescriptors", Json("[" + descriptors.write() + "]"), name);
    for (const auto& entry : imported.getValues()) {
        if (!entry.find_value("success").get_bool()) throw std::runtime_error{"Descriptor import failed: " + entry.write()};
    }
    Rpc(app, "keypoolrefill", Json("[1]"), name);
    Rpc(app, "walletlock", {}, name);
    if (!QTest::qWaitFor([&] { return wallet->isLocked(); }, 30'000)) return nullptr;
    return wallet;
}

inline QString NewAddress(BitcoinQmlApplication& app, const QString& wallet, const char* type = "bech32")
{
    UniValue params{UniValue::VARR};
    params.push_back("");
    params.push_back(type);
    return QString::fromStdString(Rpc(app, "getnewaddress", params, wallet).get_str());
}

inline bool FundWallet(BitcoinQmlApplication& app, WalletModel& wallet)
{
    UniValue params{UniValue::VARR};
    params.push_back(101);
    params.push_back(NewAddress(app, wallet.name()).toStdString());
    Rpc(app, "generatetoaddress", params);
    return QTest::qWaitFor([&] { return wallet.balanceSatoshi() > 0; }, 30'000);
}
} // namespace qmltest

#endif // BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_WALLET_H
