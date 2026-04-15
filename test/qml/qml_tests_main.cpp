// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtQuickTest/quicktest.h>

#include <QAbstractListModel>
#include <QQmlEngine>
#include <QQmlContext>
#include <QRegularExpression>
#include <QStringList>
#include <qqml.h>

#include <algorithm>
#include <vector>

#include <qml/components/blockclockdial.h>
#include <qml/controls/linegraph.h>

class MockAppMode : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isDesktop READ isDesktop WRITE setIsDesktop NOTIFY isDesktopChanged)
    Q_PROPERTY(bool walletEnabled READ walletEnabled WRITE setWalletEnabled NOTIFY walletEnabledChanged)
    Q_PROPERTY(QString state READ state WRITE setState NOTIFY stateChanged)

public:
    bool isDesktop() const { return m_is_desktop; }
    bool walletEnabled() const { return m_wallet_enabled; }
    QString state() const { return m_state; }

public Q_SLOTS:
    void setIsDesktop(const bool value)
    {
        if (m_is_desktop == value) return;
        m_is_desktop = value;
        Q_EMIT isDesktopChanged();
    }
    void setWalletEnabled(const bool value)
    {
        if (m_wallet_enabled == value) return;
        m_wallet_enabled = value;
        Q_EMIT walletEnabledChanged();
    }
    void setState(const QString& value)
    {
        if (m_state == value) return;
        m_state = value;
        Q_EMIT stateChanged();
    }

Q_SIGNALS:
    void isDesktopChanged();
    void walletEnabledChanged();
    void stateChanged();

private:
    bool m_is_desktop{true};
    bool m_wallet_enabled{true};
    QString m_state{QStringLiteral("desktop")};
};

class MockPeerDetailsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int nodeId MEMBER m_node_id CONSTANT)
    Q_PROPERTY(QString address MEMBER m_address CONSTANT)
    Q_PROPERTY(QString addressLocal MEMBER m_address_local CONSTANT)
    Q_PROPERTY(QString type MEMBER m_type CONSTANT)
    Q_PROPERTY(QString permission MEMBER m_permission CONSTANT)
    Q_PROPERTY(QString version MEMBER m_version CONSTANT)
    Q_PROPERTY(QString userAgent MEMBER m_user_agent CONSTANT)
    Q_PROPERTY(QString services MEMBER m_services CONSTANT)
    Q_PROPERTY(bool transactionRelay MEMBER m_transaction_relay CONSTANT)
    Q_PROPERTY(bool addressRelay MEMBER m_address_relay CONSTANT)
    Q_PROPERTY(QString mappedAS MEMBER m_mapped_as CONSTANT)
    Q_PROPERTY(QString startingHeight MEMBER m_starting_height CONSTANT)
    Q_PROPERTY(QString syncedHeaders MEMBER m_synced_headers CONSTANT)
    Q_PROPERTY(QString syncedBlocks MEMBER m_synced_blocks CONSTANT)
    Q_PROPERTY(QString direction MEMBER m_direction CONSTANT)
    Q_PROPERTY(QString connectionDuration MEMBER m_connection_duration CONSTANT)
    Q_PROPERTY(QString lastSend MEMBER m_last_send CONSTANT)
    Q_PROPERTY(QString lastReceived MEMBER m_last_received CONSTANT)
    Q_PROPERTY(QString bytesSent MEMBER m_bytes_sent CONSTANT)
    Q_PROPERTY(QString bytesReceived MEMBER m_bytes_received CONSTANT)
    Q_PROPERTY(QString pingTime MEMBER m_ping_time CONSTANT)
    Q_PROPERTY(QString pingWait MEMBER m_ping_wait CONSTANT)
    Q_PROPERTY(QString pingMin MEMBER m_ping_min CONSTANT)
    Q_PROPERTY(QString timeOffset MEMBER m_time_offset CONSTANT)

public:
    int m_node_id{7};
    QString m_address{QStringLiteral("127.0.0.1:8333")};
    QString m_address_local{QStringLiteral("127.0.0.1:18444")};
    QString m_type{QStringLiteral("Outbound Full Relay")};
    QString m_permission{QStringLiteral("N/A")};
    QString m_version{QStringLiteral("70016")};
    QString m_user_agent{QStringLiteral("/Satoshi:test/")};
    QString m_services{QStringLiteral("NETWORK|WITNESS")};
    bool m_transaction_relay{true};
    bool m_address_relay{false};
    QString m_mapped_as{QStringLiteral("N/A")};
    QString m_starting_height{QStringLiteral("100")};
    QString m_synced_headers{QStringLiteral("200")};
    QString m_synced_blocks{QStringLiteral("150")};
    QString m_direction{QStringLiteral("Inbound")};
    QString m_connection_duration{QStringLiteral("5 min")};
    QString m_last_send{QStringLiteral("2 s")};
    QString m_last_received{QStringLiteral("3 s")};
    QString m_bytes_sent{QStringLiteral("1.0 MiB")};
    QString m_bytes_received{QStringLiteral("2.0 MiB")};
    QString m_ping_time{QStringLiteral("10 ms")};
    QString m_ping_wait{QStringLiteral("N/A")};
    QString m_ping_min{QStringLiteral("7 ms")};
    QString m_time_offset{QStringLiteral("0 s")};

    Q_INVOKABLE void triggerDisconnected() { Q_EMIT disconnected(); }

Q_SIGNALS:
    void disconnected();
};

class MockBitcoinAmount : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString display MEMBER m_display NOTIFY displayChanged)
    Q_PROPERTY(Unit unit MEMBER m_unit NOTIFY unitChanged)
    Q_PROPERTY(QString unitLabel READ unitLabel NOTIFY unitChanged)

public:
    enum Unit {
        BTC,
        SAT
    };
    Q_ENUM(Unit)

    QString m_display{QStringLiteral("0.00000000")};
    Unit m_unit{BTC};

    QString unitLabel() const { return m_unit == BTC ? QStringLiteral("BTC") : QStringLiteral("sat"); }
    Q_INVOKABLE void format() {}
    Q_INVOKABLE void flipUnit()
    {
        m_unit = (m_unit == BTC) ? SAT : BTC;
        Q_EMIT unitChanged();
    }

