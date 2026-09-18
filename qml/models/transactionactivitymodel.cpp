// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/transactionactivitymodel.h>

#include <qml/bitcoinunits.h>
#include <qml/models/transactionflow.h>
#include <qml/models/walletqmlmodel.h>

#include <core_io.h>

#include <QDateTime>

namespace {
using Type = TransactionActivity::Type;
using Direction = TransactionAction::Direction;

int LegacyType(Type type)
{
    switch (type) {
    case Type::Send: return Transaction::SendToAddress;
    case Type::Receive: return Transaction::RecvWithAddress;
    case Type::Mined: return Transaction::Generated;
    case Type::Consolidation:
    case Type::Split:
    case Type::InternalTransfer: return Transaction::SendToSelf;
    default: return Transaction::Other;
    }
}

} // namespace

TransactionActivityModel::TransactionActivityModel(WalletQmlModel* wallet_model)
    : QAbstractListModel(wallet_model), m_wallet_model(wallet_model), m_display_unit(wallet_model->displayUnit())
{
    connect(wallet_model, &WalletQmlModel::transactionChanged, this, &TransactionActivityModel::updateTransaction);
    connect(wallet_model, &WalletQmlModel::addressListChanged, this, [this] {
        for (auto& record : m_transactions) updateLabels(record);
        rebuildRows();
    });
    auto* requests = wallet_model->receiveRequests();
    connect(requests, &QAbstractItemModel::modelReset, this, &TransactionActivityModel::rebuildRows);
    connect(requests, &QAbstractItemModel::rowsInserted, this, &TransactionActivityModel::rebuildRows);
    connect(requests, &QAbstractItemModel::rowsRemoved, this, &TransactionActivityModel::rebuildRows);
    connect(requests, &QAbstractItemModel::dataChanged, this, &TransactionActivityModel::rebuildRows);
    connect(wallet_model, &WalletQmlModel::displayUnitChanged, this, &TransactionActivityModel::setDisplayUnit);
    connect(wallet_model, &WalletQmlModel::walletUnloaded, &m_timer, &QTimer::stop);
    connect(&m_timer, &QTimer::timeout, this, &TransactionActivityModel::poll);
    reload();
    if (wallet_model->wallet()) m_timer.start(1000);
}

int TransactionActivityModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QVariant TransactionActivityModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.model() != this || index.column() != 0 || index.row() < 0 || index.row() >= m_rows.size()) return {};
    return m_rows.at(index.row()).value(role);
}

QHash<int, QByteArray> TransactionActivityModel::roleNames() const
{
    return {
        {AddressRole, "address"}, {AmountRole, "amount"},
        {DateTimeRole, "date"}, {DepthRole, "depth"},
        {LabelRole, "label"}, {StatusRole, "status"},
        {TypeRole, "type"}, {TxidRole, "txid"},
        {CanBumpRole, "canBump"}, {ReplacesTxidRole, "replacesTxid"},
        {ReplacedByTxidRole, "replacedByTxid"}, {TimestampRole, "timestamp"},
        {IsPendingRequestRole, "isPendingRequest"}, {RequestIdRole, "requestId"},
        {NetAmountSatRole, "netAmountSat"},
        {IdRole, "activityId"}, {ActivityTypeRole, "activityType"},
        {ActionsRole, "actions"}, {ActionCountRole, "actionCount"},
        {WalletDebitSatRole, "walletDebitSat"}, {WalletCreditSatRole, "walletCreditSat"},
        {FeeSatRole, "feeSat"}, {FeeKnownRole, "feeKnown"},
        {StatusKnownRole, "statusKnown"}, {IsPendingRole, "isPending"},
        {IsInactiveRole, "isInactive"}, {PaymentRequestsRole, "paymentRequests"},
        {HasPaymentRequestRole, "hasPaymentRequest"}, {SearchTextRole, "searchText"},
        {BlocksToMaturityRole, "blocksToMaturity"},
    };
}

void TransactionActivityModel::updateLabels(Record& record)
{
    record.labels.clear();
    for (const auto& action : record.activity.actions) {
        record.labels.append(m_wallet_model->getAddressLabel(action.address));
    }
}

