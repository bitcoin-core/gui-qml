// Copyright (c) 2024-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/activitylistmodel.h>

#include <qml/models/receiverequesthistorymodel.h>
#include <qml/models/walletqmlmodel.h>

ActivityListModel::ActivityListModel(WalletQmlModel *parent)
    : QAbstractListModel(parent)
    , m_wallet_model(parent)
{
    if (m_wallet_model != nullptr) {
        refreshWallet();
        subsctribeToCoreSignals();
    }
}

ActivityListModel::~ActivityListModel()
{
    unsubscribeFromCoreSignals();
}

int ActivityListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return m_transactions.size();
}

void ActivityListModel::updateTransactionStatus(QSharedPointer<Transaction> tx) const
{
    if (m_wallet_model == nullptr || tx->isPendingRequest) {
        return;
    }
    interfaces::WalletTxStatus wtx;
    int num_blocks;
    int64_t block_time;
    if (m_wallet_model->tryGetTxStatus(tx->hash, wtx, num_blocks, block_time)) {
        tx->updateStatus(wtx, num_blocks, block_time);
    } else {
        tx->status = Transaction::Status::NotAccepted;
    }
}

void ActivityListModel::updateTransactionLabel(QSharedPointer<Transaction> tx) const
{
    if (m_wallet_model == nullptr) {
        return;
    }

    tx->label = m_wallet_model->getAddressLabel(tx->address);
}

QVariant ActivityListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_transactions.size())
        return QVariant();

    QSharedPointer<Transaction> tx = m_transactions.at(index.row());
    updateTransactionStatus(tx);

    switch (role) {
    case AddressRole:
        return tx->address;
    case AmountRole:
        return tx->prettyAmount();
    case DateTimeRole:
        return tx->dateTimeString();
    case DepthRole:
        return tx->depth;
    case LabelRole:
        updateTransactionLabel(tx);
        return tx->label;
    case StatusRole:
        return tx->status;
    case TypeRole:
        return tx->type;
    case TxidRole:
        return tx->txid;
    case CanBumpRole:
        return m_wallet_model ? m_wallet_model->canBumpTransaction(tx->hash) : false;
    case ReplacesTxidRole:
        return tx->replacesTxid;
    case ReplacedByTxidRole:
        return tx->replacedByTxid;
    case IsPendingRequestRole:
        return tx->isPendingRequest;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ActivityListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[AddressRole] = "address";
    roles[AmountRole] = "amount";
    roles[DateTimeRole] = "date";
    roles[DepthRole] = "depth";
    roles[LabelRole] = "label";
    roles[StatusRole] = "status";
    roles[TypeRole] = "type";
    roles[TxidRole] = "txid";
    roles[CanBumpRole] = "canBump";
    roles[ReplacesTxidRole] = "replacesTxid";
    roles[ReplacedByTxidRole] = "replacedByTxid";
    roles[IsPendingRequestRole] = "isPendingRequest";
    return roles;
}

void ActivityListModel::reload()
{
    beginResetModel();
    m_transactions.clear();
    refreshWallet();
    endResetModel();
}

QVariantMap ActivityListModel::transactionDetails(const QString& txid) const
{
    for (const auto& tx : m_transactions) {
        if (tx->txid == txid) {
            updateTransactionStatus(tx);
            updateTransactionLabel(tx);
            return {
                {"txid", tx->txid},
                {"canBump", m_wallet_model ? m_wallet_model->canBumpTransaction(tx->hash) : false},
                {"replacedByTxid", tx->replacedByTxid},
                {"amount", tx->prettyAmount()},
                {"date", tx->dateTimeString()},
                {"depth", tx->depth},
                {"type", tx->type},
                {"status", tx->status},
                {"address", tx->address},
                {"label", tx->label}
            };
        }
    }
    return {};
}

void ActivityListModel::refreshWallet()
{
    if (m_wallet_model == nullptr) {
        return;
    }
    for (const auto &tx : m_wallet_model->getWalletTxs()) {
        auto transactions = Transaction::fromWalletTx(tx);
        m_transactions.append(transactions);
        for (const auto &transaction : transactions) {
            updateTransactionStatus(transaction);
        }
    }
    std::sort(m_transactions.begin(), m_transactions.end(),
              [](const QSharedPointer<Transaction> &a, const QSharedPointer<Transaction> &b) {
                  return a->depth < b->depth;
              });

    addPendingReceiveRequests();
}

void ActivityListModel::addPendingReceiveRequests()
{
    if (m_wallet_model == nullptr) return;

    ReceiveRequestHistoryModel* history = m_wallet_model->receiveRequests();
    if (!history) return;

    QSet<QString> existing_addresses;
    for (const auto& tx : m_transactions) {
        if (!tx->address.isEmpty()) {
            existing_addresses.insert(tx->address);
        }
    }

    for (int i = 0; i < history->rowCount(); ++i) {
        QModelIndex idx = history->index(i);
        QString address = history->data(idx, ReceiveRequestHistoryModel::AddressRole).toString();
        if (address.isEmpty() || existing_addresses.contains(address)) continue;

        QString label = history->data(idx, ReceiveRequestHistoryModel::LabelRole).toString();
        CAmount amount = history->data(idx, ReceiveRequestHistoryModel::AmountSatRole).toLongLong();
        QString dateIso = history->data(idx, ReceiveRequestHistoryModel::DateIsoRole).toString();
        qint64 timestamp = QDateTime::fromString(dateIso, Qt::ISODate).toSecsSinceEpoch();

        addReceiveRequest(address, label, amount, timestamp);
    }
}

