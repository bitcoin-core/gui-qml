// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletqmlcontroller.h>

#include <interfaces/node.h>

WalletQmlController::WalletQmlController(interfaces::Node& node, QObject *parent)
    : QObject(parent)
    , m_node(node)
{
    m_selected_wallet = new WalletQmlModel(parent);
}

WalletQmlController::~WalletQmlController()
{
    if (m_handler_load_wallet) {
        m_handler_load_wallet->disconnect();
    }
}

void WalletQmlController::setSelectedWallet(QString path)
{
    std::vector<bilingual_str>  warning_message;
    auto wallet{m_node.walletLoader().loadWallet(path.toStdString(), warning_message)};
    if (wallet.has_value()) {
        m_selected_wallet = new WalletQmlModel(std::move(wallet.value()));
        m_wallets.push_back(m_selected_wallet);
        Q_EMIT selectedWalletChanged();
    }
}

WalletQmlModel* WalletQmlController::selectedWallet() const
{
    return m_selected_wallet;
}

void WalletQmlController::unloadWallets()
{
    m_handler_load_wallet->disconnect();
    for (WalletQmlModel* wallet : m_wallets) {
        delete wallet;
    }
    m_wallets.clear();
}

void WalletQmlController::handleLoadWallet(std::unique_ptr<interfaces::Wallet> wallet)
{
    if (!m_wallets.empty()) {
        QString name = QString::fromStdString(wallet->getWalletName());
        for (WalletQmlModel* wallet_model : m_wallets) {
            if (wallet_model->name() == name) {
                return;
            }
        }
    }

    m_selected_wallet = new WalletQmlModel(std::move(wallet));
    m_wallets.push_back(m_selected_wallet);
    Q_EMIT selectedWalletChanged();
}

void WalletQmlController::initialize()
{
    m_handler_load_wallet = m_node.walletLoader().handleLoadWallet([this](std::unique_ptr<interfaces::Wallet> wallet) {
        handleLoadWallet(std::move(wallet));
    });

    auto wallets = m_node.walletLoader().getWallets();
    for (auto& wallet : wallets) {
        handleLoadWallet(std::move(wallet));
    }
}