Q_SIGNALS:
    void displayChanged();
    void unitChanged();
};

class MockBitcoinAddress : public QObject
{
    Q_OBJECT
};

class MockPaymentRequest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id MEMBER m_id NOTIFY idChanged)
    Q_PROPERTY(QObject* amount READ amount CONSTANT)
    Q_PROPERTY(QString amountError MEMBER m_amount_error NOTIFY amountErrorChanged)
    Q_PROPERTY(QString label MEMBER m_label NOTIFY labelChanged)
    Q_PROPERTY(QString message MEMBER m_message NOTIFY messageChanged)
    Q_PROPERTY(QString address MEMBER m_address NOTIFY addressChanged)
    Q_PROPERTY(QString addressFormatted READ addressFormatted NOTIFY addressChanged)

public:
    QString m_id;
    QString m_amount_error;
    QString m_label;
    QString m_message;
    QString m_address;
    MockBitcoinAmount m_amount{};

    QObject* amount() { return &m_amount; }
    QString addressFormatted() const { return m_address; }
    Q_INVOKABLE void clear()
    {
        m_id.clear();
        m_amount_error.clear();
        m_label.clear();
        m_message.clear();
        m_address.clear();
        m_amount.m_display = QStringLiteral("0.00000000");
        m_amount.m_unit = MockBitcoinAmount::BTC;
        Q_EMIT idChanged();
        Q_EMIT amountErrorChanged();
        Q_EMIT labelChanged();
        Q_EMIT messageChanged();
        Q_EMIT addressChanged();
    }

Q_SIGNALS:
    void idChanged();
    void amountErrorChanged();
    void labelChanged();
    void messageChanged();
    void addressChanged();
};

class MockTransaction : public QObject
{
    Q_OBJECT

public:
    enum Status {
        Unconfirmed = 0,
        Confirming = 1,
        Confirmed = 2
    };
    Q_ENUM(Status)

    enum Type {
        Other = 0,
        RecvWithAddress = 1,
        RecvFromOther = 2,
        SendToAddress = 3,
        SendToOther = 4,
        Generated = 5
    };
    Q_ENUM(Type)
};

class MockSendRecipient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address MEMBER m_address NOTIFY addressChanged)
    Q_PROPERTY(QString addressError MEMBER m_address_error NOTIFY addressErrorChanged)
    Q_PROPERTY(QObject* amount READ amount CONSTANT)
    Q_PROPERTY(QString amountError MEMBER m_amount_error NOTIFY amountErrorChanged)
    Q_PROPERTY(QString label MEMBER m_label NOTIFY labelChanged)
    Q_PROPERTY(bool subtractFeeFromAmount READ subtractFeeFromAmount WRITE setSubtractFeeFromAmount NOTIFY subtractFeeFromAmountChanged)
    Q_PROPERTY(bool isValid MEMBER m_is_valid NOTIFY isValidChanged)

public:
    QString m_address{QStringLiteral("bcrt1qsendtoaddress")};
    QString m_address_error;
    MockBitcoinAmount m_amount{};
    QString m_amount_error;
    QString m_label;
    bool m_subtract_fee_from_amount{false};
    bool m_is_valid{true};

    QObject* amount() { return &m_amount; }
    bool subtractFeeFromAmount() const { return m_subtract_fee_from_amount; }
    void setSubtractFeeFromAmount(bool value)
    {
        if (m_subtract_fee_from_amount == value) return;
        m_subtract_fee_from_amount = value;
        Q_EMIT subtractFeeFromAmountChanged();
    }

Q_SIGNALS:
    void addressChanged();
    void addressErrorChanged();
    void amountErrorChanged();
    void labelChanged();
    void subtractFeeFromAmountChanged();
    void isValidChanged();
};

class MockRecipientsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QObject* current READ current NOTIFY currentChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Roles {
        AddressRole = Qt::UserRole + 1,
        LabelRole,
        AmountRole
    };

    struct RecipientRow {
        QString address;
        QString label;
        QString amount;
    };

    MockRecipientsModel()
    {
        m_rows.push_back({
            QStringLiteral("bcrt1qsendtoaddress"),
            QStringLiteral("recipient-1"),
            QStringLiteral("0.01000000 BTC"),
        });
    }

    QObject* current() const { return m_current; }
    int currentIndex() const { return m_current_index; } // 1-based, matches QML expectations.
    int count() const { return static_cast<int>(m_rows.size()); }

    void setCurrent(QObject* recipient)
    {
        m_current = recipient;
        Q_EMIT currentChanged();
        Q_EMIT currentRecipientChanged();
    }

    void setCurrentIndex(int index)
    {
        const int bounded = std::clamp(index, 1, std::max(1, count()));
        if (m_current_index == bounded) return;
        m_current_index = bounded;
        Q_EMIT currentIndexChanged();
    }

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override
    {
        Q_UNUSED(parent);
        return count();
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
        const RecipientRow& row = m_rows.at(index.row());
        switch (role) {
        case AddressRole: return row.address;
        case LabelRole: return row.label;
        case AmountRole: return row.amount;
        default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {AddressRole, "address"},
            {LabelRole, "label"},
            {AmountRole, "amount"},
        };
    }

    Q_INVOKABLE void clearToFront()
    {
        beginResetModel();
        while (m_rows.size() > 1) {
            m_rows.pop_back();
        }
        endResetModel();
        m_current_index = 1;
        Q_EMIT countChanged();
        Q_EMIT currentIndexChanged();
        Q_EMIT listCleared();
    }

    Q_INVOKABLE void add()
    {
        const int row = count();
        beginInsertRows(QModelIndex{}, row, row);
        const int next = row + 1;
        m_rows.push_back({
            QStringLiteral("bcrt1qrecipient%1").arg(next),
            QStringLiteral("recipient-%1").arg(next),
            QStringLiteral("0.00%1 BTC").arg(next),
        });
        endInsertRows();
        m_current_index = count();
        Q_EMIT countChanged();
        Q_EMIT currentIndexChanged();
    }

    Q_INVOKABLE void prev() { setCurrentIndex(m_current_index - 1); }
    Q_INVOKABLE void next() { setCurrentIndex(m_current_index + 1); }

    Q_INVOKABLE void remove()
    {
        if (count() <= 1) return;
        const int row = count() - 1;
        beginRemoveRows(QModelIndex{}, row, row);
        m_rows.pop_back();
        endRemoveRows();
        if (m_current_index > count()) m_current_index = count();
        Q_EMIT countChanged();
        Q_EMIT currentIndexChanged();
    }

