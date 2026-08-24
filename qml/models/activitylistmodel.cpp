// Copyright (c) 2024-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/activitylistmodel.h>

#include <qml/models/receiverequesthistorymodel.h>
#include <qml/models/walletqmlmodel.h>

#include <QDateTime>
#include <QTimer>
#include <QVariantList>

#include <algorithm>

namespace {
// Row dates are rendered as "N minutes ago" relative to the moment the row is
// read, so without a periodic re-read they keep whatever age they had when the
// view last asked. A minute is the smallest unit the string distinguishes, but
// refreshing once a minute would leave a row reading "0 minutes ago" for nearly
// two: the sweep runs on its own phase, unrelated to when any row was created.
// Sampling several times a minute bounds that lag without costing anything,
// since the sweep is a signal with no wallet work behind it and the view only
// re-reads the delegates it has realized.
constexpr int DATE_REFRESH_INTERVAL_MS{20 * 1000};
// A status read normally only loses the wallet lock race for a moment,
// so a short fixed cadence is enough for the refresh sweep's follow-up.
constexpr int STATUS_RETRY_INTERVAL_MS{250};
constexpr int MAX_STATUS_RETRY_INTERVAL_MS{2000};
// The wallet is normally only a moment behind, so the first retry comes
// quickly and then backs off: a wallet still behind after this is rescanning
// or otherwise busy rather than mid-block, and the next tip refreshes it
// anyway. Each retry re-reads every row, so the budget stays small.
constexpr int MAX_STATUS_RETRIES{6};
} // namespace

