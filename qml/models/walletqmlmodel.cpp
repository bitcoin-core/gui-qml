
// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletqmlmodel.h>

#include <qml/models/activitylistmodel.h>
#include <qml/models/paymentrequest.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/walletqmlmodeltransaction.h>

#include <consensus/amount.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <addresstype.h>
#include <outputtype.h>
#include <qml/bitcoinunits.h>
#include <serialize.h>
#include <streams.h>
#include <wallet/coincontrol.h>
#include <wallet/wallet.h>

#include <QDateTime>

namespace {
struct QmlReceiveRequestRecipient
{
    static constexpr int CURRENT_VERSION{1};
    int nVersion{CURRENT_VERSION};
    std::string address;
    std::string label;
    CAmount amount{0};
    std::string message;
    std::string sPaymentRequest;
    std::string authenticatedMerchant;

    SERIALIZE_METHODS(QmlReceiveRequestRecipient, obj)
    {
        READWRITE(obj.nVersion, obj.address, obj.label, obj.amount, obj.message, obj.sPaymentRequest, obj.authenticatedMerchant);
    }
};

struct QmlRecentRequestEntry
{
    static constexpr int CURRENT_VERSION{1};
    int nVersion{CURRENT_VERSION};
    int64_t id{0};
    QDateTime date;
    QmlReceiveRequestRecipient recipient;

    SERIALIZE_METHODS(QmlRecentRequestEntry, obj)
    {
        unsigned int date_timet;
        SER_WRITE(obj, date_timet = obj.date.toSecsSinceEpoch());
        READWRITE(obj.nVersion, obj.id, date_timet, obj.recipient);
        SER_READ(obj, obj.date = QDateTime::fromSecsSinceEpoch(date_timet));
    }
};
} // namespace

WalletQmlModel::WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, QObject *parent)
    : QObject(parent)
{
    m_wallet = std::move(wallet);
    m_activity_list_model = new ActivityListModel(this);
    m_coins_list_model = new CoinsListModel(this);
    m_send_recipients = new SendRecipientsListModel(this);
    m_current_payment_request = new PaymentRequest(this);
}

WalletQmlModel::WalletQmlModel(QObject* parent)
    : QObject(parent)
{
    m_activity_list_model = new ActivityListModel(this);
    m_coins_list_model = new CoinsListModel(this);
    m_send_recipients = new SendRecipientsListModel(this);
    m_current_payment_request = new PaymentRequest(this);
}

WalletQmlModel::~WalletQmlModel()
{
    delete m_activity_list_model;
    delete m_coins_list_model;
    delete m_send_recipients;
    delete m_current_payment_request;
    if (m_current_transaction) {
        delete m_current_transaction;
    }
}

QString WalletQmlModel::balance() const
{
    if (!m_wallet) {
        return "0";
    }
    return QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, m_wallet->getBalance());
}

CAmount WalletQmlModel::balanceSatoshi() const
{
    if (!m_wallet) {
        return 0;
    }
    return m_wallet->getBalance();
}

QString WalletQmlModel::name() const
{
    if (!m_wallet) {
        return QString();
    }
    return QString::fromStdString(m_wallet->getWalletName());
}

QString WalletQmlModel::newAddress(QString label)
{
    if (!m_wallet) {
        return QString();
    }
    OutputType output_type = m_wallet->getDefaultAddressType();
    util::Result<CTxDestination> dest{m_wallet->getNewDestination(output_type, label.toStdString())};
    return QString::fromStdString(EncodeDestination(dest.value()));
}

void WalletQmlModel::commitPaymentRequest()
{
    if (!m_wallet || !m_current_payment_request) {
        return;
    }

    if (m_current_payment_request->id().isEmpty()) {
        m_current_payment_request->setId(nextPaymentRequestId());
    }

    if (m_current_payment_request->address().isEmpty()) {
        const OutputType output_type = m_wallet->getDefaultAddressType();
        const auto destination{m_wallet->getNewDestination(output_type, m_current_payment_request->label().toStdString())};
        if (!destination) {
            return;
        }
        m_current_payment_request->setDestination(destination.value());
    }

    bool parse_ok{false};
    const int64_t request_id{m_current_payment_request->id().toLongLong(&parse_ok)};
    if (!parse_ok || request_id <= 0) {
        return;
    }

    QmlRecentRequestEntry request_entry;
    request_entry.id = request_id;
    request_entry.date = QDateTime::currentDateTime();
    request_entry.recipient.address = m_current_payment_request->address().toStdString();
    request_entry.recipient.label = m_current_payment_request->label().toStdString();
    request_entry.recipient.amount = m_current_payment_request->amount()->satoshi();
    request_entry.recipient.message = m_current_payment_request->message().toStdString();

    DataStream ss{};
    ss << request_entry;

    m_wallet->setAddressReceiveRequest(
        m_current_payment_request->destination(),
        m_current_payment_request->id().toStdString(),
        ss.str());
}