Q_SIGNALS:
    void currentChanged();
    void currentRecipientChanged();
    void currentIndexChanged();
    void countChanged();
    void listCleared();

private:
    QObject* m_current{nullptr};
    int m_current_index{1};
    std::vector<RecipientRow> m_rows{};
};

class MockCoinsListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int selectedCoinsCount READ selectedCoinsCount NOTIFY selectedCoinsCountChanged)
    Q_PROPERTY(int coinCount READ coinCount NOTIFY coinCountChanged)
    Q_PROPERTY(QString totalSelected READ totalSelected NOTIFY totalSelectedChanged)
    Q_PROPERTY(bool overRequiredAmount READ overRequiredAmount NOTIFY overRequiredAmountChanged)
    Q_PROPERTY(QString changeAmount READ changeAmount NOTIFY changeAmountChanged)

public:
    enum Roles {
        AddressRole = Qt::UserRole + 1,
        AmountRole,
        LabelRole,
        LockedRole,
        SelectedRole
    };

    struct CoinRow {
        QString address;
        QString amount;
        QString label;
        bool locked;
        bool selected;
    };

    MockCoinsListModel()
    {
        m_rows = {
            {QStringLiteral("bcrt1qcoin1"), QStringLiteral("0.00100000 BTC"), QStringLiteral("utxo-1"), false, false},
            {QStringLiteral("bcrt1qcoin2"), QStringLiteral("0.00200000 BTC"), QStringLiteral("utxo-2"), false, false},
            {QStringLiteral("bcrt1qcoin3"), QStringLiteral("0.00300000 BTC"), QStringLiteral(""), true, false},
        };
    }

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override
    {
        Q_UNUSED(parent);
        return static_cast<int>(m_rows.size());
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
        const CoinRow& row = m_rows.at(index.row());
        switch (role) {
        case AddressRole: return row.address;
        case AmountRole: return row.amount;
        case LabelRole: return row.label;
        case LockedRole: return row.locked;
        case SelectedRole: return row.selected;
        default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {AddressRole, "address"},
            {AmountRole, "amount"},
            {LabelRole, "label"},
            {LockedRole, "locked"},
            {SelectedRole, "selected"},
        };
    }

    int selectedCoinsCount() const
    {
        return std::count_if(m_rows.begin(), m_rows.end(), [](const CoinRow& row) { return row.selected; });
    }

    int coinCount() const { return rowCount(); }

    QString totalSelected() const
    {
        return QStringLiteral("%1 selected").arg(selectedCoinsCount());
    }

    bool overRequiredAmount() const { return selectedCoinsCount() > 1; }

    QString changeAmount() const
    {
        return overRequiredAmount() ? QStringLiteral("0.00050000 BTC") : QStringLiteral("0.00000000 BTC");
    }

    Q_INVOKABLE void toggleCoinSelection(int index)
    {
        if (index < 0 || index >= rowCount()) return;
        CoinRow& row = m_rows[static_cast<size_t>(index)];
        if (row.locked) return;
        row.selected = !row.selected;
        const QModelIndex model_index = createIndex(index, 0);
        Q_EMIT dataChanged(model_index, model_index, {SelectedRole});
        emitAggregateSignals();
    }

    Q_INVOKABLE void update() {}

Q_SIGNALS:
    void selectedCoinsCountChanged();
    void coinCountChanged();
    void totalSelectedChanged();
    void overRequiredAmountChanged();
    void changeAmountChanged();

private:
    void emitAggregateSignals()
    {
        Q_EMIT selectedCoinsCountChanged();
        Q_EMIT totalSelectedChanged();
        Q_EMIT overRequiredAmountChanged();
        Q_EMIT changeAmountChanged();
    }

    std::vector<CoinRow> m_rows{};
};

class MockWalletQmlModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name MEMBER m_name NOTIFY nameChanged)
    Q_PROPERTY(QString balance MEMBER m_balance NOTIFY balanceChanged)
    Q_PROPERTY(QObject* activityListModel READ activityListModel CONSTANT)
    Q_PROPERTY(QObject* bumpModel READ bumpModel CONSTANT)
    Q_PROPERTY(QObject* recipients READ recipients CONSTANT)
    Q_PROPERTY(QObject* coinsListModel READ coinsListModel CONSTANT)
    Q_PROPERTY(QObject* currentTransaction READ currentTransaction CONSTANT)
    Q_PROPERTY(QObject* currentPaymentRequest READ currentPaymentRequest CONSTANT)
    Q_PROPERTY(int targetBlocks READ targetBlocks WRITE setTargetBlocks NOTIFY targetBlocksChanged)
    Q_PROPERTY(QString estimatedFee READ estimatedFee NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(bool customFeeEnabled READ customFeeEnabled WRITE setCustomFeeEnabled NOTIFY customFeeEnabledChanged)
    Q_PROPERTY(QString customFeeRate READ customFeeRate WRITE setCustomFeeRate NOTIFY customFeeRateChanged)
    Q_PROPERTY(bool customFeeRateValid READ customFeeRateValid NOTIFY customFeeRateValidChanged)
    Q_PROPERTY(bool feeEstimatePending MEMBER m_fee_estimate_pending NOTIFY feeEstimatePendingChanged)
    Q_PROPERTY(int feeEstimateRevision MEMBER m_fee_estimate_revision NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(bool prepareTransactionResult MEMBER m_prepare_transaction_result NOTIFY prepareTransactionResultChanged)
    Q_PROPERTY(bool sendTransactionResult MEMBER m_send_transaction_result NOTIFY sendTransactionResultChanged)
    Q_PROPERTY(int prepareTransactionCalls READ prepareTransactionCalls NOTIFY prepareTransactionCallsChanged)
    Q_PROPERTY(int scheduleFeeEstimatesCalls READ scheduleFeeEstimatesCalls NOTIFY scheduleFeeEstimatesCallsChanged)
    Q_PROPERTY(int sendTransactionCalls READ sendTransactionCalls NOTIFY sendTransactionCallsChanged)
    Q_PROPERTY(bool isEncrypted MEMBER m_is_encrypted NOTIFY securityStateChanged)
    Q_PROPERTY(bool isLocked MEMBER m_is_locked NOTIFY securityStateChanged)
    Q_PROPERTY(QString transactionError MEMBER m_transaction_error NOTIFY transactionErrorChanged)
    Q_PROPERTY(bool transactionNeedsUnlock MEMBER m_transaction_needs_unlock NOTIFY transactionNeedsUnlockChanged)

public:
    QString m_name{QStringLiteral("testwallet")};
    QString m_balance{QStringLiteral("1.00000000 BTC")};
    QObject* m_activity_list_model{nullptr};
    QObject* m_bump_model{nullptr};
    QObject* m_recipients{nullptr};
    QObject* m_coins_list_model{nullptr};
    QObject* m_current_transaction{nullptr};
    QObject* m_current_payment_request{nullptr};
    int m_target_blocks{2};
    bool m_prepare_transaction_result{true};

    QObject* activityListModel() const { return m_activity_list_model; }
    QObject* bumpModel() const { return m_bump_model; }
    QObject* recipients() const { return m_recipients; }
    QObject* coinsListModel() const { return m_coins_list_model; }
    QObject* currentTransaction() const { return m_current_transaction; }
    QObject* currentPaymentRequest() const { return m_current_payment_request; }
    int targetBlocks() const { return m_target_blocks; }
    QString estimatedFee() const
    {
        return m_custom_fee_enabled ? m_custom_fee_estimate : estimatedFeeForTarget(m_target_blocks);
    }
    bool customFeeEnabled() const { return m_custom_fee_enabled; }
    QString customFeeRate() const { return m_custom_fee_rate; }
    bool customFeeRateValid() const
    {
        static const QRegularExpression pattern{
            QStringLiteral(R"(^[0-9]+(?:\.[0-9]{0,3})?$)")
        };
        return pattern.match(m_custom_fee_rate).hasMatch() && m_custom_fee_rate != QStringLiteral("0");
    }
    int prepareTransactionCalls() const { return m_prepare_transaction_calls; }
    int scheduleFeeEstimatesCalls() const { return m_schedule_fee_estimates_calls; }
    int sendTransactionCalls() const { return m_send_transaction_calls; }
    Q_INVOKABLE QString estimatedFeeForTarget(const int target) const
    {
        const QString estimate = m_fee_estimates.value(target);
        if (!estimate.isEmpty()) {
            return estimate;
        }

        return {};
    }
    Q_INVOKABLE int feeTargetIndex(const int target) const
    {
        switch (target) {
        case 1: return 0;
        case 6: return 2;
        case 2:
        default:
            return 1;
        }
    }
    void setActivityListModel(QObject* model) { m_activity_list_model = model; }
    void setBumpModel(QObject* model) { m_bump_model = model; }
    void setRecipients(QObject* model) { m_recipients = model; }
    void setCoinsListModel(QObject* model) { m_coins_list_model = model; }
    void setCurrentTransaction(QObject* transaction) { m_current_transaction = transaction; }
    void setCurrentPaymentRequest(QObject* request) { m_current_payment_request = request; }
    void setTargetBlocks(const int value)
    {
        if (m_target_blocks == value) return;
        m_target_blocks = value;
        ++m_fee_estimate_revision;
        Q_EMIT targetBlocksChanged();
        Q_EMIT feeEstimateRevisionChanged();
        scheduleFeeEstimates();
    }
    void setCustomFeeEnabled(const bool value)
    {
        if (m_custom_fee_enabled == value) return;
        m_custom_fee_enabled = value;
        ++m_fee_estimate_revision;
        Q_EMIT customFeeEnabledChanged();
        Q_EMIT feeEstimateRevisionChanged();
        scheduleFeeEstimates();
    }
    void setCustomFeeRate(const QString& value)
    {
        const bool was_valid = customFeeRateValid();
        if (m_custom_fee_rate == value) return;
        m_custom_fee_rate = value;
        m_custom_fee_estimate = customFeeRateValid() ? QStringLiteral("0.00000400 ₿") : QString{};
        ++m_fee_estimate_revision;
        Q_EMIT customFeeRateChanged();
        if (was_valid != customFeeRateValid()) {
            Q_EMIT customFeeRateValidChanged();
        }
        Q_EMIT feeEstimateRevisionChanged();
        scheduleFeeEstimates();
    }
    Q_INVOKABLE bool prepareTransaction()
    {
        ++m_prepare_transaction_calls;
        Q_EMIT prepareTransactionCallsChanged();
        if (m_prepare_transaction_result) {
            setTransactionStatus({}, false);
        } else {
            setTransactionStatus(QStringLiteral("Amount plus fee exceeds available balance"), false);
        }
        return m_prepare_transaction_result;
    }
    Q_INVOKABLE bool prepareTransactionWithPassphrase(const QString&)
    {
        return prepareTransaction();
    }
    Q_INVOKABLE void scheduleFeeEstimates()
    {
        ++m_schedule_fee_estimates_calls;
        Q_EMIT scheduleFeeEstimatesCallsChanged();
    }
    Q_INVOKABLE void setFeeEstimate(const int target, const QString& estimate)
    {
        if (estimate.isEmpty()) {
            m_fee_estimates.remove(target);
        } else {
            m_fee_estimates.insert(target, estimate);
        }
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }
    Q_INVOKABLE void clearFeeEstimates()
    {
        m_fee_estimates.clear();
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }
    Q_INVOKABLE bool sendTransaction()
    {
        ++m_send_transaction_calls;
        Q_EMIT sendTransactionCallsChanged();
        if (m_send_transaction_result) {
            setTransactionStatus({}, false);
        }
        return m_send_transaction_result;
    }
    Q_INVOKABLE void commitPaymentRequest()
    {
        auto* request = qobject_cast<MockPaymentRequest*>(m_current_payment_request);
        if (!request) return;
        request->m_id = QStringLiteral("1");
        request->m_address = QStringLiteral("bcrt1qrequestaddress0000000000000000000000");
        Q_EMIT request->idChanged();
        Q_EMIT request->addressChanged();
    }

Q_SIGNALS:
    void nameChanged();
    void balanceChanged();
    void targetBlocksChanged();
    void customFeeEnabledChanged();
    void customFeeRateChanged();
    void customFeeRateValidChanged();
    void feeEstimatePendingChanged();
    void feeEstimateRevisionChanged();
    void prepareTransactionResultChanged();
    void sendTransactionResultChanged();
    void prepareTransactionCallsChanged();
    void scheduleFeeEstimatesCallsChanged();
    void sendTransactionCallsChanged();
    void securityStateChanged();
    void transactionErrorChanged();
    void transactionNeedsUnlockChanged();

private:
    void setTransactionStatus(const QString& error, bool needs_unlock)
    {
        if (m_transaction_error != error) {
            m_transaction_error = error;
            Q_EMIT transactionErrorChanged();
        }
        if (m_transaction_needs_unlock != needs_unlock) {
            m_transaction_needs_unlock = needs_unlock;
            Q_EMIT transactionNeedsUnlockChanged();
        }
    }

    QHash<int, QString> m_fee_estimates{{1, QStringLiteral("0.00000750 ₿")}, {2, QStringLiteral("0.00000500 ₿")}, {6, QStringLiteral("0.00000250 ₿")}};
    bool m_custom_fee_enabled{false};
    QString m_custom_fee_rate;
    QString m_custom_fee_estimate;
    bool m_fee_estimate_pending{false};
    bool m_send_transaction_result{true};
    bool m_is_encrypted{false};
    bool m_is_locked{false};
    QString m_transaction_error;
    bool m_transaction_needs_unlock{false};
    int m_fee_estimate_revision{1};
    int m_prepare_transaction_calls{0};
    int m_schedule_fee_estimates_calls{0};
    int m_send_transaction_calls{0};
};

class MockWalletQmlModelTransaction : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address MEMBER m_address CONSTANT)
    Q_PROPERTY(QString label MEMBER m_label CONSTANT)
    Q_PROPERTY(QString amount MEMBER m_amount CONSTANT)
    Q_PROPERTY(QString fee MEMBER m_fee CONSTANT)
    Q_PROPERTY(QString total MEMBER m_total CONSTANT)