bool TransactionActivityModel::updateStatus(Record& record)
{
    interfaces::WalletTxStatus status{};
    int blocks{0};
    int64_t block_time{0};
    const uint256 txid{uint256::FromHex(record.activity.txid.toStdString()).value()};
    if (!m_wallet_model->tryGetTxStatus(txid, status, blocks, block_time)) return false;

    record.status_known = true;
    record.depth = status.depth_in_main_chain;
    record.blocks_to_maturity = status.blocks_to_maturity;
    record.block_height = record.depth > 0 ? status.block_height : 0;
    if (record.activity.type == Type::Mined) {
        record.status = !status.is_in_main_chain ? Transaction::NotAccepted
            : status.blocks_to_maturity > 0 ? Transaction::Immature : Transaction::Confirmed;
    } else {
        record.status = record.depth < 0 ? Transaction::Conflicted
            : status.is_abandoned ? Transaction::Abandoned
            : record.depth == 0 ? Transaction::Unconfirmed
            : record.depth < 6 ? Transaction::Confirming : Transaction::Confirmed;
    }
    record.can_bump = record.status == Transaction::Unconfirmed && record.activity.replaced_by_txid.isEmpty()
        && m_wallet_model->canBumpTransaction(txid);
    return true;
}

void TransactionActivityModel::reload()
{
    m_detail_txid.clear();
    m_detail_flow.clear();
    QMap<QString, Record> records;
    m_retry.clear();
    for (const auto& wtx : m_wallet_model->getWalletTxs()) {
        auto activity = TransactionActivity::fromWalletTx(wtx);
        if (!activity) continue;
        Record record = m_transactions.value(activity->txid);
        record.activity = std::move(*activity);
        updateLabels(record);
        if (!updateStatus(record)) m_retry.insert(record.activity.txid);
        records.insert(record.activity.txid, std::move(record));
    }
    m_transactions = std::move(records);
    rebuildRows();
}

void TransactionActivityModel::updateTransaction(const QString& txid, int change)
{
    m_detail_txid.clear();
    m_detail_flow.clear();
    m_retry.remove(txid);
    if (change == CT_DELETED) {
        m_transactions.remove(txid);
    } else {
        // The Core callback only queues work; wallet reads happen here on the
        // model thread. Refresh the complete snapshot so an existing
        // transaction can acquire replacement metadata too.
        const auto hash = uint256::FromHex(txid.toStdString());
        if (!hash) return;
        auto activity = TransactionActivity::fromWalletTx(m_wallet_model->getWalletTx(*hash));
        if (!activity) {
            Q_EMIT transactionDetailsChanged();
            return;
        }
        Record record = m_transactions.value(txid);
        record.activity = std::move(*activity);
        updateLabels(record);
        if (!updateStatus(record)) m_retry.insert(txid);
        m_transactions.insert(txid, std::move(record));
    }
    rebuildRows();
}

void TransactionActivityModel::refreshStatuses()
{
    for (auto& record : m_transactions) {
        if (updateStatus(record)) m_retry.remove(record.activity.txid);
        else m_retry.insert(record.activity.txid);
    }
    rebuildRows();
}

void TransactionActivityModel::poll()
{
    // Wallet-processed block hash detects reorgs even at the same height. Only
    // reread every status when this tip changes, not on every role read/tick.
    interfaces::WalletBalances balances{};
    uint256 tip;
    if (m_wallet_model->wallet()->tryGetBalances(balances, tip) && (!m_last_tip || *m_last_tip != tip)) {
        m_last_tip = tip;
        refreshStatuses();
    } else if (!m_retry.isEmpty()) {
        const auto retry = m_retry;
        for (const auto& txid : retry) {
            auto it = m_transactions.find(txid);
            if (it != m_transactions.end() && updateStatus(it.value())) m_retry.remove(txid);
        }
        rebuildRows();
    }
}

QString TransactionActivityModel::formatAmount(CAmount amount, bool receive) const
{
    const auto unit = QmlBitcoinUnits::fromDisplayUnit(m_display_unit);
    return QmlBitcoinUnits::format(unit, qAbs(amount), receive) + QLatin1Char(' ')
        + QmlBitcoinUnits::label(unit);
}

void TransactionActivityModel::setDisplayUnit(int unit)
{
    if (m_display_unit == unit) return;
    m_display_unit = unit;
    rebuildRows();
}

