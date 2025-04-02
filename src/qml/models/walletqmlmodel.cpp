// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletqmlmodel.h>

#include <qml/models/activitylistmodel.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/walletqmlmodeltransaction.h>

#include <consensus/amount.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <outputtype.h>
#include <qt/bitcoinunits.h>
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>

#include <QTimer>

WalletQmlModel::WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, QObject *parent)
    : QObject(parent)
{
    m_wallet = std::move(wallet);
    m_activity_list_model = new ActivityListModel(this);
    m_coins_list_model = new CoinsListModel(this);
    m_current_recipient = new SendRecipient(this);
    m_send_recipients = new SendRecipientsListModel(this);
}

WalletQmlModel::WalletQmlModel(QObject* parent)
    : QObject(parent)
{
    m_activity_list_model = new ActivityListModel(this);
    m_coins_list_model = new CoinsListModel(this);
    m_current_recipient = new SendRecipient(this);
    m_send_recipients = new SendRecipientsListModel(this);
}

WalletQmlModel::~WalletQmlModel()
{
    delete m_activity_list_model;
    delete m_coins_list_model;
    delete m_current_recipient;
    delete m_send_recipients;
    if (m_current_transaction) {
        delete m_current_transaction;
    }
}

QString WalletQmlModel::balance() const
{
    if (!m_wallet) {
        return "0";
    }
    return BitcoinUnits::format(BitcoinUnits::Unit::BTC, m_wallet->getBalance());
}

QString WalletQmlModel::name() const
{
    if (!m_wallet) {
        return QString();
    }
    return QString::fromStdString(m_wallet->getWalletName());
}

std::set<interfaces::WalletTx> WalletQmlModel::getWalletTxs() const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->getWalletTxs();
}

interfaces::WalletTx WalletQmlModel::getWalletTx(const uint256& hash) const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->getWalletTx(hash);
}

bool WalletQmlModel::tryGetTxStatus(const uint256& txid,
                                    interfaces::WalletTxStatus& tx_status,
                                    int& num_blocks,
                                    int64_t& block_time) const
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->tryGetTxStatus(txid, tx_status, num_blocks, block_time);
}

std::unique_ptr<interfaces::Handler> WalletQmlModel::handleTransactionChanged(TransactionChangedFn fn)
{
    if (!m_wallet) {
        return nullptr;
    }
    return m_wallet->handleTransactionChanged(fn);
}

bool WalletQmlModel::prepareTransaction()
{
    if (!m_wallet || !m_current_recipient) {
        return false;
    }

    CScript scriptPubKey = GetScriptForDestination(DecodeDestination(m_current_recipient->address().toStdString()));
    wallet::CRecipient recipient = {scriptPubKey, m_current_recipient->cAmount(), m_current_recipient->subtractFeeFromAmount()};
    m_coin_control.m_feerate = CFeeRate(1000);

    CAmount balance = m_wallet->getBalance();
    if (balance < recipient.nAmount) {
        return false;
    }

    std::vector<wallet::CRecipient> vecSend{recipient};
    int nChangePosRet = -1;
    CAmount nFeeRequired = 0;
    const auto& res = m_wallet->createTransaction(vecSend, m_coin_control, true, nChangePosRet, nFeeRequired);
    if (res) {
        if (m_current_transaction) {
            delete m_current_transaction;
        }
        CTransactionRef newTx = *res;
        m_current_transaction = new WalletQmlModelTransaction(m_current_recipient, this);
        m_current_transaction->setWtx(newTx);
        m_current_transaction->setTransactionFee(nFeeRequired);
        Q_EMIT currentTransactionChanged();
        return true;
    } else {
        return false;
    }
}

void WalletQmlModel::sendTransaction()
{
    if (!m_wallet || !m_current_transaction) {
        return;
    }

    CTransactionRef newTx = m_current_transaction->getWtx();
    if (!newTx) {
        return;
    }

    interfaces::WalletValueMap value_map;
    interfaces::WalletOrderForm order_form;
    m_wallet->commitTransaction(newTx, value_map, order_form);
}

interfaces::Wallet::CoinsList WalletQmlModel::listCoins() const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->listCoins();
}

bool WalletQmlModel::lockCoin(const COutPoint& output)
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->lockCoin(output, true);
}

bool WalletQmlModel::unlockCoin(const COutPoint& output)
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->unlockCoin(output);
}

bool WalletQmlModel::isLockedCoin(const COutPoint& output)
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->isLockedCoin(output);
}

void WalletQmlModel::listLockedCoins(std::vector<COutPoint>& outputs)
{
    if (!m_wallet) {
        return;
    }
    m_wallet->listLockedCoins(outputs);
}

void WalletQmlModel::selectCoin(const COutPoint& output)
{
    m_coin_control.Select(output);
}

void WalletQmlModel::unselectCoin(const COutPoint& output)
{
    m_coin_control.UnSelect(output);
}

bool WalletQmlModel::isSelectedCoin(const COutPoint& output)
{
    return m_coin_control.IsSelected(output);
}

std::vector<COutPoint> WalletQmlModel::listSelectedCoins() const
{
    return m_coin_control.ListSelected();
}

int WalletQmlModel::recipientIndex() const
{
    return 1;
}

int WalletQmlModel::recipientsCount() const
{
    return 1;
}