public:
    QString m_address{QStringLiteral("bcrt1qexampleaddress")};
    QString m_label{QStringLiteral("example-label")};
    QString m_amount{QStringLiteral("0.01000000 BTC")};
    QString m_fee{QStringLiteral("0.00001000 BTC")};
    QString m_total{QStringLiteral("0.01001000 BTC")};
};

class MockWalletController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool initialized MEMBER m_initialized NOTIFY initializedChanged)
    Q_PROPERTY(bool isWalletLoaded MEMBER m_is_wallet_loaded NOTIFY isWalletLoadedChanged)
    Q_PROPERTY(bool noWalletsFound MEMBER m_no_wallets_found NOTIFY noWalletsFoundChanged)
    Q_PROPERTY(QString lastSelectedWalletName READ lastSelectedWalletName NOTIFY lastSelectedWalletNameChanged)
    Q_PROPERTY(QString lastClosedWalletName READ lastClosedWalletName NOTIFY lastClosedWalletNameChanged)
    Q_PROPERTY(int closeWalletCalls READ closeWalletCalls NOTIFY closeWalletCallsChanged)
    Q_PROPERTY(QObject* selectedWallet READ selectedWallet NOTIFY selectedWalletChanged)

public:
    bool m_initialized{true};
    bool m_is_wallet_loaded{true};
    bool m_no_wallets_found{false};
    QObject* m_selected_wallet{nullptr};
    QString m_last_selected_wallet_name;
    QString m_last_closed_wallet_name;
    int m_close_wallet_calls{0};

    QObject* selectedWallet() const { return m_selected_wallet; }
    QString lastSelectedWalletName() const { return m_last_selected_wallet_name; }
    QString lastClosedWalletName() const { return m_last_closed_wallet_name; }
    int closeWalletCalls() const { return m_close_wallet_calls; }
    void setSelectedWalletObject(QObject* wallet)
    {
        if (m_selected_wallet == wallet) return;
        m_selected_wallet = wallet;
        Q_EMIT selectedWalletChanged();
    }
    Q_INVOKABLE void setSelectedWallet(const QString& name, const QString& format = QString())
    {
        Q_UNUSED(format);
        if (m_last_selected_wallet_name == name) return;
        m_last_selected_wallet_name = name;
        Q_EMIT lastSelectedWalletNameChanged();
        Q_EMIT selectedWalletChanged();
    }
    Q_INVOKABLE void closeWallet(const QString& name)
    {
        m_last_closed_wallet_name = name;
        ++m_close_wallet_calls;
        Q_EMIT lastClosedWalletNameChanged();
        Q_EMIT closeWalletCallsChanged();
    }
    Q_INVOKABLE void reset()
    {
        m_last_selected_wallet_name.clear();
        m_last_closed_wallet_name.clear();
        m_close_wallet_calls = 0;
        Q_EMIT lastSelectedWalletNameChanged();
        Q_EMIT lastClosedWalletNameChanged();
        Q_EMIT closeWalletCallsChanged();
    }

Q_SIGNALS:
    void initializedChanged();
    void isWalletLoadedChanged();
    void noWalletsFoundChanged();
    void lastSelectedWalletNameChanged();
    void lastClosedWalletNameChanged();
    void closeWalletCallsChanged();
    void selectedWalletChanged();
};

class MockOptionsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool listen MEMBER m_listen NOTIFY listenChanged)
    Q_PROPERTY(bool natpmp MEMBER m_natpmp NOTIFY natpmpChanged)
    Q_PROPERTY(bool server MEMBER m_server NOTIFY serverChanged)
    Q_PROPERTY(bool prune MEMBER m_prune NOTIFY pruneChanged)
    Q_PROPERTY(int pruneSizeGB MEMBER m_prune_size_gb NOTIFY pruneSizeGBChanged)
    Q_PROPERTY(QString dataDir MEMBER m_data_dir NOTIFY dataDirChanged)
    Q_PROPERTY(QString getDefaultDataDirString READ getDefaultDataDirString CONSTANT)
    Q_PROPERTY(int displayUnit READ displayUnit WRITE setDisplayUnit NOTIFY displayUnitChanged)
    Q_PROPERTY(QString displayUnitLabel READ displayUnitLabel NOTIFY displayUnitChanged)
    Q_PROPERTY(QString languageSummary READ languageSummary NOTIFY languageChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages CONSTANT)