void TransactionActivityModel::rebuildRows()
{
    QMap<QString, QVariantList> requests_by_address;
    auto* history = m_wallet_model->receiveRequests();
    for (int i = 0; i < history->rowCount(); ++i) {
        const QString address = history->index(i).data(ReceiveRequestHistoryModel::AddressRole).toString();
        if (!requests_by_address.contains(address)) requests_by_address.insert(address, history->matchingEntriesForAddress(address));
    }

    QMap<QString, Row> rows;
    QSet<QString> fulfilled;
    for (const auto& record : m_transactions) {
        const auto& tx = record.activity;
        // Replacement metadata is historical. An original transaction that
        // subsequently confirms is active even if the marker is still stored.
        const bool replaced = !tx.replaced_by_txid.isEmpty() && (!record.status_known || record.depth <= 0);
        const bool inactive = replaced || (record.status_known &&
            (record.status == Transaction::Conflicted || record.status == Transaction::Abandoned || record.status == Transaction::NotAccepted));
        QVariantList actions;
        QVariantList requests;
        QSet<QString> request_ids;
        QStringList search{tx.txid};
        for (size_t i = 0; i < tx.actions.size(); ++i) {
            const auto& action = tx.actions[i];
            const bool receive = action.direction == Direction::Receive;
            // Associate requests with owned outputs, never an external send or
            // the aggregate wallet-input contribution of a mixed transaction.
            const QVariantList matches = action.output_index >= 0 && action.direction != Direction::Send && action.amount > 0
                ? requests_by_address.value(action.address) : QVariantList{};
            // A request's public Name is not a private activity note. An
            // associated request owns the note, including an intentionally
            // empty one; never fall back to its name/address-book label.
            const QString label = !matches.isEmpty() ? matches.first().toMap().value("noteSelf").toString()
                : action.direction == Direction::Send ? record.labels.at(i) : QString{};
            for (const auto& match : matches) {
                const auto request = match.toMap();
                const QString id = request.value("requestId").toString();
                if (!request_ids.contains(id)) {
                    request_ids.insert(id);
                    requests.append(match);
                    search << request.value("label").toString() << request.value("message").toString()
                           << request.value("noteSelf").toString();
                }
                if (record.status_known && !inactive) fulfilled.insert(id);
            }
            QVariantList inputs;
            for (const auto& input : action.inputs) {
                inputs.append(QVariantMap{{"txid", QString::fromStdString(input.hash.ToString())}, {"outputIndex", input.n}});
            }
            actions.append(QVariantMap{
                {"actionId", action.id}, {"direction", static_cast<int>(action.direction)},
                {"source", static_cast<int>(action.source)}, {"outputIndex", action.output_index},
                {"inputs", inputs}, {"address", action.address}, {"label", label},
                {"amountSat", qint64(action.amount)}, {"amount", formatAmount(action.amount, receive)},
                {"paymentRequests", matches}, {"hasPaymentRequest", !matches.isEmpty()},
            });
            search << action.address << label << record.labels.at(i);
        }
        const bool single = actions.size() == 1;
        const QVariantMap first = single ? actions.first().toMap() : QVariantMap{};
        const QString label = single ? first.value("label").toString() : QString{};
        search << label;
        const QString id = QStringLiteral("tx:") + tx.txid;
        rows.insert(id, {
            {IdRole, id}, {TxidRole, tx.txid}, {LabelRole, label},
            {AddressRole, single ? first.value("address") : QVariant{QString{}}},
            {TimestampRole, tx.timestamp},
            {DateTimeRole, QDateTime::fromSecsSinceEpoch(tx.timestamp)},
            {NetAmountSatRole, qint64(tx.walletImpact())},
            {AmountRole, formatAmount(tx.walletImpact(), tx.walletImpact() > 0)},
            {TypeRole, LegacyType(tx.type)}, {ActivityTypeRole, static_cast<int>(tx.type)},
            {DepthRole, record.depth}, {StatusRole, record.status},
            {StatusKnownRole, record.status_known}, {BlocksToMaturityRole, record.blocks_to_maturity},
            {CanBumpRole, record.can_bump && !inactive}, {IsInactiveRole, inactive},
            {ReplacesTxidRole, tx.replaces_txid}, {ReplacedByTxidRole, tx.replaced_by_txid},
            {IsPendingRole, record.status_known && !inactive && record.depth == 0},
            {IsPendingRequestRole, false}, {RequestIdRole, QString{}},
            {WalletDebitSatRole, qint64(tx.wallet_debit)}, {WalletCreditSatRole, qint64(tx.wallet_credit)},
            {FeeKnownRole, tx.fee.has_value()}, {FeeSatRole, tx.fee ? QVariant{qint64(*tx.fee)} : QVariant{}},
            {ActionsRole, actions}, {ActionCountRole, actions.size()},
            {PaymentRequestsRole, requests}, {HasPaymentRequestRole, !requests.isEmpty()},
            {SearchTextRole, search.join(QLatin1Char('\n'))},
        });
    }
    for (const auto& matches : requests_by_address) {
        for (const auto& match : matches) {
            const auto request = match.toMap();
            const QString request_id = request.value("requestId").toString();
            if (fulfilled.contains(request_id)) continue;
            const QString id = QStringLiteral("request:") + request_id;
            const QString address = request.value("address").toString();
            const QString label = request.value("noteSelf").toString();
            const auto date = QDateTime::fromString(request.value("dateIso").toString(), Qt::ISODate);
            const qint64 amount = request.value("amountSat").toLongLong();
            rows.insert(id, {
                {IdRole, id}, {TxidRole, QString{}}, {RequestIdRole, request_id},
                {AddressRole, address}, {LabelRole, label},
                {TimestampRole, date.toSecsSinceEpoch()}, {DateTimeRole, date.toLocalTime()},
                {AmountRole, formatAmount(amount, false)}, {NetAmountSatRole, amount},
                {IsPendingRequestRole, true}, {IsPendingRole, true}, {IsInactiveRole, false},
                {TypeRole, int(Transaction::RecvWithAddress)}, {ActivityTypeRole, int(Receive)},
                {DepthRole, 0}, {StatusRole, int(Transaction::Unconfirmed)}, {StatusKnownRole, false},
                {CanBumpRole, false}, {FeeKnownRole, false},
                {BlocksToMaturityRole, 0},
                {ReplacesTxidRole, QString{}}, {ReplacedByTxidRole, QString{}},
                {ActionsRole, QVariantList{}}, {ActionCountRole, 0},
                {PaymentRequestsRole, QVariantList{match}}, {HasPaymentRequestRole, true},
                {SearchTextRole, QStringList{address, label, request.value("label").toString(), request.value("message").toString()}.join(QLatin1Char('\n'))},
            });
        }
    }

    // Reconcile stable IDs without resetting the view on each confirmation or
    // request edit. Source order is stable; the proxy owns chronology/sections.
    const int old_count = m_rows.size();
    int old_requests{0};
    for (const auto& row : m_rows) old_requests += row.value(IsPendingRequestRole).toBool();
    int first_changed{-1}, last_changed{-1};
    QList<int> changed_roles;
    const auto flush_changes = [&] {
        if (first_changed < 0) return;
        Q_EMIT dataChanged(index(first_changed), index(last_changed), changed_roles);
        first_changed = last_changed = -1;
        changed_roles.clear();
    };
    int i{0};
    for (auto it = rows.cbegin(); it != rows.cend();) {
        int remove_end = i;
        while (remove_end < m_rows.size() && m_rows[remove_end].value(IdRole).toString() < it.key()) ++remove_end;
        if (remove_end > i) {
            flush_changes();
            beginRemoveRows({}, i, remove_end - 1);
            m_rows.erase(m_rows.begin() + i, m_rows.begin() + remove_end);
            endRemoveRows();
        }
        if (i == m_rows.size() || m_rows[i].value(IdRole).toString() != it.key()) {
            flush_changes();
            auto end = it;
            while (end != rows.cend() && (i == m_rows.size() || end.key() < m_rows[i].value(IdRole).toString())) ++end;
            const int added = std::distance(it, end);
            beginInsertRows({}, i, i + added - 1);
            m_rows.insert(i, added, Row{});
            while (it != end) m_rows[i++] = (it++).value();
            endInsertRows();
            continue;
        }
        if (m_rows[i] != it.value()) {
            for (auto role = it->cbegin(); role != it->cend(); ++role) {
                if (m_rows[i].value(role.key()) != role.value() && !changed_roles.contains(role.key())) changed_roles.append(role.key());
            }
            for (auto role = m_rows[i].cbegin(); role != m_rows[i].cend(); ++role) {
                if (!it->contains(role.key()) && !changed_roles.contains(role.key())) changed_roles.append(role.key());
            }
            m_rows[i] = it.value();
            if (first_changed < 0) first_changed = i;
            last_changed = i;
        }
        ++i;
        ++it;
    }
    flush_changes();
    if (i < m_rows.size()) {
        beginRemoveRows({}, i, m_rows.size() - 1);
        m_rows.erase(m_rows.begin() + i, m_rows.end());
        endRemoveRows();
    }
    if (old_count != rowCount() || old_requests != requestCount()) Q_EMIT countChanged();
    Q_EMIT transactionDetailsChanged();
}

