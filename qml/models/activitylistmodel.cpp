// Copyright (c) 2024-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/activitylistmodel.h>

#include <qml/models/receiverequesthistorymodel.h>
#include <qml/models/walletqmlmodel.h>

#include <QDateTime>
#include <QVariantList>

#include <algorithm>

ActivityListModel::ActivityListModel(WalletQmlModel *parent)
    : QAbstractListModel(parent)
    , m_wallet_model(parent)
{
    if (m_wallet_model != nullptr) {
        refreshWallet();
        subscribeToCoreSignals();
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
        return tx->prettyAmount(m_display_unit);
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
        return tx->isPendingRequest ? QString{} : tx->txid;
    case CanBumpRole:
        return m_wallet_model ? m_wallet_model->canBumpTransaction(tx->hash) : false;
    case ReplacesTxidRole:
        return tx->replacesTxid;
    case ReplacedByTxidRole:
        return tx->replacedByTxid;
    case TimestampRole:
        return tx->time;
    case IsPendingRequestRole:
        return tx->isPendingRequest;
    case RequestIdRole:
        return tx->requestId;
    case NetAmountSatRole:
        return QVariant::fromValue<qlonglong>(tx->netAmount());
    case OutputIndexRole:
        return tx->idx;
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
    roles[TimestampRole] = "timestamp";
    roles[IsPendingRequestRole] = "isPendingRequest";
    roles[RequestIdRole] = "requestId";
    roles[NetAmountSatRole] = "netAmountSat";
    roles[OutputIndexRole] = "outputIndex";
    return roles;
}

void ActivityListModel::reload()
{
    beginResetModel();
    m_transactions.clear();
    refreshWallet();
    endResetModel();
}

QVariantMap ActivityListModel::firstTransactionDetails(const QString& txid) const
{
    QSharedPointer<Transaction> first;
    for (const auto& tx : m_transactions) {
        if (tx->txid != txid) {
            continue;
        }
        const bool prefer_outgoing_tie = first && tx->idx == first->idx
            && tx->debit < 0 && first->debit >= 0;
        if (!first || tx->idx < first->idx || prefer_outgoing_tie) {
            first = tx;
        }
    }
    return transactionDetails(first);
}

QVariantMap ActivityListModel::transactionDetails(const QString& txid, int output_index) const
{
    QSharedPointer<Transaction> match;
    for (const auto& tx : m_transactions) {
        if (tx->txid != txid || tx->idx != output_index) {
            continue;
        }
        if (!match || (tx->debit < 0 && match->debit >= 0)) {
            match = tx;
        }
    }
    return transactionDetails(match);
}

QVariantMap ActivityListModel::transactionDetails(const QSharedPointer<Transaction>& tx) const
{
    if (!tx) {
        return {};
    }
    updateTransactionStatus(tx);
    updateTransactionLabel(tx);
    const QVariantList payment_requests = m_wallet_model && m_wallet_model->receiveRequests()
        ? m_wallet_model->receiveRequests()->matchingEntriesForAddress(tx->address)
        : QVariantList{};
    return {
        {"txid", tx->txid},
        {"outputIndex", tx->idx},
        {"canBump", m_wallet_model ? m_wallet_model->canBumpTransaction(tx->hash) : false},
        {"replacedByTxid", tx->replacedByTxid},
        {"amount", tx->prettyAmount(m_display_unit)},
        {"date", tx->dateTimeString()},
        {"depth", tx->depth},
        {"type", tx->type},
        {"status", tx->status},
        {"address", tx->address},
        {"label", tx->label},
        {"paymentRequests", payment_requests}
    };
}

void ActivityListModel::setDisplayUnit(int unit)
{
    if (unit != m_display_unit) {
        m_display_unit = unit;
        if (!m_transactions.isEmpty()) {
            Q_EMIT dataChanged(index(0), index(m_transactions.size() - 1), {AmountRole});
        }
    }
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
    std::sort(m_transactions.begin(), m_transactions.end(), transactionSortsBefore);

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
        QString reqId = history->data(idx, ReceiveRequestHistoryModel::IdRole).toString();

        addReceiveRequest(address, label, amount, timestamp, reqId);
    }
}