public:
    bool m_listen{true};
    bool m_natpmp{false};
    bool m_server{false};
    bool m_prune{true};
    int m_prune_size_gb{2};
    QString m_data_dir{QStringLiteral("/tmp/bitcoin-default")};
    QString m_custom_data_dir{QStringLiteral("/tmp/bitcoin-custom")};

    QString getDefaultDataDirString() const { return QStringLiteral("/tmp/bitcoin-default"); }
    Q_INVOKABLE QString getCustomDataDirString() const { return m_custom_data_dir; }
    Q_INVOKABLE void setCustomDataDirString(const QString& dir) { m_custom_data_dir = dir; }
    Q_INVOKABLE void setCustomDataDirArgs(const QString& dir) { m_data_dir = dir; }
    Q_INVOKABLE void onboard() {}

    int displayUnit() const { return m_displayUnit; }
    void setDisplayUnit(int u) {
        if (u != m_displayUnit) { m_displayUnit = u; Q_EMIT displayUnitChanged(u); }
    }
    QString displayUnitLabel() const { return m_displayUnit == 1 ? "sat" : "BTC"; }
    Q_INVOKABLE QString displayUnitLabelForAmount(qint64 satoshi) const {
        if (m_displayUnit != 1) return QString("₿");
        return (qAbs(satoshi) == 1) ? QString("sat") : QString("sats");
    }
    QString language() const { return m_language; }
    void setLanguage(const QString& l) {
        if (l != m_language) { m_language = l; Q_EMIT languageChanged(); }
    }
    QString languageSummary() const { return m_language.isEmpty() ? "System default" : m_language; }
    QStringList availableLanguages() const { return {"", "de", "es", "fr"}; }
    Q_INVOKABLE QString languageLabel(const QString& tag) const {
        if (tag.isEmpty()) return "System default";
        if (tag == "de") return "Deutsch — German";
        if (tag == "es") return "Español — Spanish";
        if (tag == "fr") return "Français — French";
        return tag;
    }

Q_SIGNALS:
    void listenChanged();
    void natpmpChanged();
    void serverChanged();
    void pruneChanged();
    void pruneSizeGBChanged();
    void dataDirChanged();
    void displayUnitChanged(int unit);
    void languageChanged();

private:
    int m_displayUnit{0};
    QString m_language;
};

class MockChainModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int assumedChainstateSize MEMBER m_assumed_chainstate_size CONSTANT)
    Q_PROPERTY(int assumedBlockchainSize MEMBER m_assumed_blockchain_size CONSTANT)
    Q_PROPERTY(QVariantList timeRatioList MEMBER m_time_ratio_list CONSTANT)

public:
    int m_assumed_chainstate_size{12};
    int m_assumed_blockchain_size{610};
    QVariantList m_time_ratio_list{0.1, 0.2, 0.4, 0.8};
};

class MockNodeModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool pause MEMBER m_pause NOTIFY pauseChanged)
    Q_PROPERTY(int numOutboundPeers MEMBER m_num_outbound_peers NOTIFY numOutboundPeersChanged)
    Q_PROPERTY(int maxNumOutboundPeers MEMBER m_max_num_outbound_peers NOTIFY maxNumOutboundPeersChanged)
    Q_PROPERTY(double verificationProgress MEMBER m_verification_progress NOTIFY verificationProgressChanged)
    Q_PROPERTY(int remainingSyncTime MEMBER m_remaining_sync_time NOTIFY remainingSyncTimeChanged)
    Q_PROPERTY(bool faulted MEMBER m_faulted NOTIFY faultedChanged)
    Q_PROPERTY(int blockTipHeight MEMBER m_block_tip_height NOTIFY blockTipHeightChanged)

public:
    bool m_pause{false};
    int m_num_outbound_peers{0};
    int m_max_num_outbound_peers{8};
    double m_verification_progress{0.0};
    int m_remaining_sync_time{0};
    bool m_faulted{false};
    int m_block_tip_height{0};

    Q_INVOKABLE void startNodeInitializionThread() {}
    Q_INVOKABLE void requestShutdown() { Q_EMIT requestedShutdown(); }
    Q_INVOKABLE QString defaultProxyAddress() const { return QStringLiteral("127.0.0.1:9050"); }
    Q_INVOKABLE bool validateProxyAddress(const QString& value) const
    {
        // Minimal test stub to emulate host:port and [ipv6]:port acceptance.
        static const QRegularExpression pattern{
            QStringLiteral(R"(^(\[[0-9A-Fa-f:.]+\]|[0-9]{1,3}(\.[0-9]{1,3}){3}):([0-9]{1,5})$)")
        };
        return pattern.match(value).hasMatch();
    }

Q_SIGNALS:
    void requestedShutdown();
    void pauseChanged();
    void numOutboundPeersChanged();
    void maxNumOutboundPeersChanged();
    void verificationProgressChanged();
    void remainingSyncTimeChanged();
    void faultedChanged();
    void blockTipHeightChanged();
};

class MockPeerTableModel : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE void startAutoRefresh() {}
    Q_INVOKABLE void stopAutoRefresh() {}
};