unsigned int WalletQmlModel::nextPaymentRequestId() const
{
    if (!m_wallet) {
        return 1;
    }

    int64_t max_id{0};
    for (const std::string& request : m_wallet->getAddressReceiveRequests()) {
        std::vector<uint8_t> data(request.begin(), request.end());
        DataStream ss{data};
        QmlRecentRequestEntry entry;
        try {
            ss >> entry;
        } catch (const std::ios_base::failure&) {
            continue;
        }
        if (entry.id > max_id) {
            max_id = entry.id;
        }
    }

    if (max_id <= 0 || max_id >= std::numeric_limits<unsigned int>::max() - 1) {
        return 1;
    }

    return static_cast<unsigned int>(max_id + 1);
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
    return m_wallet->getWalletTx(Txid::FromUint256(hash));
}

bool WalletQmlModel::tryGetTxStatus(const uint256& txid,
                                    interfaces::WalletTxStatus& tx_status,
                                    int& num_blocks,
                                    int64_t& block_time) const
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->tryGetTxStatus(Txid::FromUint256(txid), tx_status, num_blocks, block_time);
}

QString WalletQmlModel::getAddressLabel(const QString& address) const
{
    if (!m_wallet || address.isEmpty()) {
        return {};
    }

    const CTxDestination destination = DecodeDestination(address.toStdString());
    if (!IsValidDestination(destination)) {
        return {};
    }

    std::string label;
    if (!m_wallet->getAddress(destination, &label, nullptr)) {
        return {};
    }

    return QString::fromStdString(label);
}

std::unique_ptr<interfaces::Handler> WalletQmlModel::handleTransactionChanged(TransactionChangedFn fn)
{
    if (!m_wallet) {
        return nullptr;
    }
    // Convert the function to match the expected signature
    auto converted_fn = [fn](const Txid& txid, ChangeType change_type) {
        fn(txid.ToUint256(), change_type);
    };
    return m_wallet->handleTransactionChanged(converted_fn);
}

bool WalletQmlModel::prepareTransaction()
{
    if (!m_wallet || !m_send_recipients || m_send_recipients->recipients().empty()) {
        return false;
    }

    std::vector<wallet::CRecipient> vecSend;
    CAmount total = 0;
    for (auto* recipient : m_send_recipients->recipients()) {
        CTxDestination destination = DecodeDestination(recipient->address()->address().toStdString());
        wallet::CRecipient c_recipient = {destination, recipient->cAmount(), recipient->subtractFeeFromAmount()};
        m_coin_control.m_feerate = CFeeRate(1000);
        vecSend.push_back(c_recipient);
        total += recipient->cAmount();
    }

    CAmount balance = m_wallet->getBalance();
    if (balance < total) {
        return false;
    }

    const auto& res = m_wallet->createTransaction(vecSend, m_coin_control, true, std::nullopt);
    if (res) {
        if (m_current_transaction) {
            delete m_current_transaction;
        }
        CTransactionRef newTx = res->tx;
        m_current_transaction = new WalletQmlModelTransaction(m_send_recipients, this);
        m_current_transaction->setWtx(newTx);
        m_current_transaction->setTransactionFee(res->fee);
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

unsigned int WalletQmlModel::feeTargetBlocks() const
{
    return m_coin_control.m_confirm_target.value_or(wallet::DEFAULT_TX_CONFIRM_TARGET);
}

void WalletQmlModel::setFeeTargetBlocks(unsigned int target_blocks)
{
    if (m_coin_control.m_confirm_target != target_blocks) {
        m_coin_control.m_confirm_target = target_blocks;
        Q_EMIT feeTargetBlocksChanged();
    }
}
