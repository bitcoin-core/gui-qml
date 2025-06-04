// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletqmlcontroller.h>

#include <qml/models/walletqmlmodel.h>

#include <interfaces/node.h>
#include <support/allocators/secure.h>
#include <wallet/walletutil.h>
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
    if (!m_wallets.empty()) {
        for (WalletQmlModel* wallet : m_wallets) {
            if (wallet->name() == path) {
                m_selected_wallet = wallet;
                Q_EMIT selectedWalletChanged();
                return;
            }
        }
    }

    QTimer::singleShot(0, m_worker, [this, path = path.toStdString()]() {
        std::vector<bilingual_str> warning_message;
        auto wallet{m_node.walletLoader().loadWallet(path, warning_message)};
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

void WalletQmlController::createSingleSigWallet(const QString &name, const QString &passphrase)
{
    const SecureString secure_passphrase{passphrase.toStdString()};
    const std::string wallet_name{name.toStdString()};
    auto wallet{m_node.walletLoader().createWallet(wallet_name, secure_passphrase, wallet::WALLET_FLAG_DESCRIPTORS, m_warning_messages)};
    QMutexLocker locker(&m_wallets_mutex);
    if (wallet) {
        m_selected_wallet = new WalletQmlModel(std::move(*wallet));
        m_wallets.push_back(m_selected_wallet);
        Q_EMIT selectedWalletChanged();
    } else {
        m_error_message = util::ErrorString(wallet);
    }
}

void WalletQmlController::handleLoadWallet(std::unique_ptr<interfaces::Wallet> wallet)
{
    QMutexLocker locker(&m_wallets_mutex);
    if (!m_wallets.empty()) {
        QString name = QString::fromStdString(wallet->getWalletName());
        for (WalletQmlModel* wallet_model : m_wallets) {
            if (wallet_model->name() == name) {
                m_selected_wallet = wallet_model;
                Q_EMIT selectedWalletChanged();
                setWalletLoaded(true);
                return;
            }
        }
    }

    auto wallet_model = new WalletQmlModel(std::move(wallet));
    wallet_model->moveToThread(this->thread());
    m_selected_wallet = wallet_model;
    m_wallets.push_back(m_selected_wallet);
    Q_EMIT selectedWalletChanged();
    setWalletLoaded(true);
}

void WalletQmlController::initialize()
{
    m_handler_load_wallet = m_node.walletLoader().handleLoadWallet([this](std::unique_ptr<interfaces::Wallet> wallet) {
        handleLoadWallet(std::move(wallet));
    });

    auto wallets = m_node.walletLoader().getWallets();
    for (auto& wallet : wallets) {
        m_wallets.push_back(new WalletQmlModel(std::move(wallet)));
    }
    if (!m_wallets.empty()) {
        m_selected_wallet = m_wallets.front();
        setWalletLoaded(true);
        Q_EMIT selectedWalletChanged();
    }

    m_initialized = true;
    Q_EMIT initializedChanged();
}

void WalletQmlController::setWalletLoaded(bool loaded)
{
    if (m_is_wallet_loaded != loaded) {
        m_is_wallet_loaded = loaded;
        Q_EMIT isWalletLoadedChanged();
    }
}