void ActivityListModel::addReceiveRequest(const QString& address, const QString& label,
                                          CAmount amount, qint64 timestamp)
{
    uint256 zero_hash;
    auto tx = QSharedPointer<Transaction>::create(zero_hash, timestamp,
        Transaction::RecvWithAddress, address, CAmount{0}, amount);
    tx->label = label.isEmpty() ? QStringLiteral("Payment request") : label;
    tx->status = Transaction::Unconfirmed;
    tx->isPendingRequest = true;

    beginInsertRows(QModelIndex(), 0, 0);
    m_transactions.push_front(tx);
    m_pending_request_addresses.insert(address);
    endInsertRows();
}

void ActivityListModel::removePendingRequestForAddress(const QString& address)
{
    if (!m_pending_request_addresses.contains(address)) return;

    for (int i = 0; i < m_transactions.size(); ++i) {
        if (m_transactions[i]->isPendingRequest && m_transactions[i]->address == address) {
            beginRemoveRows(QModelIndex(), i, i);
            m_transactions.removeAt(i);
            endRemoveRows();
            break;
        }
    }
    m_pending_request_addresses.remove(address);
}

void ActivityListModel::updateTransaction(const uint256& hash, const interfaces::WalletTxStatus& tx_status, int num_blocks, int64_t block_time)
{
    int index = findTransactionIndex(hash);

    if (index != -1) {
        QSharedPointer<Transaction> tx = m_transactions.at(index);
        tx->updateStatus(tx_status, num_blocks, block_time);
        Q_EMIT dataChanged(this->index(index), this->index(index));
    } else {
        // new transaction
        interfaces::WalletTx wtx = m_wallet_model->getWalletTx(hash);
        auto transactions = Transaction::fromWalletTx(wtx);
        if (transactions.isEmpty()) {
            return;
        }
        for (const auto& tx : transactions) {
            tx->updateStatus(tx_status, num_blocks, block_time);
            int pendingIdx = findPendingRequestIndex(tx->address);
            if (pendingIdx != -1) {
                fulfillPendingRequest(pendingIdx, tx);
            } else {
                beginInsertRows(QModelIndex(), 0, 0);
                m_transactions.push_front(tx);
                endInsertRows();
            }
        }
    }
}


int ActivityListModel::findTransactionIndex(const uint256& hash) const
{
    auto it = std::find_if(m_transactions.begin(), m_transactions.end(),
                           [&hash](const QSharedPointer<Transaction>& tx) {
                               return tx->hash == hash;
                           });
    if (it != m_transactions.end()) {
        return std::distance(m_transactions.begin(), it);
    }
    return -1;
}

int ActivityListModel::findPendingRequestIndex(const QString& address) const
{
    if (!m_pending_request_addresses.contains(address)) return -1;

    for (int i = 0; i < m_transactions.size(); ++i) {
        if (m_transactions[i]->isPendingRequest && m_transactions[i]->address == address) {
            return i;
        }
    }
    return -1;
}

void ActivityListModel::fulfillPendingRequest(int index, const QSharedPointer<Transaction>& real_tx)
{
    QSharedPointer<Transaction> pending = m_transactions.at(index);

    pending->hash = real_tx->hash;
    pending->status = real_tx->status;
    pending->depth = real_tx->depth;
    pending->time = real_tx->time;
    pending->credit = real_tx->credit;
    pending->debit = real_tx->debit;
    pending->type = real_tx->type;
    pending->idx = real_tx->idx;
    pending->txid = real_tx->txid;
    pending->countsForBalance = real_tx->countsForBalance;
    pending->involvesWatchAddress = real_tx->involvesWatchAddress;
    pending->isPendingRequest = false;
    if (!real_tx->label.isEmpty()) {
        pending->label = real_tx->label;
    }

    m_pending_request_addresses.remove(pending->address);

    Q_EMIT dataChanged(this->index(index), this->index(index));
}

void ActivityListModel::subsctribeToCoreSignals()
{
    // Connect signals to wallet
    m_handler_transaction_changed = m_wallet_model->handleTransactionChanged([this](const uint256& hash, ChangeType status) {
        interfaces::WalletTxStatus wtx;
        int num_blocks;
        int64_t block_time;
        if (m_wallet_model->tryGetTxStatus(hash, wtx, num_blocks, block_time)) {
            updateTransaction(hash, wtx, num_blocks, block_time);
        }
    });
}

void ActivityListModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    if (m_handler_transaction_changed) {
        m_handler_transaction_changed->disconnect();
    }
}
