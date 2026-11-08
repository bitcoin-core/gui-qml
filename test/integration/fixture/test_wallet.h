// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_WALLET_H
#define BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_WALLET_H

#include <test/integration/fixture/test_util.h>
#include <qml/walletqmlcontroller.h>

#include <stdexcept>

namespace qmltest {
inline UniValue Rpc(BitcoinQmlApplication& app, const std::string& method,
                    const UniValue& params = UniValue{UniValue::VARR}, const QString& wallet = {})
{
    try {
        return Call(app, &interfaces::Node::executeRpc, method, params.isNull() ? UniValue{UniValue::VARR} : params,
                    wallet.isEmpty() ? std::string{} : "/wallet/" + wallet.toStdString());
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

inline WalletQmlModel* WaitForWallet(BitcoinQmlApplication& app, const QString& name)
{
    auto* controller = Model<WalletQmlController>(app, "walletController");
    if (!controller || !QTest::qWaitFor([&] {
            return controller->isWalletLoaded() && !controller->walletLoadInProgress() &&
                   controller->selectedWallet()->name() == name;
        }, 30'000)) return nullptr;
    return controller->selectedWallet();
}

inline WalletQmlModel* CreateWallet(BitcoinQmlApplication& app, const QString& name = "integration")
{
    UniValue params{UniValue::VOBJ};
    params.pushKV("wallet_name", name.toStdString());
    Rpc(app, "createwallet", params);
    return WaitForWallet(app, name);
}

inline QString NewAddress(BitcoinQmlApplication& app, const QString& wallet, const char* type = "bech32")
{
    UniValue params{UniValue::VARR};
    params.push_back("");
    params.push_back(type);
    return QString::fromStdString(Rpc(app, "getnewaddress", params, wallet).get_str());
}

inline bool FundWallet(BitcoinQmlApplication& app, WalletQmlModel& wallet)
{
    UniValue params{UniValue::VARR};
    params.push_back(101);
    params.push_back(NewAddress(app, wallet.name()).toStdString());
    Rpc(app, "generatetoaddress", params);
    return QTest::qWaitFor([&] { return wallet.balanceSatoshi() > 0; }, 30'000);
}
} // namespace qmltest

#endif // BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_WALLET_H
