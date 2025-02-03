// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletqmlcontroller.h>

#include <qml/models/walletqmlmodel.h>

#include <interfaces/node.h>
#include <util/threadnames.h>

#include <QTimer>

WalletQmlController::WalletQmlController(interfaces::Node& node, QObject *parent)
    : QObject(parent)
    , m_node(node)
    , m_selected_wallet(new WalletQmlModel(parent))
    , m_worker(new QObject)
    , m_worker_thread(new QThread(this))
{
    m_worker->moveToThread(m_worker_thread);
    m_worker_thread->start();
    QTimer::singleShot(0, m_worker, []() {
        util::ThreadRename("qml-walletctrl");
    });
}

WalletQmlController::~WalletQmlController()
{
    if (m_handler_load_wallet) {
        m_handler_load_wallet->disconnect();
    }
    m_worker_thread->quit();
    m_worker_thread->wait();
    delete m_worker;
}

void WalletQmlController::setSelectedWallet(QString path)
{
    QTimer::singleShot(0, m_worker, [this, path = path.toStdString()]() {
        std::vector<bilingual_str> warning_message;
        auto wallet{m_node.walletLoader().loadWallet(path, warning_message)};
        if (wallet.has_value()) {
            auto wallet_model = new WalletQmlModel(std::move(wallet.value()));
            wallet_model->moveToThread(this->thread());
            {
                QMutexLocker locker(&m_wallets_mutex);
                m_selected_wallet = wallet_model;
                m_wallets.push_back(m_selected_wallet);
            }
            Q_EMIT selectedWalletChanged();
        }
    });
}

WalletQmlModel* WalletQmlController::selectedWallet() const
{
    return m_selected_wallet;
}

void WalletQmlController::unloadWallets()
{
    m_handler_load_wallet->disconnect();
    QMutexLocker locker(&m_wallets_mutex);
    for (WalletQmlModel* wallet : m_wallets) {
        delete wallet;
    }
    m_wallets.clear();
}

void WalletQmlController::handleLoadWallet(std::unique_ptr<interfaces::Wallet> wallet)
{
    {
        QMutexLocker locker(&m_wallets_mutex);
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
    }
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