class MockNetworkTrafficTower : public QObject
{
    Q_OBJECT
    Q_PROPERTY(quint64 totalBytesReceived MEMBER m_total_bytes_received NOTIFY totalBytesReceivedChanged)
    Q_PROPERTY(quint64 totalBytesSent MEMBER m_total_bytes_sent NOTIFY totalBytesSentChanged)
    Q_PROPERTY(double maxReceivedRateBps MEMBER m_max_received_rate_bps NOTIFY maxReceivedRateBpsChanged)
    Q_PROPERTY(double maxSentRateBps MEMBER m_max_sent_rate_bps NOTIFY maxSentRateBpsChanged)
    Q_PROPERTY(QVariantList receivedRateList MEMBER m_received_rate_list NOTIFY receivedRateListChanged)
    Q_PROPERTY(QVariantList sentRateList MEMBER m_sent_rate_list NOTIFY sentRateListChanged)
    Q_PROPERTY(int lastFilterWindowSize MEMBER m_last_filter_window_size NOTIFY lastFilterWindowSizeChanged)

public:
    quint64 m_total_bytes_received{1'000};
    quint64 m_total_bytes_sent{2'000};
    double m_max_received_rate_bps{100.0};
    double m_max_sent_rate_bps{200.0};
    QVariantList m_received_rate_list{10.0, 20.0, 30.0};
    QVariantList m_sent_rate_list{15.0, 25.0, 35.0};
    int m_last_filter_window_size{30};

    Q_INVOKABLE void updateFilterWindowSize(const int window_size)
    {
        m_last_filter_window_size = window_size;
        Q_EMIT lastFilterWindowSizeChanged();
    }

Q_SIGNALS:
    void totalBytesReceivedChanged();
    void totalBytesSentChanged();
    void maxReceivedRateBpsChanged();
    void maxSentRateBpsChanged();
    void receivedRateListChanged();
    void sentRateListChanged();
    void lastFilterWindowSizeChanged();
};

class MockPeerListModelProxy : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY sortByChanged)

public:
    QString sortBy() const { return m_sort_by; }
    void setSortBy(const QString& value)
    {
        if (m_sort_by == value) return;
        m_sort_by = value;
        Q_EMIT sortByChanged(m_sort_by);
    }

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override
    {
        Q_UNUSED(parent);
        return 0;
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        Q_UNUSED(index);
        Q_UNUSED(role);
        return {};
    }

Q_SIGNALS:
    void sortByChanged(const QString& roleName);
    void dataChanged(int startIndex, int endIndex);

private:
    QString m_sort_by{QStringLiteral("nodeId")};
};

class MockWalletListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        FormatRole,
        LoadStateRole
    };

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override
    {
        Q_UNUSED(parent);
        return m_wallet_names.size();
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_wallet_names.size()) return {};
        if (role == NameRole) return m_wallet_names.at(index.row());
        if (role == FormatRole) return QStringLiteral("sqlite");
        if (role == LoadStateRole) return m_wallet_load_states.at(index.row());
        return {};
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {NameRole, "name"},
            {FormatRole, "format"},
            {LoadStateRole, "loadState"},
        };
    }

    Q_INVOKABLE void listWalletDir() {}
    Q_INVOKABLE void reset()
    {
        setWalletLoadState(QStringLiteral("testwallet"), 1);
        setWalletLoadState(QStringLiteral("secondarywallet"), 0);
    }
    Q_INVOKABLE void setWalletLoadState(const QString& name, int state)
    {
        const int row = m_wallet_names.indexOf(name);
        if (row < 0 || m_wallet_load_states.at(row) == state) return;
        m_wallet_load_states[row] = state;
        const QModelIndex changed_index = index(row, 0);
        Q_EMIT dataChanged(changed_index, changed_index, {LoadStateRole});
    }

private:
    QStringList m_wallet_names{QStringLiteral("testwallet"), QStringLiteral("secondarywallet")};
    QVector<int> m_wallet_load_states{1, 0};
};

class MockBumpTransactionModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int state READ state WRITE setState NOTIFY stateChanged)
    Q_PROPERTY(QString oldFee MEMBER m_old_fee NOTIFY resultChanged)
    Q_PROPERTY(QString newFee MEMBER m_new_fee NOTIFY resultChanged)
    Q_PROPERTY(QString feeIncrease MEMBER m_fee_increase NOTIFY resultChanged)
    Q_PROPERTY(QString oldTxid MEMBER m_old_txid NOTIFY resultChanged)
    Q_PROPERTY(QString newTxid MEMBER m_new_txid NOTIFY resultChanged)
    Q_PROPERTY(QString errorText MEMBER m_error_text NOTIFY resultChanged)

public:
    enum State { Idle, Preparing, NeedsConfirmation, Committing, Succeeded, Failed };
    Q_ENUM(State)

    enum ActionType { SpeedUp };
    Q_ENUM(ActionType)

    int state() const { return m_state; }
    void setState(int state)
    {
        if (m_state == state) return;
        m_state = state;
        Q_EMIT stateChanged();
    }

    Q_INVOKABLE void prepareFeeBump(const QString& txid, unsigned int targetBlocks)
    {
        Q_UNUSED(targetBlocks);
        m_old_txid = txid;
        m_old_fee = QStringLiteral("0.00000500 ₿");
        m_new_fee = QStringLiteral("0.00001000 ₿");
        m_fee_increase = QStringLiteral("0.00000500 ₿");
        setState(NeedsConfirmation);
        Q_EMIT resultChanged();
    }

    Q_INVOKABLE void confirmFeeBump()
    {
        m_new_txid = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        setState(Succeeded);
        Q_EMIT resultChanged();
    }

    Q_INVOKABLE void reset()
    {
        m_state = Idle;
        m_old_fee.clear();
        m_new_fee.clear();
        m_fee_increase.clear();
        m_old_txid.clear();
        m_new_txid.clear();
        m_error_text.clear();
        Q_EMIT stateChanged();
        Q_EMIT resultChanged();
    }

Q_SIGNALS:
    void stateChanged();
    void actionTypeChanged();
    void resultChanged();

private:
    int m_state{Idle};
    QString m_old_fee;
    QString m_new_fee;
    QString m_fee_increase;
    QString m_old_txid;
    QString m_new_txid;
    QString m_error_text;
};

class MockActivityListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        AddressRole = Qt::UserRole + 1,
        AmountRole,
        DateRole,
        DepthRole,
        LabelRole,
        StatusRole,
        TypeRole,
        TxidRole,
        CanBumpRole,
        ReplacesTxidRole,
        ReplacedByTxidRole
    };

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override
    {
        Q_UNUSED(parent);
        return 2;
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
        if (index.row() == 0) {
            switch (role) {
            case AddressRole: return QStringLiteral("bcrt1qreceiveaddress");
            case AmountRole: return QStringLiteral("+0.01000000 BTC");
            case DateRole: return QStringLiteral("2026-01-01 00:00");
            case DepthRole: return 3;
            case LabelRole: return QStringLiteral("salary");
            case StatusRole: return MockTransaction::Confirmed;
            case TypeRole: return MockTransaction::RecvWithAddress;
            case TxidRole: return QStringLiteral("aaaa");
            case CanBumpRole: return false;
            case ReplacesTxidRole: return QString{};
            case ReplacedByTxidRole: return QString{};
            default: return {};
            }
        }
        switch (role) {
        case AddressRole: return QStringLiteral("bcrt1qsendaddress");
        case AmountRole: return QStringLiteral("-0.00100000 BTC");
        case DateRole: return QStringLiteral("2026-01-02 00:00");
        case DepthRole: return 0;
        case LabelRole: return QStringLiteral("coffee");
        case StatusRole: return MockTransaction::Unconfirmed;
        case TypeRole: return MockTransaction::SendToAddress;
        case TxidRole: return QStringLiteral("bbbb");
        case CanBumpRole: return true;
        case ReplacesTxidRole: return QString{};
        case ReplacedByTxidRole: return QString{};
        default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {AddressRole, "address"},
            {AmountRole, "amount"},
            {DateRole, "date"},
            {DepthRole, "depth"},
            {LabelRole, "label"},
            {StatusRole, "status"},
            {TypeRole, "type"},
            {TxidRole, "txid"},
            {CanBumpRole, "canBump"},
            {ReplacesTxidRole, "replacesTxid"},
            {ReplacedByTxidRole, "replacedByTxid"},
        };
    }

    Q_INVOKABLE void reload() {}
};

class QmlTestsSetup : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->addImportPath(QStringLiteral(BITCOINQML_QML_TEST_MOCKS_DIR));
        static MockAppMode app_mode;
        static MockOptionsModel options_model;
        static MockChainModel chain_model;
        static MockNodeModel node_model;
        static MockPeerTableModel peer_table_model;
        static MockNetworkTrafficTower network_traffic_tower;
        static MockPeerListModelProxy peer_list_model_proxy;
        static MockPeerDetailsModel peer_details_model;
        static MockWalletQmlModelTransaction wallet_transaction;
        static MockPaymentRequest payment_request;
        static MockSendRecipient send_recipient;
        static MockRecipientsModel recipients_model;
        static MockCoinsListModel coins_list_model;
        static MockWalletQmlModel wallet_model;
        static MockWalletController wallet_controller;
        static MockWalletListModel wallet_list_model;
        static MockActivityListModel activity_list_model;
        static MockBumpTransactionModel bump_model;
        recipients_model.setCurrent(&send_recipient);
        wallet_model.setActivityListModel(&activity_list_model);
        wallet_model.setBumpModel(&bump_model);
        wallet_model.setRecipients(&recipients_model);
        wallet_model.setCoinsListModel(&coins_list_model);
        wallet_model.setCurrentTransaction(&wallet_transaction);
        wallet_model.setCurrentPaymentRequest(&payment_request);
        wallet_controller.setSelectedWalletObject(&wallet_model);
        qmlRegisterSingletonInstance<MockAppMode>("org.bitcoincore.qt", 1, 0, "AppMode", &app_mode);
        qmlRegisterUncreatableType<MockPeerDetailsModel>(
            "org.bitcoincore.qt",
            1,
            0,
            "PeerDetailsModel",
            "Test stub type"
        );
        qmlRegisterType<MockBitcoinAmount>("org.bitcoincore.qt", 1, 0, "BitcoinAmount");
        qmlRegisterType<MockBitcoinAddress>("org.bitcoincore.qt", 1, 0, "BitcoinAddress");
        qmlRegisterType<MockPaymentRequest>("org.bitcoincore.qt", 1, 0, "PaymentRequest");
        qmlRegisterUncreatableType<MockTransaction>("org.bitcoincore.qt", 1, 0, "Transaction", "Test stub type");
        qmlRegisterUncreatableType<MockSendRecipient>("org.bitcoincore.qt", 1, 0, "SendRecipient", "Test stub type");
        qmlRegisterUncreatableType<MockBumpTransactionModel>("org.bitcoincore.qt", 1, 0, "BumpTransactionModel", "Test stub type");
        qmlRegisterUncreatableType<MockWalletQmlModel>("org.bitcoincore.qt", 1, 0, "WalletQmlModel", "Test stub type");
        qmlRegisterUncreatableType<MockWalletQmlModelTransaction>(
            "org.bitcoincore.qt",
            1,
            0,
            "WalletQmlModelTransaction",
            "Test stub type"
        );
        qmlRegisterType<BlockClockDial>("org.bitcoincore.qt", 1, 0, "BlockClockDial");
        qmlRegisterType<LineGraph>("org.bitcoincore.qt", 1, 0, "LineGraph");
        engine->rootContext()->setContextProperty(QStringLiteral("needOnboarding"), true);
        engine->rootContext()->setContextProperty(QStringLiteral("optionsModel"), &options_model);
        engine->rootContext()->setContextProperty(QStringLiteral("chainModel"), &chain_model);
        engine->rootContext()->setContextProperty(QStringLiteral("nodeModel"), &node_model);
        engine->rootContext()->setContextProperty(QStringLiteral("peerTableModel"), &peer_table_model);
        engine->rootContext()->setContextProperty(QStringLiteral("networkTrafficTower"), &network_traffic_tower);
        engine->rootContext()->setContextProperty(QStringLiteral("peerListModelProxy"), &peer_list_model_proxy);
        engine->rootContext()->setContextProperty(QStringLiteral("testPeerDetailsModel"), &peer_details_model);
        engine->rootContext()->setContextProperty(QStringLiteral("walletController"), &wallet_controller);
        engine->rootContext()->setContextProperty(QStringLiteral("walletListModel"), &wallet_list_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testWalletModel"), &wallet_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testWalletTransaction"), &wallet_transaction);
        engine->rootContext()->setContextProperty(QStringLiteral("testPaymentRequest"), &payment_request);
        engine->rootContext()->setContextProperty(QStringLiteral("testSendRecipient"), &send_recipient);
        engine->rootContext()->setContextProperty(QStringLiteral("testRecipientsModel"), &recipients_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testCoinsListModel"), &coins_list_model);
        engine->addImportPath(QStringLiteral(BITCOINQML_QML_SOURCE_DIR));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(bitcoinqml_qmltests, QmlTestsSetup)

#include "qml_tests_main.moc"