ActivityListModel::ActivityListModel(WalletQmlModel *parent)
    : QAbstractListModel(parent)
    , m_wallet_model(parent)
{
    if (m_wallet_model != nullptr) {
        refreshWallet();
        subscribeToCoreSignals();
        connect(m_wallet_model, &WalletQmlModel::addressListChanged,
                this, &ActivityListModel::refreshLabels);

        m_date_refresh_timer = new QTimer(this);
        m_date_refresh_timer->setInterval(DATE_REFRESH_INTERVAL_MS);
        connect(m_date_refresh_timer, &QTimer::timeout, this, &ActivityListModel::refreshDates);
        m_date_refresh_timer->start();
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

bool ActivityListModel::updateTransactionStatus(QSharedPointer<Transaction> tx, int* wallet_height) const
{
    if (m_wallet_model == nullptr || tx->isPendingRequest) {
        return true;
    }
    interfaces::WalletTxStatus wtx;
    int num_blocks;
    int64_t block_time;
    // tryGetTxStatus fails on wallet lock contention as well as for a
    // missing transaction, so keep the cached status rather than
    // downgrading the row; the next refresh picks up the real state
    // (the Widgets TransactionTablePriv does the same).
    if (m_wallet_model->tryGetTxStatus(tx->hash, wtx, num_blocks, block_time)) {
        tx->updateStatus(wtx, num_blocks, block_time);
        if (wallet_height != nullptr) *wallet_height = num_blocks;
        return true;
    }
    return false;
}

void ActivityListModel::updateTransactionLabel(QSharedPointer<Transaction> tx) const
{
    // Pending receive requests carry their own label, set when the request is
    // saved and refreshed when it is edited. Re-reading the address book here
    // would overwrite an edited request label with the older stored address
    // label, so leave pending requests untouched.
    if (m_wallet_model == nullptr || tx->isPendingRequest) {
        return;
    }

    tx->label = m_wallet_model->getAddressLabel(tx->address);
}

QVariant ActivityListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_transactions.size())
        return QVariant();

    QSharedPointer<Transaction> tx = m_transactions.at(index.row());

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
        return tx->label;
    case StatusRole:
        return static_cast<int>(tx->status);
    case TypeRole:
        return static_cast<int>(tx->type);
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
    case IsUsedAddressRequestRole:
        return tx->isUsedAddressRequest;
    case CountsForBalanceRole:
        return tx->countsForBalance;
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
    roles[IsUsedAddressRequestRole] = "isUsedAddressRequest";
    roles[CountsForBalanceRole] = "countsForBalance";
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
        {"type", static_cast<int>(tx->type)},
        {"status", static_cast<int>(tx->status)},
        {"countsForBalance", tx->countsForBalance},
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

void ActivityListModel::refreshStatuses(int chain_height)
{
    if (m_transactions.isEmpty()) {
        return;
    }
    // The latest announced tip is the height every read chases, kept in a
    // member so a tip that arrives while a retry is already pending is not
    // lost to the older target the retry was scheduled for. A genuinely
    // new tip is what clears the attempt budget.
    if (chain_height >= 0 && chain_height != m_status_target_height) {
        m_status_target_height = chain_height;
        m_status_retry_attempts = 0;
    }

    bool all_read{true};
    int wallet_height{-1};
    for (const auto& tx : m_transactions) {
        all_read = updateTransactionStatus(tx, &wallet_height) && all_read;
    }
    Q_EMIT dataChanged(index(0), index(m_transactions.size() - 1),
                       {StatusRole, DepthRole, DateTimeRole, CanBumpRole, CountsForBalanceRole});

    // Two ways a refresh leaves a row stale. A read lost to wallet lock
    // contention keeps its cached status above, and a read taken while the
    // wallet's height differs from the announced tip reports a depth from
    // the wrong block: tryGetTxStatus answers with the wallet's own height,
    // which trails the node's tip until the wallet catches up, and after a
    // tip disconnect sits above it. Neither re-reads itself, since data()
    // is a pure read, so a row would keep the wrong confirmation count
    // until the next block. The Widgets table avoids both by comparing
    // each row against the wallet's last processed block on every paint.
    const bool out_of_sync{m_status_target_height >= 0 && wallet_height >= 0 &&
                           wallet_height != m_status_target_height};
    if (!all_read || out_of_sync) {
        if (!m_status_retry_scheduled && m_status_retry_attempts < MAX_STATUS_RETRIES) {
            m_status_retry_scheduled = true;
            const int delay{std::min(STATUS_RETRY_INTERVAL_MS << m_status_retry_attempts,
                                     MAX_STATUS_RETRY_INTERVAL_MS)};
            ++m_status_retry_attempts;
            // The pending retry reads the member rather than capturing the
            // height, so it always chases the newest announced tip.
            QTimer::singleShot(delay, this, [this] {
                m_status_retry_scheduled = false;
                refreshStatuses(m_status_target_height);
            });
        }
        return;
    }
    m_status_retry_attempts = 0;
}

void ActivityListModel::refreshLabels()
{
    if (m_transactions.isEmpty()) {
        return;
    }
    for (const auto& tx : m_transactions) {
        // Pending request rows carry the request's label, kept in sync by
        // updateReceiveRequest; only wallet transactions follow the address book.
        if (tx->isPendingRequest) {
            continue;
        }
        updateTransactionLabel(tx);
    }
    Q_EMIT dataChanged(index(0), index(m_transactions.size() - 1), {LabelRole});
}

void ActivityListModel::refreshDates()
{
    if (m_transactions.isEmpty()) {
        return;
    }
    // Only the rendered string ages; the underlying timestamps do not change,
    // so re-emitting the date role is all the view needs.
    Q_EMIT dataChanged(index(0), index(m_transactions.size() - 1), {DateTimeRole});
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
            updateTransactionLabel(transaction);
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
        if (address.isEmpty()) continue;

        QString label = history->data(idx, ReceiveRequestHistoryModel::LabelRole).toString();
        CAmount amount = history->data(idx, ReceiveRequestHistoryModel::AmountSatRole).toLongLong();
        QString dateIso = history->data(idx, ReceiveRequestHistoryModel::DateIsoRole).toString();
        qint64 timestamp = QDateTime::fromString(dateIso, Qt::ISODate).toSecsSinceEpoch();
        QString reqId = history->data(idx, ReceiveRequestHistoryModel::IdRole).toString();

        // A request whose address already has a real transaction is still
        // materialized (used_address), but the proxy shows it only under the
        // Payment request filter so it does not duplicate the real row.
        const bool used_address = existing_addresses.contains(address);
        addReceiveRequest(address, label, amount, timestamp, reqId, used_address);
    }
}