QVariantMap TransactionActivityModel::transactionDetails(const QString& txid, bool include_flow) const
{
    const auto names = roleNames();
    for (const auto& row : m_rows) {
        if (row.value(TxidRole).toString() != txid || row.value(IsPendingRequestRole).toBool()) continue;
        QVariantMap result;
        for (auto it = names.cbegin(); it != names.cend(); ++it) result.insert(QString::fromUtf8(it.value()), row.value(it.key()));
        const auto& record = m_transactions[txid];
        result.insert("blockHeight", record.status_known && record.depth > 0 ? QVariant{record.block_height} : QVariant{});
        if (!include_flow) return result;
        if (m_detail_txid != txid) {
            const auto hash = uint256::FromHex(txid.toStdString());
            if (!hash || !m_wallet_model->wallet()) return result;
            const auto wtx = m_wallet_model->getWalletTx(*hash);
            if (!wtx.tx) return result;
            // Resolve only from local wallet history. This also works for spent
            // and conflicted parents; never query an external block explorer.
            std::map<Txid, CTransactionRef> parents;
            std::vector<std::optional<CTxOut>> prevouts;
            if (!wtx.tx->IsCoinBase()) {
                for (const auto& input : wtx.tx->vin) {
                    auto [it, inserted] = parents.try_emplace(input.prevout.hash);
                    if (inserted) it->second = m_wallet_model->wallet()->getTx(input.prevout.hash);
                    prevouts.push_back(it->second && input.prevout.n < it->second->vout.size()
                        ? std::optional{it->second->vout[input.prevout.n]} : std::nullopt);
                }
            }
            m_detail_flow = BuildTransactionFlow(wtx, prevouts);
            m_detail_raw_transaction = QString::fromStdString(EncodeHexTx(*wtx.tx));
            m_detail_txid = txid;
        }
        QVariantMap flow = m_detail_flow;
        for (const QString& side : {QStringLiteral("inputs"), QStringLiteral("outputs")}) {
            QVariantList entries;
            for (const auto& value : flow.value(side).toList()) {
                QVariantMap entry = value.toMap();
                const QString address = entry.value("address").toString();
                const bool output = side == "outputs";
                const bool mine = entry.value("ownership") == "wallet";
                const auto requests = output && mine && entry.value("amountSat").toLongLong() > 0
                    ? m_wallet_model->receiveRequests()->matchingEntriesForAddress(address) : QVariantList{};
                entry.insert("paymentRequests", requests);
                entry.insert("label", !requests.isEmpty() ? requests.first().toMap().value("noteSelf").toString()
                    : !output || !mine ? m_wallet_model->getAddressLabel(address) : QString{});
                entry.insert("amount", entry.value("amountKnown").toBool()
                    ? formatAmount(entry.value("amountSat").toLongLong(), false) : QString{});
                entries.append(entry);
            }
            flow.insert(side, entries);
        }
        const bool fee_known = flow.value("feeKnown").toBool();
        const qint64 vsize = flow.value("virtualSize").toLongLong();
        flow.insert("feeAmount", fee_known ? formatAmount(flow.value("feeSat").toLongLong(), false) : QString{});
        result.insert("flow", flow);
        result.insert("rawTransaction", m_detail_raw_transaction);
        result.insert("virtualSize", vsize);
        result.insert("size", flow.value("size"));
        result.insert("weight", flow.value("weight"));
        result.insert("version", flow.value("version"));
        result.insert("lockTime", flow.value("lockTime"));
        result.insert("feeKnown", fee_known);
        result.insert("feeSat", flow.value("feeSat"));
        result.insert("feeRateSatPerVb", fee_known && vsize > 0
            ? QVariant{double(flow.value("feeSat").toLongLong()) / vsize} : QVariant{});
        result.insert("signalsRbf", flow.value("signalsRbf"));
        return result;
    }
    return {};
}

QString TransactionActivityModel::rawTransaction(const QString& txid) const
{
    if (!m_transactions.contains(txid)) return {};
    const auto hash = uint256::FromHex(txid.toStdString());
    if (!hash) return {};
    const auto wtx = m_wallet_model->getWalletTx(*hash);
    return wtx.tx ? QString::fromStdString(EncodeHexTx(*wtx.tx)) : QString{};
}

QString TransactionActivityModel::paymentRequestUri(const QString& request_id) const
{
    const auto entry = m_wallet_model->receiveRequests()->entryById(request_id);
    if (!entry) return {};
    return ReceiveRequestHistoryModel::BuildBitcoinUri(QString::fromStdString(entry->recipient.address),
        entry->recipient.amount, QString::fromStdString(entry->recipient.label), QString::fromStdString(entry->recipient.message));
}