void ActivityListModel::addReceiveRequest(const QString& address, const QString& label,
                                          CAmount amount, qint64 timestamp, const QString& requestId)
{
    uint256 zero_hash;
    auto tx = QSharedPointer<Transaction>::create(zero_hash, timestamp,
        Transaction::RecvWithAddress, address, CAmount{0}, amount);
    tx->label = label.isEmpty() ? tr("Payment request") : label;
    tx->status = Transaction::Unconfirmed;
    tx->isPendingRequest = true;
    tx->requestId = requestId;

    const int row = sortedInsertPosition(tx);
    beginInsertRows(QModelIndex(), row, row);
    m_transactions.insert(row, tx);
    m_pending_request_addresses.insert(address);
    endInsertRows();
    Q_EMIT countChanged();
}

void ActivityListModel::updateReceiveRequest(const QString& requestId, const QString& label, CAmount amount)
{
    for (int i = 0; i < m_transactions.size(); ++i) {
        if (m_transactions[i]->isPendingRequest && m_transactions[i]->requestId == requestId) {
            m_transactions[i]->label = label.isEmpty() ? tr("Payment request") : label;
            m_transactions[i]->credit = amount;
            Q_EMIT dataChanged(index(i), index(i));
            return;
        }
    }
}

void ActivityListModel::removePendingReceiveRequest(const QString& requestId)
{
    for (int i = 0; i < m_transactions.size(); ++i) {
        if (!m_transactions[i]->isPendingRequest || m_transactions[i]->requestId != requestId) {
            continue;
        }

        const QString address = m_transactions[i]->address;
        beginRemoveRows(QModelIndex(), i, i);
        m_transactions.removeAt(i);
        endRemoveRows();

        const bool address_still_pending = std::any_of(m_transactions.cbegin(), m_transactions.cend(),
            [&address](const QSharedPointer<Transaction>& tx) {
                return tx->isPendingRequest && tx->address == address;
            });
        if (!address_still_pending) {
            m_pending_request_addresses.remove(address);
        }

        Q_EMIT countChanged();
        return;
    }
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
                const int row = sortedInsertPosition(tx);
                beginInsertRows(QModelIndex(), row, row);
                m_transactions.insert(row, tx);
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
    repositionTransaction(index);
}

bool ActivityListModel::transactionSortsBefore(const QSharedPointer<Transaction>& a,
                                               const QSharedPointer<Transaction>& b)
{
    // Newest first. Ties are broken deterministically so that a live insert
    // and a full refresh produce the same row order (#848).
    if (a->time != b->time) return a->time > b->time;
    if (a->isPendingRequest != b->isPendingRequest) return a->isPendingRequest;
    if (a->txid != b->txid) return a->txid < b->txid;
    if (a->idx != b->idx) return a->idx < b->idx;
    return a->requestId < b->requestId;
}

int ActivityListModel::sortedInsertPosition(const QSharedPointer<Transaction>& tx) const
{
    const auto it = std::lower_bound(m_transactions.cbegin(), m_transactions.cend(),
                                     tx, transactionSortsBefore);
    return std::distance(m_transactions.cbegin(), it);
}

void ActivityListModel::repositionTransaction(int index)
{
    const QSharedPointer<Transaction> tx = m_transactions.at(index);
    int target = 0;
    for (int i = 0; i < m_transactions.size(); ++i) {
        if (i == index) continue;
        if (transactionSortsBefore(m_transactions.at(i), tx)) ++target;
    }
    const int destination = target > index ? target + 1 : target;
    if (destination == index || destination == index + 1) return;
    beginMoveRows(QModelIndex(), index, index, QModelIndex(), destination);
    m_transactions.move(index, target);
    endMoveRows();
}

void ActivityListModel::subscribeToCoreSignals()
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