void ActivityListModel::addReceiveRequest(const QString& address, const QString& label,
                                          CAmount amount, qint64 timestamp, const QString& requestId,
                                          bool used_address)
{
    uint256 zero_hash;
    auto tx = QSharedPointer<Transaction>::create(zero_hash, timestamp,
        Transaction::RecvWithAddress, address, CAmount{0}, amount);
    tx->label = label.isEmpty() ? tr("Payment request") : label;
    tx->status = Transaction::Unconfirmed;
    tx->isPendingRequest = true;
    tx->isUsedAddressRequest = used_address;
    tx->requestId = requestId;

    const int row = sortedInsertPosition(tx);
    beginInsertRows(QModelIndex(), row, row);
    m_transactions.insert(row, tx);
    // A used-address request has no unfulfilled payment to wait for, so it is
    // not tracked for fulfillment; only unused pending requests promote to a
    // real row when a matching transaction arrives.
    if (!used_address) {
        m_pending_request_addresses.insert(address);
    }
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
    // One wallet transaction can back several rows (a multi-recipient
    // send, a self-payment's send and receive parts), and no row re-reads
    // the wallet on its own, so a change has to refresh every one of them.
    bool found{false};
    for (int i = 0; i < m_transactions.size(); ++i) {
        if (m_transactions.at(i)->hash != hash) continue;
        found = true;
        m_transactions.at(i)->updateStatus(tx_status, num_blocks, block_time);
        Q_EMIT dataChanged(index(i), index(i));
    }

    if (!found) {
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
                updateTransactionLabel(tx);
                const int row = sortedInsertPosition(tx);
                beginInsertRows(QModelIndex(), row, row);
                m_transactions.insert(row, tx);
                endInsertRows();
            }
        }
    }
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
    const QString address = m_transactions.at(index)->address;

    // The request's address now has a real transaction. Rather than consuming
    // a request in place, keep it as a used-address request (surfaced only
    // under the Payment request filter) and insert the transaction as its own
    // row, so the live list matches the state rebuilt on reload. Core supports
    // multiple receive requests per address, so mark every request for the
    // address, not just the row that triggered the match: a single marked row
    // would strand its siblings as pending for the rest of the session while
    // a reload marks them all used.
    for (int i = 0; i < m_transactions.size(); ++i) {
        if (m_transactions[i]->isPendingRequest && m_transactions[i]->address == address &&
            !m_transactions[i]->isUsedAddressRequest) {
            m_transactions[i]->isUsedAddressRequest = true;
            Q_EMIT dataChanged(this->index(i), this->index(i));
        }
    }
    m_pending_request_addresses.remove(address);

    // The inserted row must carry its address book label like any other
    // insert: data() no longer reads the wallet, so a row inserted without
    // the label shows the bare address until something else refreshes it,
    // diverging from what a reload rebuilds.
    updateTransactionLabel(real_tx);
    const int row = sortedInsertPosition(real_tx);
    beginInsertRows(QModelIndex(), row, row);
    m_transactions.insert(row, real_tx);
    endInsertRows();
    Q_EMIT countChanged();
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

bool ActivityListModel::rowSortsBefore(int lhs_row, int rhs_row) const
{
    return transactionSortsBefore(m_transactions.at(lhs_row), m_transactions.at(rhs_row));
}

int ActivityListModel::sortedInsertPosition(const QSharedPointer<Transaction>& tx) const
{
    const auto it = std::lower_bound(m_transactions.cbegin(), m_transactions.cend(),
                                     tx, transactionSortsBefore);
    return std::distance(m_transactions.cbegin(), it);
}

void ActivityListModel::subscribeToCoreSignals()
{
    // Connect signals to wallet. The callback fires on the wallet's
    // notification thread.
    m_handler_transaction_changed = m_wallet_model->handleTransactionChanged([this](const uint256& hash, ChangeType status) {
        // Read the status here, on the wallet's notification thread: it already
        // holds cs_wallet, so the try-lock inside tryGetTxStatus is guaranteed
        // to succeed. Deferred to the GUI thread it can lose the try-lock while
        // the wallet is still processing the block and silently drop the update.
        interfaces::WalletTxStatus wtx;
        int num_blocks;
        int64_t block_time;
        if (!m_wallet_model->tryGetTxStatus(hash, wtx, num_blocks, block_time)) {
            return;
        }
        // Apply the update on the thread the model (and its attached proxy and
        // views) lives on: emitting row signals from the notification thread
        // corrupts the proxy's row mapping. This is the same marshalling the
        // Widgets TransactionTableModel does for this notification.
        QMetaObject::invokeMethod(this, [this, hash, wtx, num_blocks, block_time] {
            updateTransaction(hash, wtx, num_blocks, block_time);
        }, Qt::QueuedConnection);
    });
}

void ActivityListModel::unsubscribeFromCoreSignals()
{
    // Disconnect signals from wallet
    if (m_handler_transaction_changed) {
        m_handler_transaction_changed->disconnect();
    }
}
