// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtQuickTest/quicktest.h>

#include <QAbstractListModel>
#include <QDateTime>
#include <QFont>
#include <QHash>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <qqml.h>

#include <algorithm>
#include <utility>
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
    QString m_state{QStringLiteral("DESKTOP")};
};

class MockBuildInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isDebug READ isDebug CONSTANT)
    Q_PROPERTY(QString fullClientVersion READ fullClientVersion CONSTANT)

public:
    bool isDebug() const { return false; }
    QString fullClientVersion() const { return QStringLiteral("v0.0.0-test"); }
};

class MockPeerDetailsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int nodeId MEMBER m_node_id CONSTANT)
    Q_PROPERTY(QString rawAddress MEMBER m_raw_address CONSTANT)
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
    QString m_raw_address{QStringLiteral("127.0.0.1")};
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
    Q_PROPERTY(QString display READ display WRITE setDisplay NOTIFY displayChanged)
    Q_PROPERTY(Unit unit READ unit WRITE setUnit NOTIFY unitChanged)
    Q_PROPERTY(qint64 satoshi READ satoshi WRITE setSatoshi NOTIFY amountChanged)
    Q_PROPERTY(QString unitLabel READ unitLabel NOTIFY unitChanged)
    Q_PROPERTY(QString displayWithUnit READ displayWithUnit NOTIFY displayChanged)

public:
    enum Unit {
        BTC,
        mBTC,
        uBTC,
        SAT
    };
    Q_ENUM(Unit)

    QString m_display{QStringLiteral("0.00000000")};
    Unit m_unit{BTC};

    QString display() const { return m_display; }
    Unit unit() const { return m_unit; }
    void setUnit(Unit unit)
    {
        if (m_unit == unit) return;
        const qint64 sats = satoshi();
        m_unit = unit;
        m_display = displayForSatoshi(sats);
        Q_EMIT unitChanged();
        Q_EMIT displayChanged();
    }
    qint64 satoshi() const
    {
        const QString amount_text = m_display.split(u' ').constFirst();
        bool ok{false};
        if (m_unit == SAT) {
            const qint64 value = amount_text.toLongLong(&ok);
            return ok ? value : 0;
        }

        const double value = amount_text.toDouble(&ok);
        const double factor = m_unit == mBTC ? 100000.0 : (m_unit == uBTC ? 100.0 : 100000000.0);
        return ok ? static_cast<qint64>(value * factor + 0.5) : 0;
    }
    QString unitLabel() const
    {
        if (m_unit == BTC) return QStringLiteral("BTC");
        if (m_unit == mBTC) return QStringLiteral("mBTC");
        if (m_unit == uBTC) return QStringLiteral("bits");
        const qint64 sats = satoshi();
        return sats == 1 || sats == -1 ? QStringLiteral("sat") : QStringLiteral("sats");
    }
    void setSatoshi(qint64 sats)
    {
        const QString updated = displayForSatoshi(sats);
        if (m_display == updated) return;
        m_display = updated;
        Q_EMIT displayChanged();
        Q_EMIT amountChanged();
    }
    void setDisplay(const QString& display)
    {
        const QString normalized = normalizedDisplay(display);
        if (m_display == normalized) return;
        m_display = normalized;
        Q_EMIT displayChanged();
        Q_EMIT amountChanged();
    }
    QString displayWithUnit() const { return m_display.isEmpty() ? QString{} : m_display + QStringLiteral(" ") + unitLabel(); }
    Q_INVOKABLE void format()
    {
        const QString normalized = normalizedDisplay(m_display);
        if (m_display != normalized) {
            m_display = normalized;
        }
        Q_EMIT displayChanged();
        Q_EMIT amountChanged();
    }
    Q_INVOKABLE void flipUnit()
    {
        setUnit(m_unit == BTC ? SAT : BTC);
    }

Q_SIGNALS:
    void amountChanged();
    void displayChanged();
    void unitChanged();

private:
    QString displayForSatoshi(qint64 sats) const
    {
        if (m_unit == SAT) return QString::number(sats);
        const qint64 whole = sats / 100000000;
        const qint64 fraction = qAbs(sats % 100000000);
        return QStringLiteral("%1.%2").arg(whole).arg(fraction, 8, 10, QLatin1Char('0'));
    }

    QString normalizedDisplay(const QString& display) const
    {
        const QString trimmed = display.trimmed();
        if (trimmed.isEmpty()) return QString{};
        if (m_unit == SAT) {
            QString digits_only = trimmed;
            digits_only.remove(QRegularExpression(QStringLiteral("[^0-9]")));
            return digits_only.isEmpty() ? QString{} : QString::number(digits_only.toLongLong());
        }

        QString sanitized = trimmed;
        sanitized.remove(QRegularExpression(QStringLiteral("[^0-9.]")));
        const QStringList parts = sanitized.split(u'.');
        const qint64 whole = parts.value(0).isEmpty() ? 0 : parts.value(0).toLongLong();
        const QString fraction_text = parts.size() > 1 ? parts.value(1).left(8).leftJustified(8, u'0') : QStringLiteral("00000000");
        const qint64 fraction = fraction_text.toLongLong();
        return QStringLiteral("%1.%2").arg(whole).arg(fraction, 8, 10, QLatin1Char('0'));
    }
};

class MockBitcoinAddress : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address READ address NOTIFY addressChanged)
    Q_PROPERTY(QString formattedAddress READ formattedAddress NOTIFY formattedAddressChanged)
    Q_PROPERTY(QString ellipsesAddress READ ellipsesAddress NOTIFY ellipsesAddressChanged)

public:
    QString address() const { return m_address; }
    QString formattedAddress() const { return m_address; }
    QString ellipsesAddress() const { return m_address; }
    Q_INVOKABLE int setAddress(const QString& address, int cursorPosition = 0)
    {
        if (m_address != address) {
            m_address = address;
            Q_EMIT addressChanged();
            Q_EMIT formattedAddressChanged();
            Q_EMIT ellipsesAddressChanged();
        }
        return cursorPosition;
    }

Q_SIGNALS:
    void addressChanged();
    void formattedAddressChanged();
    void ellipsesAddressChanged();

private:
    QString m_address{QStringLiteral("bcrt1qsendtoaddress")};
};

class MockAddressListModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Category category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(QVariantList categoryOptions READ categoryOptions CONSTANT)
    Q_PROPERTY(bool showUsed READ showUsed WRITE setShowUsed NOTIFY showUsedChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Category {
        SingleUse,
        Change
    };
    Q_ENUM(Category)

    Category category() const { return m_category; }
    QVariantList categoryOptions() const
    {
        return {
            QVariantMap{{QStringLiteral("value"), static_cast<int>(SingleUse)}, {QStringLiteral("text"), QStringLiteral("Single-use")}},
            QVariantMap{{QStringLiteral("value"), static_cast<int>(Change)}, {QStringLiteral("text"), QStringLiteral("Change")}},
        };
    }
    bool showUsed() const { return m_show_used; }
    int count() const { return 0; }

    void setCategory(const Category category)
    {
        if (m_category == category) return;
        m_category = category;
        Q_EMIT categoryChanged();
    }

    void setShowUsed(const bool show_used)
    {
        if (m_show_used == show_used) return;
        m_show_used = show_used;
        Q_EMIT showUsedChanged();
    }

    Q_INVOKABLE void refresh() {}
    Q_INVOKABLE bool setAddressLabel(const QString&, const QString&) { return false; }
    Q_INVOKABLE QString addressAt(int) const { return {}; }

Q_SIGNALS:
    void categoryChanged();
    void showUsedChanged();
    void countChanged();

private:
    Category m_category{SingleUse};
    bool m_show_used{false};
};

class MockPaymentRequest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString id MEMBER m_id NOTIFY idChanged)
    Q_PROPERTY(QObject* amount READ amount CONSTANT)
    Q_PROPERTY(QString amountError MEMBER m_amount_error NOTIFY amountErrorChanged)
    Q_PROPERTY(QString label MEMBER m_label NOTIFY labelChanged)
    Q_PROPERTY(QString message MEMBER m_message NOTIFY messageChanged)
    Q_PROPERTY(QString noteSelf MEMBER m_note_self NOTIFY noteSelfChanged)
    Q_PROPERTY(QString address MEMBER m_address NOTIFY addressChanged)
    Q_PROPERTY(QString addressFormatted READ addressFormatted NOTIFY addressChanged)
    Q_PROPERTY(QString addressType MEMBER m_address_type NOTIFY addressTypeChanged)
    Q_PROPERTY(bool needsUnlock MEMBER m_needs_unlock NOTIFY needsUnlockChanged)
    Q_PROPERTY(QString unlockError MEMBER m_unlock_error NOTIFY unlockErrorChanged)
    Q_PROPERTY(QString createdIso MEMBER m_created_iso NOTIFY createdIsoChanged)
    Q_PROPERTY(QString qrPayload READ qrPayload NOTIFY addressChanged)
    Q_PROPERTY(bool isEditing MEMBER m_is_editing NOTIFY isEditingChanged)

public:
    QString m_id;
    QString m_amount_error;
    QString m_label;
    QString m_message;
    QString m_note_self;
    QString m_address;
    QString m_address_type;
    bool m_needs_unlock{false};
    QString m_unlock_error;
    QString m_created_iso;
    bool m_is_editing{true};
    MockBitcoinAmount m_amount{};

    QObject* amount() { return &m_amount; }
    QString addressFormatted() const { return m_address; }
    QString qrPayload() const { return m_address.isEmpty() ? QString{} : QStringLiteral("bitcoin:") + m_address; }
    Q_INVOKABLE void clear()
    {
        m_id.clear();
        m_amount_error.clear();
        m_label.clear();
        m_message.clear();
        m_note_self.clear();
        m_address.clear();
        m_address_type.clear();
        m_needs_unlock = false;
        m_unlock_error.clear();
        m_created_iso.clear();
        m_is_editing = true;
        m_amount.m_display = QStringLiteral("0.00000000");
        m_amount.m_unit = MockBitcoinAmount::BTC;
        Q_EMIT idChanged();
        Q_EMIT amountErrorChanged();
        Q_EMIT labelChanged();
        Q_EMIT messageChanged();
        Q_EMIT noteSelfChanged();
        Q_EMIT addressChanged();
        Q_EMIT addressTypeChanged();
        Q_EMIT needsUnlockChanged();
        Q_EMIT unlockErrorChanged();
        Q_EMIT createdIsoChanged();
        Q_EMIT isEditingChanged();
        Q_EMIT m_amount.displayChanged();
    }
    Q_INVOKABLE void edit()
    {
        if (m_is_editing) return;
        m_is_editing = true;
        Q_EMIT isEditingChanged();
    }

Q_SIGNALS:
    void idChanged();
    void amountErrorChanged();
    void labelChanged();
    void messageChanged();
    void noteSelfChanged();
    void addressChanged();
    void addressTypeChanged();
    void needsUnlockChanged();
    void unlockErrorChanged();
    void createdIsoChanged();
    void isEditingChanged();
};

class MockReceiveRequests : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int count MEMBER m_count NOTIFY countChanged)

public:
    int m_count{0};

    Q_INVOKABLE QVariantList matchingEntriesForAddress(const QString& address) const
    {
        if (address != QStringLiteral("bcrt1qreceiveaddress")) {
            return {};
        }

        return {
            QVariantMap{
                {QStringLiteral("requestId"), QStringLiteral("req-1")},
                {QStringLiteral("label"), QStringLiteral("Alice")},
                {QStringLiteral("amountDisplay"), QStringLiteral("0.00100000 BTC")},
                {QStringLiteral("date"), QStringLiteral("Fri Jan 2 2026")},
            },
        };
    }

Q_SIGNALS:
    void countChanged();
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
    Q_PROPERTY(QObject* address READ address CONSTANT)
    Q_PROPERTY(QString addressError MEMBER m_address_error NOTIFY addressErrorChanged)
    Q_PROPERTY(QObject* amount READ amount CONSTANT)
    Q_PROPERTY(QString amountError MEMBER m_amount_error NOTIFY amountErrorChanged)
    Q_PROPERTY(QString label MEMBER m_label NOTIFY labelChanged)
    Q_PROPERTY(bool subtractFeeFromAmount READ subtractFeeFromAmount WRITE setSubtractFeeFromAmount NOTIFY subtractFeeFromAmountChanged)
    Q_PROPERTY(bool isValid MEMBER m_is_valid NOTIFY isValidChanged)

public:
    MockBitcoinAddress m_address{};
    QString m_address_error;
    MockBitcoinAmount m_amount{};
    QString m_amount_error;
    QString m_label;
    bool m_subtract_fee_from_amount{false};
    bool m_is_valid{true};

    QObject* address() { return &m_address; }
    QObject* amount() { return &m_amount; }
    bool subtractFeeFromAmount() const { return m_subtract_fee_from_amount; }
    void setSubtractFeeFromAmount(bool value)
    {
        if (m_subtract_fee_from_amount == value) return;
        m_subtract_fee_from_amount = value;
        Q_EMIT subtractFeeFromAmountChanged();
    }

Q_SIGNALS:
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
    Q_PROPERTY(qint64 totalAmountSatoshi READ totalAmountSatoshi WRITE setTotalAmountSatoshi NOTIFY totalAmountChanged)
    Q_PROPERTY(bool allValid READ allValid WRITE setAllValid NOTIFY validationChanged)
    Q_PROPERTY(QString validationError READ validationError WRITE setValidationError NOTIFY validationChanged)

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
    qint64 totalAmountSatoshi() const { return m_total_amount_satoshi; }
    bool allValid() const { return m_all_valid; }
    QString validationError() const { return m_validation_error; }

    void setAllValid(bool all_valid)
    {
        if (m_all_valid == all_valid) return;
        m_all_valid = all_valid;
        Q_EMIT validationChanged();
    }

    void setValidationError(const QString& validation_error)
    {
        if (m_validation_error == validation_error) return;
        m_validation_error = validation_error;
        Q_EMIT validationChanged();
    }

    void setTotalAmountSatoshi(qint64 total_amount_satoshi)
    {
        if (m_total_amount_satoshi == total_amount_satoshi) return;
        m_total_amount_satoshi = total_amount_satoshi;
        Q_EMIT totalAmountChanged();
    }

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
        setTotalAmountSatoshi(0);
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
    void totalAmountChanged();
    void listCleared();
    void validationChanged();

private:
    QObject* m_current{nullptr};
    int m_current_index{1};
    qint64 m_total_amount_satoshi{0};
    std::vector<RecipientRow> m_rows{};
    bool m_all_valid{true};
    QString m_validation_error;
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

    Q_INVOKABLE void reset()
    {
        bool changed{false};
        for (CoinRow& row : m_rows) {
            if (!row.selected) continue;
            row.selected = false;
            changed = true;
        }
        if (!changed) return;
        Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, 0), {SelectedRole});
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
    Q_PROPERTY(bool currentTransactionCanSend MEMBER m_current_transaction_can_send NOTIFY currentTransactionChanged)
    Q_PROPERTY(bool currentTransactionCanBroadcast MEMBER m_current_transaction_can_broadcast NOTIFY currentTransactionChanged)
    Q_PROPERTY(QString currentTransactionReviewMessage MEMBER m_current_transaction_review_message NOTIFY currentTransactionChanged)
    Q_PROPERTY(QObject* currentPaymentRequest READ currentPaymentRequest CONSTANT)
    Q_PROPERTY(QObject* detailPaymentRequest READ detailPaymentRequest CONSTANT)
    Q_PROPERTY(QObject* receiveRequests READ receiveRequests CONSTANT)
    Q_PROPERTY(MockAddressListModel* addressListModel READ addressListModel CONSTANT)
    Q_PROPERTY(bool hasExternalSigner MEMBER m_has_external_signer NOTIFY walletInfoChanged)
    Q_PROPERTY(int displayUnit MEMBER m_display_unit NOTIFY displayUnitChanged)
    Q_PROPERTY(int targetBlocks READ targetBlocks WRITE setTargetBlocks NOTIFY targetBlocksChanged)
    Q_PROPERTY(QString estimatedFee READ estimatedFee NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(bool customFeeEnabled READ customFeeEnabled WRITE setCustomFeeEnabled NOTIFY customFeeEnabledChanged)
    Q_PROPERTY(QString customFeeRate READ customFeeRate WRITE setCustomFeeRate NOTIFY customFeeRateChanged)
    Q_PROPERTY(bool customFeeRateValid READ customFeeRateValid NOTIFY customFeeRateValidChanged)
    Q_PROPERTY(bool feeEstimatePending MEMBER m_fee_estimate_pending NOTIFY feeEstimatePendingChanged)
    Q_PROPERTY(int feeEstimateRevision MEMBER m_fee_estimate_revision NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(bool sendAmountExhaustsBalance READ sendAmountExhaustsBalance WRITE setSendAmountExhaustsBalance NOTIFY sendAmountExhaustsBalanceChanged)
    Q_PROPERTY(bool prepareTransactionResult MEMBER m_prepare_transaction_result NOTIFY prepareTransactionResultChanged)
    Q_PROPERTY(bool sendTransactionResult MEMBER m_send_transaction_result NOTIFY sendTransactionResultChanged)
    Q_PROPERTY(int prepareTransactionCalls READ prepareTransactionCalls NOTIFY prepareTransactionCallsChanged)
    Q_PROPERTY(int scheduleFeeEstimatesCalls READ scheduleFeeEstimatesCalls NOTIFY scheduleFeeEstimatesCallsChanged)
    Q_PROPERTY(int sendTransactionCalls READ sendTransactionCalls NOTIFY sendTransactionCallsChanged)
    Q_PROPERTY(int broadcastCurrentTransactionCalls READ broadcastCurrentTransactionCalls NOTIFY broadcastCurrentTransactionCallsChanged)
    Q_PROPERTY(int discardCurrentTransactionCalls READ discardCurrentTransactionCalls NOTIFY discardCurrentTransactionCallsChanged)
    Q_PROPERTY(bool isEncrypted MEMBER m_is_encrypted NOTIFY securityStateChanged)
    Q_PROPERTY(bool isLocked MEMBER m_is_locked NOTIFY securityStateChanged)
    Q_PROPERTY(QString keyScheme MEMBER m_key_scheme NOTIFY walletInfoChanged)
    Q_PROPERTY(QString privateKeysStatus MEMBER m_private_keys_status NOTIFY walletInfoChanged)
    Q_PROPERTY(QString externalSignerStatus MEMBER m_external_signer_status NOTIFY walletInfoChanged)
    Q_PROPERTY(bool canManagePassphrase MEMBER m_can_manage_passphrase NOTIFY walletInfoChanged)
    Q_PROPERTY(QString settingsError MEMBER m_settings_error NOTIFY settingsErrorChanged)
    Q_PROPERTY(QString lastBackupPath READ lastBackupPath NOTIFY backupWalletCallsChanged)
    Q_PROPERTY(int backupWalletCalls READ backupWalletCalls NOTIFY backupWalletCallsChanged)
    Q_PROPERTY(QString transactionError MEMBER m_transaction_error NOTIFY transactionErrorChanged)
    Q_PROPERTY(bool transactionNeedsUnlock MEMBER m_transaction_needs_unlock NOTIFY transactionNeedsUnlockChanged)
    Q_PROPERTY(QString lastCommitAddressType MEMBER m_last_commit_address_type NOTIFY lastCommitAddressTypeChanged)
    Q_PROPERTY(QString lastLoadedPaymentRequestId MEMBER m_last_loaded_payment_request_id NOTIFY lastLoadedPaymentRequestIdChanged)
    Q_PROPERTY(QString lastLoadedPaymentRequestDetailId MEMBER m_last_loaded_payment_request_detail_id NOTIFY lastLoadedPaymentRequestDetailIdChanged)
    Q_PROPERTY(QString lastTemplateRequestId MEMBER m_last_template_request_id NOTIFY lastTemplateRequestIdChanged)
    Q_PROPERTY(QString lastRemovedRequestId MEMBER m_last_removed_request_id NOTIFY lastRemovedRequestIdChanged)

public:
    QString m_name{QStringLiteral("testwallet")};
    QString m_balance{QStringLiteral("1.00000000 BTC")};
    QObject* m_activity_list_model{nullptr};
    QObject* m_bump_model{nullptr};
    QObject* m_recipients{nullptr};
    QObject* m_coins_list_model{nullptr};
    QObject* m_current_transaction{nullptr};
    QObject* m_current_payment_request{nullptr};
    MockReceiveRequests m_receive_requests{};
    bool m_has_external_signer{false};
    int m_display_unit{0};
    QString m_default_receive_address_type{QStringLiteral("bech32")};
    QString m_last_commit_address_type;
    QString m_last_loaded_payment_request_id;
    QString m_last_loaded_payment_request_detail_id;
    QString m_last_template_request_id;
    QString m_last_removed_request_id;
    QString m_saved_payment_request_label;
    QString m_saved_payment_request_message;
    QString m_saved_payment_request_note_self;
    QString m_saved_payment_request_amount_display;
    QString m_saved_payment_request_address_type;
    int m_target_blocks{2};
    bool m_prepare_transaction_result{true};
    bool m_current_transaction_can_send{true};
    bool m_current_transaction_can_broadcast{false};
    QString m_current_transaction_review_message;

    QObject* activityListModel() const { return m_activity_list_model; }
    QObject* bumpModel() const { return m_bump_model; }
    QObject* recipients() const { return m_recipients; }
    QObject* coinsListModel() const { return m_coins_list_model; }
    QObject* currentTransaction() const { return m_current_transaction; }
    QObject* currentPaymentRequest() const { return m_current_payment_request; }
    QObject* detailPaymentRequest() const { return m_current_payment_request; }
    QObject* receiveRequests() { return &m_receive_requests; }
    MockAddressListModel* addressListModel() { return &m_address_list_model; }
    Q_INVOKABLE QVariantList availableReceiveAddressTypes() const
    {
        return {
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("bech32m")},
                {QStringLiteral("label"), QStringLiteral("Bech32m (Taproot)")},
                {QStringLiteral("description"), QStringLiteral("Lower fees")},
            },
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("bech32")},
                {QStringLiteral("label"), QStringLiteral("Bech32 (SegWit)")},
                {QStringLiteral("description"), QStringLiteral("Widely supported")},
            },
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("p2sh-segwit")},
                {QStringLiteral("label"), QStringLiteral("Base58 (P2SH-SegWit)")},
                {QStringLiteral("description"), QStringLiteral("Backward compatible")},
            },
        };
    }
    Q_INVOKABLE QString defaultReceiveAddressType() const { return m_default_receive_address_type; }
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
    int broadcastCurrentTransactionCalls() const { return m_broadcast_current_transaction_calls; }
    int discardCurrentTransactionCalls() const { return m_discard_current_transaction_calls; }
    bool sendAmountExhaustsBalance() const { return m_send_amount_exhausts_balance; }
    QString lastBackupPath() const { return m_last_backup_path; }
    int backupWalletCalls() const { return m_backup_wallet_calls; }
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
    Q_INVOKABLE void loadPaymentRequest(const QString& request_id)
    {
        m_last_loaded_payment_request_id = request_id;
        Q_EMIT lastLoadedPaymentRequestIdChanged();
        auto* request = qobject_cast<MockPaymentRequest*>(m_current_payment_request);
        if (!request) return;
        request->m_id = request_id;
        Q_EMIT request->idChanged();
    }
    Q_INVOKABLE bool loadPaymentRequestDetail(const QString& request_id)
    {
        m_last_loaded_payment_request_detail_id = request_id;
        Q_EMIT lastLoadedPaymentRequestDetailIdChanged();
        auto* request = qobject_cast<MockPaymentRequest*>(m_current_payment_request);
        if (!request) return false;
        request->m_id = request_id;
        request->m_label = QStringLiteral("Alice");
        request->m_message = QStringLiteral("Coffee");
        request->m_note_self = QStringLiteral("Counter");
        request->m_address = QStringLiteral("bcrt1qrequestaddress0000000000000000000000");
        request->m_created_iso = QStringLiteral("2026-01-02T00:00:00Z");
        request->m_is_editing = false;
        request->m_amount.m_display = QStringLiteral("0.00100000");
        request->m_amount.m_unit = MockBitcoinAmount::BTC;
        Q_EMIT request->idChanged();
        Q_EMIT request->labelChanged();
        Q_EMIT request->messageChanged();
        Q_EMIT request->noteSelfChanged();
        Q_EMIT request->addressChanged();
        Q_EMIT request->createdIsoChanged();
        Q_EMIT request->isEditingChanged();
        Q_EMIT request->m_amount.displayChanged();
        Q_EMIT request->m_amount.unitChanged();
        return true;
    }
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
    void setSendAmountExhaustsBalance(const bool value)
    {
        if (m_send_amount_exhausts_balance == value) return;
        m_send_amount_exhausts_balance = value;
        Q_EMIT sendAmountExhaustsBalanceChanged();
    }
    Q_INVOKABLE bool prepareTransaction()
    {
        ++m_prepare_transaction_calls;
        Q_EMIT prepareTransactionCallsChanged();
        if (m_prepare_transaction_result) {
            setTransactionStatus({}, false);
        } else {
            const bool selected_inputs_active{
                m_coins_list_model && m_coins_list_model->property("selectedCoinsCount").toInt() > 0};
            setTransactionStatus(selected_inputs_active
                ? QStringLiteral("Selected inputs do not cover the amount plus fee")
                : QStringLiteral("Amount plus fee exceeds available balance"), false);
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
    Q_INVOKABLE bool broadcastCurrentTransaction()
    {
        ++m_broadcast_current_transaction_calls;
        Q_EMIT broadcastCurrentTransactionCallsChanged();
        return m_current_transaction_can_broadcast;
    }
    Q_INVOKABLE void discardCurrentTransaction()
    {
        ++m_discard_current_transaction_calls;
        m_current_transaction_can_send = false;
        m_current_transaction_can_broadcast = false;
        m_current_transaction_review_message.clear();
        if (m_recipients) {
            QMetaObject::invokeMethod(m_recipients, "clearToFront");
        }
        Q_EMIT currentTransactionChanged();
        Q_EMIT discardCurrentTransactionCallsChanged();
    }
    Q_INVOKABLE bool commitPaymentRequest()
    {
        auto* request = qobject_cast<MockPaymentRequest*>(m_current_payment_request);
        if (!request) return false;
        m_last_commit_address_type = request->m_address_type;
        Q_EMIT lastCommitAddressTypeChanged();
        m_saved_payment_request_label = request->m_label;
        m_saved_payment_request_message = request->m_message;
        m_saved_payment_request_note_self = request->m_note_self;
        m_saved_payment_request_amount_display = request->m_amount.m_display;
        m_saved_payment_request_address_type = request->m_address_type;
        request->m_id = QStringLiteral("1");
        request->m_address = QStringLiteral("bcrt1qrequestaddress0000000000000000000000");
        request->m_is_editing = false;
        Q_EMIT request->idChanged();
        Q_EMIT request->addressChanged();
        Q_EMIT request->isEditingChanged();
        return true;
    }
    Q_INVOKABLE bool commitPaymentRequestWithPassphrase(const QString&)
    {
        return commitPaymentRequest();
    }
    Q_INVOKABLE void clearSettingsError()
    {
        if (m_settings_error.isEmpty()) return;
        m_settings_error.clear();
        Q_EMIT settingsErrorChanged();
    }
    Q_INVOKABLE bool backupWallet(const QString& path)
    {
        m_last_backup_path = path;
        ++m_backup_wallet_calls;
        clearSettingsError();
        Q_EMIT backupWalletCallsChanged();
        return true;
    }
    Q_INVOKABLE void resetWalletSettingsTestState()
    {
        m_last_backup_path.clear();
        m_backup_wallet_calls = 0;
        m_key_scheme = QStringLiteral("Descriptor");
        m_private_keys_status = QStringLiteral("Enabled");
        m_external_signer_status = QStringLiteral("Not used");
        m_can_manage_passphrase = true;
        clearSettingsError();
        Q_EMIT backupWalletCallsChanged();
        Q_EMIT walletInfoChanged();
    }
    Q_INVOKABLE void setExternalSignerWalletSettingsTestState()
    {
        m_key_scheme = QStringLiteral("Watch-only");
        m_private_keys_status = QStringLiteral("Disabled");
        m_external_signer_status = QStringLiteral("Enabled");
        m_can_manage_passphrase = false;
        Q_EMIT walletInfoChanged();
    }
    Q_INVOKABLE void usePaymentRequestAsTemplate(const QString& request_id)
    {
        m_last_template_request_id = request_id;
        Q_EMIT lastTemplateRequestIdChanged();
        auto* request = qobject_cast<MockPaymentRequest*>(m_current_payment_request);
        if (!request) return;
        request->m_id.clear();
        request->m_label = m_saved_payment_request_label;
        request->m_message = m_saved_payment_request_message;
        request->m_note_self = m_saved_payment_request_note_self;
        request->m_address.clear();
        request->m_address_type = m_saved_payment_request_address_type;
        request->m_is_editing = true;
        request->m_amount.m_display = m_saved_payment_request_amount_display;
        Q_EMIT request->idChanged();
        Q_EMIT request->labelChanged();
        Q_EMIT request->messageChanged();
        Q_EMIT request->noteSelfChanged();
        Q_EMIT request->addressChanged();
        Q_EMIT request->addressTypeChanged();
        Q_EMIT request->isEditingChanged();
        Q_EMIT request->m_amount.displayChanged();
    }
    Q_INVOKABLE bool removeReceiveRequest(const QString& request_id)
    {
        m_last_removed_request_id = request_id;
        Q_EMIT lastRemovedRequestIdChanged();
        return true;
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
    void sendAmountExhaustsBalanceChanged();
    void prepareTransactionResultChanged();
    void sendTransactionResultChanged();
    void prepareTransactionCallsChanged();
    void scheduleFeeEstimatesCallsChanged();
    void sendTransactionCallsChanged();
    void broadcastCurrentTransactionCallsChanged();
    void discardCurrentTransactionCallsChanged();
    void currentTransactionChanged();
    void externalSignerApprovalSucceeded();
    void externalSignerApprovalPartiallySucceeded();
    void externalSignerApprovalFailed(const QString& message, bool signerNotFound);
    void securityStateChanged();
    void walletInfoChanged();
    void settingsErrorChanged();
    void backupWalletCallsChanged();
    void transactionErrorChanged();
    void transactionNeedsUnlockChanged();
    void displayUnitChanged();
    void lastCommitAddressTypeChanged();
    void lastLoadedPaymentRequestIdChanged();
    void lastLoadedPaymentRequestDetailIdChanged();
    void lastTemplateRequestIdChanged();
    void lastRemovedRequestIdChanged();

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
    bool m_send_amount_exhausts_balance{false};
    bool m_send_transaction_result{true};
    bool m_is_encrypted{false};
    bool m_is_locked{false};
    QString m_key_scheme{QStringLiteral("Descriptor")};
    QString m_private_keys_status{QStringLiteral("Enabled")};
    QString m_external_signer_status{QStringLiteral("Not used")};
    bool m_can_manage_passphrase{true};
    QString m_settings_error;
    QString m_last_backup_path;
    int m_backup_wallet_calls{0};
    QString m_transaction_error;
    bool m_transaction_needs_unlock{false};
    int m_fee_estimate_revision{1};
    int m_prepare_transaction_calls{0};
    int m_schedule_fee_estimates_calls{0};
    int m_send_transaction_calls{0};
    int m_broadcast_current_transaction_calls{0};
    int m_discard_current_transaction_calls{0};
    MockAddressListModel m_address_list_model;
};

class MockWalletQmlModelTransaction : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address MEMBER m_address CONSTANT)
    Q_PROPERTY(QString label MEMBER m_label CONSTANT)
    Q_PROPERTY(QString amount MEMBER m_amount CONSTANT)
    Q_PROPERTY(QString fee MEMBER m_fee CONSTANT)
    Q_PROPERTY(QString total MEMBER m_total CONSTANT)
    Q_PROPERTY(QObject* amountAmount READ amountAmount CONSTANT)
    Q_PROPERTY(QObject* feeAmount READ feeAmount CONSTANT)
    Q_PROPERTY(QObject* totalAmount READ totalAmount CONSTANT)

public:
    MockWalletQmlModelTransaction()
    {
        m_amount_amount.m_display = QStringLiteral("0.01000000");
        m_fee_amount.m_display = QStringLiteral("0.00001000");
        m_total_amount.m_display = QStringLiteral("0.01001000");
    }

    QString m_address{QStringLiteral("bcrt1qexampleaddress")};
    QString m_label{QStringLiteral("example-label")};
    QString m_amount{QStringLiteral("0.01000000 BTC")};
    QString m_fee{QStringLiteral("0.00001000 BTC")};
    QString m_total{QStringLiteral("0.01001000 BTC")};
    MockBitcoinAmount m_amount_amount;
    MockBitcoinAmount m_fee_amount;
    MockBitcoinAmount m_total_amount;

    QObject* amountAmount() { return &m_amount_amount; }
    QObject* feeAmount() { return &m_fee_amount; }
    QObject* totalAmount() { return &m_total_amount; }
};

class MockWalletController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool initialized MEMBER m_initialized NOTIFY initializedChanged)
    Q_PROPERTY(bool isWalletLoaded MEMBER m_is_wallet_loaded NOTIFY isWalletLoadedChanged)
    Q_PROPERTY(bool noWalletsFound MEMBER m_no_wallets_found NOTIFY noWalletsFoundChanged)
    Q_PROPERTY(bool walletLoadInProgress MEMBER m_wallet_load_in_progress NOTIFY walletLoadInProgressChanged)
    Q_PROPERTY(QString walletLoadError MEMBER m_wallet_load_error NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(QString walletLoadWarnings MEMBER m_wallet_load_warnings NOTIFY walletLoadWarningsChanged)
    Q_PROPERTY(QString walletImportErrorTitle READ walletImportErrorTitle NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(QString walletImportErrorDescription READ walletImportErrorDescription NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(QString walletImportErrorHelpText READ walletImportErrorHelpText NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(QString walletCreateError MEMBER m_wallet_create_error NOTIFY walletCreateErrorChanged)
    Q_PROPERTY(bool walletMigrationInProgress MEMBER m_wallet_migration_in_progress NOTIFY walletMigrationInProgressChanged)
    Q_PROPERTY(QString walletMigrationError MEMBER m_wallet_migration_error NOTIFY walletMigrationErrorChanged)
    Q_PROPERTY(QString lastImportedWalletName MEMBER m_last_imported_wallet_name NOTIFY lastImportedWalletInfoChanged)
    Q_PROPERTY(QString lastImportedWalletKeyScheme MEMBER m_last_imported_wallet_key_scheme NOTIFY lastImportedWalletInfoChanged)
    Q_PROPERTY(bool canCreateExternalSignerWallet MEMBER m_can_create_external_signer_wallet NOTIFY externalSignerStatusChanged)
    Q_PROPERTY(QString externalSignerName MEMBER m_external_signer_name NOTIFY externalSignerStatusChanged)
    Q_PROPERTY(QString externalSignerError MEMBER m_external_signer_error NOTIFY externalSignerStatusChanged)
    Q_PROPERTY(QString suggestedExternalSignerWalletName MEMBER m_suggested_external_signer_wallet_name NOTIFY externalSignerStatusChanged)
    Q_PROPERTY(QString lastSelectedWalletName READ lastSelectedWalletName NOTIFY lastSelectedWalletNameChanged)
    Q_PROPERTY(QString lastClosedWalletName READ lastClosedWalletName NOTIFY lastClosedWalletNameChanged)
    Q_PROPERTY(int closeWalletCalls READ closeWalletCalls NOTIFY closeWalletCallsChanged)
    Q_PROPERTY(QObject* selectedWallet READ selectedWallet NOTIFY selectedWalletChanged)
    Q_PROPERTY(int closePaymentRequestDetailRequests MEMBER m_close_payment_request_detail_requests NOTIFY closePaymentRequestDetailRequestsChanged)
    Q_PROPERTY(int openReceiveRequests MEMBER m_open_receive_requests NOTIFY openReceiveRequestsChanged)
    Q_PROPERTY(QString walletLocationOpenError READ walletLocationOpenError NOTIFY walletLocationOpenErrorChanged)
    Q_PROPERTY(int openSelectedWalletLocationCalls READ openSelectedWalletLocationCalls NOTIFY openSelectedWalletLocationCallsChanged)

public:
    bool m_initialized{true};
    bool m_is_wallet_loaded{true};
    bool m_no_wallets_found{false};
    bool m_wallet_load_in_progress{false};
    QString m_wallet_load_error;
    QString m_wallet_load_warnings;
    QString m_wallet_create_error;
    bool m_wallet_migration_in_progress{false};
    QString m_wallet_migration_error;
    QString m_last_imported_wallet_name;
    QString m_last_imported_wallet_key_scheme;
    bool m_can_create_external_signer_wallet{false};
    QString m_external_signer_name;
    QString m_external_signer_error;
    QString m_suggested_external_signer_wallet_name{QStringLiteral("external_signer")};
    QObject* m_selected_wallet{nullptr};
    QString m_last_selected_wallet_name;
    QString m_last_closed_wallet_name;
    int m_close_wallet_calls{0};
    int m_close_payment_request_detail_requests{0};
    int m_open_receive_requests{0};
    QString m_wallet_location_open_error;
    bool m_open_selected_wallet_location_result{true};
    QString m_open_selected_wallet_location_error;
    int m_open_selected_wallet_location_calls{0};

    QObject* selectedWallet() const { return m_selected_wallet; }
    QString lastSelectedWalletName() const { return m_last_selected_wallet_name; }
    QString lastClosedWalletName() const { return m_last_closed_wallet_name; }
    int closeWalletCalls() const { return m_close_wallet_calls; }
    QString walletLocationOpenError() const { return m_wallet_location_open_error; }
    int openSelectedWalletLocationCalls() const { return m_open_selected_wallet_location_calls; }
    QString walletImportErrorTitle() const { return m_wallet_load_error.isEmpty() ? QString{} : QStringLiteral("Failed to load wallet"); }
    QString walletImportErrorDescription() const { return m_wallet_load_error; }
    QString walletImportErrorHelpText() const { return QString{}; }
    Q_INVOKABLE QString homePath() const { return QStringLiteral("/tmp"); }
    Q_INVOKABLE QString normalizeWalletPath(const QString& path) const { return path; }
    Q_INVOKABLE bool walletPathExists(const QString&) const { return false; }
    Q_INVOKABLE QString walletNameAvailabilityError(const QString&) const { return QString{}; }
    Q_INVOKABLE void refreshExternalSignerStatus() { Q_EMIT externalSignerStatusChanged(); }
    Q_INVOKABLE void clearWalletLoadStatus()
    {
        m_wallet_load_error.clear();
        m_wallet_load_warnings.clear();
        m_wallet_load_in_progress = false;
        Q_EMIT walletLoadErrorChanged();
        Q_EMIT walletLoadWarningsChanged();
        Q_EMIT walletLoadInProgressChanged();
    }
    Q_INVOKABLE void clearWalletCreateStatus()
    {
        m_wallet_create_error.clear();
        Q_EMIT walletCreateErrorChanged();
    }
    Q_INVOKABLE void clearWalletMigrationStatus()
    {
        m_wallet_migration_in_progress = false;
        m_wallet_migration_error.clear();
        Q_EMIT walletMigrationInProgressChanged();
        Q_EMIT walletMigrationErrorChanged();
    }
    Q_INVOKABLE void createWatchOnlyWallet(const QString& /*name*/, const QString& /*xpub*/)
    {
        clearWalletLoadStatus();
        Q_EMIT walletCreateSucceeded();
    }
    Q_INVOKABLE void createSingleSigWallet(const QString&, const QString&)
    {
        clearWalletCreateStatus();
        clearWalletLoadStatus();
        Q_EMIT walletCreateSucceeded();
    }
    Q_INVOKABLE bool createExternalSignerWallet(const QString&)
    {
        clearWalletLoadStatus();
        if (!m_can_create_external_signer_wallet) {
            m_wallet_load_error = QStringLiteral("Connect an external signer and try again.");
            Q_EMIT walletLoadErrorChanged();
            return false;
        }
        Q_EMIT walletCreateSucceeded();
        return true;
    }
    Q_INVOKABLE void importWallet(const QString&)
    {
        clearWalletLoadStatus();
        Q_EMIT walletImportSucceeded();
    }
    Q_INVOKABLE void migrateWallet(const QString&, const QString& = QString())
    {
        clearWalletMigrationStatus();
        Q_EMIT walletMigrationSucceeded();
    }
    Q_INVOKABLE void requestOpenWalletSettings() { Q_EMIT openWalletSettingsRequested(); }
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
    Q_INVOKABLE void setInitialized(bool initialized)
    {
        if (m_initialized == initialized) return;
        m_initialized = initialized;
        Q_EMIT initializedChanged();
    }
    Q_INVOKABLE void setWalletLoaded(bool loaded)
    {
        if (m_is_wallet_loaded == loaded) return;
        m_is_wallet_loaded = loaded;
        Q_EMIT isWalletLoadedChanged();
    }
    Q_INVOKABLE void setNoWalletsFound(bool no_wallets_found)
    {
        if (m_no_wallets_found == no_wallets_found) return;
        m_no_wallets_found = no_wallets_found;
        Q_EMIT noWalletsFoundChanged();
    }
    Q_INVOKABLE void reset()
    {
        m_last_selected_wallet_name.clear();
        m_last_closed_wallet_name.clear();
        m_close_wallet_calls = 0;
        m_close_payment_request_detail_requests = 0;
        m_open_receive_requests = 0;
        m_open_selected_wallet_location_calls = 0;
        m_open_selected_wallet_location_result = true;
        m_open_selected_wallet_location_error.clear();
        clearWalletLocationOpenError();
        clearWalletLoadStatus();
        clearWalletCreateStatus();
        clearWalletMigrationStatus();
        m_last_imported_wallet_name.clear();
        m_last_imported_wallet_key_scheme.clear();
        m_can_create_external_signer_wallet = false;
        m_external_signer_name.clear();
        m_external_signer_error.clear();
        m_suggested_external_signer_wallet_name = QStringLiteral("external_signer");
        Q_EMIT lastSelectedWalletNameChanged();
        Q_EMIT lastClosedWalletNameChanged();
        Q_EMIT closeWalletCallsChanged();
        Q_EMIT closePaymentRequestDetailRequestsChanged();
        Q_EMIT openReceiveRequestsChanged();
        Q_EMIT openSelectedWalletLocationCallsChanged();
        Q_EMIT lastImportedWalletInfoChanged();
        Q_EMIT externalSignerStatusChanged();
    }
    Q_INVOKABLE void requestClosePaymentRequestDetail()
    {
        ++m_close_payment_request_detail_requests;
        Q_EMIT closePaymentRequestDetailRequestsChanged();
        Q_EMIT closePaymentRequestDetailRequested();
    }
    Q_INVOKABLE void requestOpenReceive()
    {
        ++m_open_receive_requests;
        Q_EMIT openReceiveRequestsChanged();
        Q_EMIT openReceiveRequested();
    }
    Q_INVOKABLE bool validateXpub(const QString& xpub)
    {
        QString t = xpub.trimmed();
        return t.length() >= 100 && (t.startsWith("xpub") || t.startsWith("tpub"));
    }
    Q_INVOKABLE void setOpenSelectedWalletLocationResult(const bool result, const QString& error = QString())
    {
        m_open_selected_wallet_location_result = result;
        m_open_selected_wallet_location_error = error;
    }
    Q_INVOKABLE bool openSelectedWalletLocation()
    {
        ++m_open_selected_wallet_location_calls;
        Q_EMIT openSelectedWalletLocationCallsChanged();
        if (m_open_selected_wallet_location_result) {
            clearWalletLocationOpenError();
            return true;
        }
        m_wallet_location_open_error = m_open_selected_wallet_location_error.isEmpty()
            ? QStringLiteral("Could not open wallet file location.")
            : m_open_selected_wallet_location_error;
        Q_EMIT walletLocationOpenErrorChanged();
        return false;
    }
    Q_INVOKABLE void clearWalletLocationOpenError()
    {
        if (m_wallet_location_open_error.isEmpty()) return;
        m_wallet_location_open_error.clear();
        Q_EMIT walletLocationOpenErrorChanged();
    }

Q_SIGNALS:
    void initializedChanged();
    void isWalletLoadedChanged();
    void noWalletsFoundChanged();
    void walletLoadInProgressChanged();
    void walletLoadErrorChanged();
    void walletLoadWarningsChanged();
    void walletCreateErrorChanged();
    void walletMigrationInProgressChanged();
    void walletMigrationErrorChanged();
    void lastImportedWalletInfoChanged();
    void externalSignerStatusChanged();
    void lastSelectedWalletNameChanged();
    void lastClosedWalletNameChanged();
    void closeWalletCallsChanged();
    void selectedWalletChanged();
    void closePaymentRequestDetailRequestsChanged();
    void closePaymentRequestDetailRequested();
    void openReceiveRequestsChanged();
    void openReceiveRequested();
    void openWalletSettingsRequested();
    void walletCreateSucceeded();
    void walletImportSucceeded();
    void walletMigrationSucceeded();
    void walletLocationOpenErrorChanged();
    void openSelectedWalletLocationCallsChanged();
};

class MockCoreSettingsModel;

class MockCoreSettingEntryModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString key READ key CONSTANT)
    Q_PROPERTY(QVariant value READ value WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString address READ address WRITE setAddress NOTIFY addressChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY statusChanged)
    Q_PROPERTY(QString infoText READ infoText NOTIFY statusChanged)

public:
    MockCoreSettingEntryModel(QString key, MockCoreSettingsModel& model, QObject* parent = nullptr);

    QString key() const { return m_key; }
    QVariant value() const;
    void setValue(const QVariant& value);
    bool enabled() const;
    void setEnabled(bool enabled);
    QString address() const;
    void setAddress(const QString& address);
    QVariantMap status() const;
    bool canEdit() const;
    QString infoText() const;

    Q_INVOKABLE QString validate(const QString& value) const;
    Q_INVOKABLE bool commitAddress(const QString& address);
    Q_INVOKABLE QString defaultAddress() const;

Q_SIGNALS:
    void valueChanged();
    void enabledChanged();
    void addressChanged();
    void statusChanged();

private:
    QString m_key;
    MockCoreSettingsModel& m_model;
};

class MockCoreSettingsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap statuses READ statuses NOTIFY statusesChanged)

public:
    explicit MockCoreSettingsModel(QObject* parent = nullptr) : QObject{parent} {}

    Q_INVOKABLE QObject* entry(const QString& key)
    {
        if (MockCoreSettingEntryModel* existing = m_entries.value(key)) return existing;
        auto* entry = new MockCoreSettingEntryModel{key, *this, this};
        m_entries.insert(key, entry);
        return entry;
    }

    bool listen() const { return m_listen; }
    bool natpmp() const { return m_natpmp; }
    bool server() const { return m_server; }
    bool prune() const { return m_prune; }
    int pruneSizeGB() const { return m_prune_size_gb; }
    bool proxyEnabled() const { return m_proxy_enabled; }
    bool torEnabled() const { return m_tor_enabled; }
    QString proxyAddress() const { return m_proxy_address; }
    QString torAddress() const { return m_tor_address; }
    QVariantMap statuses() const { return m_statuses; }
    QVariantMap status(const QString& key) const { return m_statuses.value(key).toMap(); }
    bool canEdit(const QString& key) const { return status(key).value(QStringLiteral("canEdit"), true).toBool(); }
    QString defaultAddress() const { return QStringLiteral("127.0.0.1:9050"); }
    QString validateAddress(const QString& value) const { return value.length() > 0 ? QString{} : QStringLiteral("Proxy location is required."); }

    void setStatuses(const QVariantMap& statuses)
    {
        m_statuses = statuses;
        Q_EMIT statusesChanged();
        for (MockCoreSettingEntryModel* entry : std::as_const(m_entries)) {
            Q_EMIT entry->statusChanged();
        }
    }

    void setListen(bool value) { setBool(QStringLiteral("listen"), m_listen, value); }
    void setNatpmp(bool value) { setBool(QStringLiteral("natpmp"), m_natpmp, value); }
    void setServer(bool value) { setBool(QStringLiteral("server"), m_server, value); }
    void setPrune(bool value) { setBool(QStringLiteral("prune"), m_prune, value); }
    void setPruneSizeGB(int value)
    {
        if (!canEdit(QStringLiteral("prune")) || value == m_prune_size_gb || value < 1) return;
        m_prune_size_gb = value;
        Q_EMIT static_cast<MockCoreSettingEntryModel*>(entry(QStringLiteral("prune")))->valueChanged();
        Q_EMIT changed();
    }
    void setProxyEnabled(bool value) { setBool(QStringLiteral("proxy"), m_proxy_enabled, value); }
    void setTorEnabled(bool value) { setBool(QStringLiteral("onion"), m_tor_enabled, value); }
    bool setProxyAddress(const QString& value) { return setAddress(QStringLiteral("proxy"), m_proxy_address, value); }
    bool setTorAddress(const QString& value) { return setAddress(QStringLiteral("onion"), m_tor_address, value); }

Q_SIGNALS:
    void changed();
    void statusesChanged();

private:
    friend class MockCoreSettingEntryModel;

    void setBool(const QString& key, bool& field, bool value)
    {
        if (!canEdit(key) || value == field) return;
        field = value;
        auto* entry_model = static_cast<MockCoreSettingEntryModel*>(entry(key));
        Q_EMIT entry_model->valueChanged();
        Q_EMIT entry_model->enabledChanged();
        Q_EMIT changed();
    }

    bool setAddress(const QString& key, QString& field, const QString& value)
    {
        if (!canEdit(key) || !validateAddress(value).isEmpty()) return false;
        if (value == field) return true;
        field = value;
        auto* entry_model = static_cast<MockCoreSettingEntryModel*>(entry(key));
        Q_EMIT entry_model->addressChanged();
        Q_EMIT entry_model->valueChanged();
        Q_EMIT changed();
        return true;
    }

    bool m_listen{true};
    bool m_natpmp{false};
    bool m_server{false};
    bool m_prune{true};
    int m_prune_size_gb{2};
    bool m_proxy_enabled{false};
    bool m_tor_enabled{false};
    QString m_proxy_address{QStringLiteral("127.0.0.1:9050")};
    QString m_tor_address{QStringLiteral("127.0.0.1:9050")};
    QVariantMap m_statuses;
    QHash<QString, MockCoreSettingEntryModel*> m_entries;
};

MockCoreSettingEntryModel::MockCoreSettingEntryModel(QString key, MockCoreSettingsModel& model, QObject* parent)
    : QObject{parent}
    , m_key{std::move(key)}
    , m_model{model}
{
}

QVariant MockCoreSettingEntryModel::value() const
{
    if (m_key == QStringLiteral("listen")) return m_model.listen();
    if (m_key == QStringLiteral("natpmp")) return m_model.natpmp();
    if (m_key == QStringLiteral("server")) return m_model.server();
    if (m_key == QStringLiteral("prune")) return m_model.pruneSizeGB();
    if (m_key == QStringLiteral("proxy")) return m_model.proxyAddress();
    if (m_key == QStringLiteral("onion")) return m_model.torAddress();
    return {};
}

void MockCoreSettingEntryModel::setValue(const QVariant& value)
{
    if (m_key == QStringLiteral("listen")) {
        m_model.setListen(value.toBool());
    } else if (m_key == QStringLiteral("natpmp")) {
        m_model.setNatpmp(value.toBool());
    } else if (m_key == QStringLiteral("server")) {
        m_model.setServer(value.toBool());
    } else if (m_key == QStringLiteral("prune")) {
        m_model.setPruneSizeGB(value.toInt());
    } else if (m_key == QStringLiteral("proxy")) {
        m_model.setProxyAddress(value.toString());
    } else if (m_key == QStringLiteral("onion")) {
        m_model.setTorAddress(value.toString());
    }
}

bool MockCoreSettingEntryModel::enabled() const
{
    if (m_key == QStringLiteral("listen")) return m_model.listen();
    if (m_key == QStringLiteral("natpmp")) return m_model.natpmp();
    if (m_key == QStringLiteral("server")) return m_model.server();
    if (m_key == QStringLiteral("prune")) return m_model.prune();
    if (m_key == QStringLiteral("proxy")) return m_model.proxyEnabled();
    if (m_key == QStringLiteral("onion")) return m_model.torEnabled();
    return false;
}

void MockCoreSettingEntryModel::setEnabled(bool enabled)
{
    if (m_key == QStringLiteral("listen")) {
        m_model.setListen(enabled);
    } else if (m_key == QStringLiteral("natpmp")) {
        m_model.setNatpmp(enabled);
    } else if (m_key == QStringLiteral("server")) {
        m_model.setServer(enabled);
    } else if (m_key == QStringLiteral("prune")) {
        m_model.setPrune(enabled);
    } else if (m_key == QStringLiteral("proxy")) {
        m_model.setProxyEnabled(enabled);
    } else if (m_key == QStringLiteral("onion")) {
        m_model.setTorEnabled(enabled);
    }
}

QString MockCoreSettingEntryModel::address() const
{
    if (m_key == QStringLiteral("proxy")) return m_model.proxyAddress();
    if (m_key == QStringLiteral("onion")) return m_model.torAddress();
    return {};
}

void MockCoreSettingEntryModel::setAddress(const QString& address)
{
    commitAddress(address);
}

QVariantMap MockCoreSettingEntryModel::status() const
{
    return m_model.status(m_key);
}

bool MockCoreSettingEntryModel::canEdit() const
{
    return status().value(QStringLiteral("canEdit"), true).toBool();
}

QString MockCoreSettingEntryModel::infoText() const
{
    return status().value(QStringLiteral("infoText")).toString();
}

QString MockCoreSettingEntryModel::validate(const QString& value) const
{
    return m_model.validateAddress(value);
}

bool MockCoreSettingEntryModel::commitAddress(const QString& address)
{
    if (m_key == QStringLiteral("proxy")) return m_model.setProxyAddress(address);
    if (m_key == QStringLiteral("onion")) return m_model.setTorAddress(address);
    return false;
}

QString MockCoreSettingEntryModel::defaultAddress() const
{
    return m_model.defaultAddress();
}

class MockOptionsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool listen READ listen WRITE setListen NOTIFY listenChanged)
    Q_PROPERTY(bool natpmp READ natpmp WRITE setNatpmp NOTIFY natpmpChanged)
    Q_PROPERTY(bool server READ server WRITE setServer NOTIFY serverChanged)
    Q_PROPERTY(int maxMempoolSizeMB READ maxMempoolSizeMB WRITE setMaxMempoolSizeMB NOTIFY maxMempoolSizeMBChanged)
    Q_PROPERTY(int maxMaxMempoolSizeMB MEMBER m_max_max_mempool_size_mb CONSTANT)
    Q_PROPERTY(int minMaxMempoolSizeMB MEMBER m_min_max_mempool_size_mb CONSTANT)
    Q_PROPERTY(bool prune READ prune WRITE setPrune NOTIFY pruneChanged)
    Q_PROPERTY(int pruneSizeGB READ pruneSizeGB WRITE setPruneSizeGB NOTIFY pruneSizeGBChanged)
    Q_PROPERTY(QString dataDir MEMBER m_data_dir NOTIFY dataDirChanged)
    Q_PROPERTY(QString getDefaultDataDirString READ getDefaultDataDirString CONSTANT)
    Q_PROPERTY(int displayUnit READ displayUnit WRITE setDisplayUnit NOTIFY displayUnitChanged)
    Q_PROPERTY(QString displayUnitLabel READ displayUnitLabel NOTIFY displayUnitChanged)
    Q_PROPERTY(QString languageSummary READ languageSummary NOTIFY languageChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages CONSTANT)
    Q_PROPERTY(bool connectionSettingsDirty MEMBER m_connection_settings_dirty NOTIFY connectionSettingsDirtyChanged)
    Q_PROPERTY(bool storageSettingsDirty MEMBER m_storage_settings_dirty NOTIFY storageSettingsDirtyChanged)
    Q_PROPERTY(bool developerSettingsDirty MEMBER m_developer_settings_dirty NOTIFY developerSettingsDirtyChanged)
    Q_PROPERTY(bool mempoolSettingsDirty MEMBER m_mempool_settings_dirty NOTIFY mempoolSettingsDirtyChanged)
    Q_PROPERTY(bool proxySettingsDirty MEMBER m_proxy_settings_dirty NOTIFY proxySettingsDirtyChanged)
    Q_PROPERTY(bool walletSettingsDirty MEMBER m_wallet_settings_dirty NOTIFY walletSettingsDirtyChanged)
    Q_PROPERTY(bool restartRequired MEMBER m_restart_required NOTIFY restartRequiredChanged)
    Q_PROPERTY(QObject* coreSettings READ coreSettings CONSTANT)
    Q_PROPERTY(QVariantMap coreSettingStatuses READ coreSettingStatuses NOTIFY coreSettingStatusesChanged)
    Q_PROPERTY(QString previewError MEMBER m_preview_error NOTIFY previewErrorChanged)
    Q_PROPERTY(bool canFinish READ canFinish NOTIFY canFinishChanged)
    Q_PROPERTY(int assumedBlockchainSize MEMBER m_assumed_blockchain_size NOTIFY assumedSizesChanged)
    Q_PROPERTY(int assumedChainstateSize MEMBER m_assumed_chainstate_size NOTIFY assumedSizesChanged)
    Q_PROPERTY(bool existingProfile READ existingProfile WRITE setExistingProfile NOTIFY storageStatusChanged)
    Q_PROPERTY(bool storageCheckPending MEMBER m_storage_check_pending NOTIFY storageStatusChanged)
    Q_PROPERTY(QString storageStatus READ storageStatus NOTIFY storageStatusChanged)
    Q_PROPERTY(int storageAvailableGB MEMBER m_storage_available_gb NOTIFY storageStatusChanged)
    Q_PROPERTY(QString storageAvailableText READ storageAvailableText NOTIFY storageStatusChanged)
    Q_PROPERTY(QString storagePathMessage MEMBER m_storage_path_message NOTIFY storageStatusChanged)
    Q_PROPERTY(QString storageWarningText MEMBER m_storage_warning_text NOTIFY storageStatusChanged)
    Q_PROPERTY(QString storageErrorText MEMBER m_storage_error_text NOTIFY storageStatusChanged)
    Q_PROPERTY(int storageMinimumRequiredGB READ storageMinimumRequiredGB NOTIFY storageStatusChanged)
    Q_PROPERTY(int fullStorageRequiredGB READ fullStorageRequiredGB NOTIFY storageStatusChanged)
    Q_PROPERTY(int prunedStorageRequiredGB READ prunedStorageRequiredGB NOTIFY storageStatusChanged)
    Q_PROPERTY(int selectedStorageRequiredGB READ selectedStorageRequiredGB NOTIFY storageStatusChanged)
    Q_PROPERTY(bool storageEnoughForSelected READ storageEnoughForSelected NOTIFY storageStatusChanged)
    Q_PROPERTY(bool storageEnoughForFull READ storageEnoughForFull NOTIFY storageStatusChanged)
    Q_PROPERTY(QString thirdPartyTransactionUrls MEMBER m_third_party_transaction_urls NOTIFY thirdPartyTransactionUrlsChanged)
    Q_PROPERTY(QString moneyFontChoice MEMBER m_money_font_choice NOTIFY moneyFontChoiceChanged)
    Q_PROPERTY(QString externalSignerPath MEMBER m_external_signer_path NOTIFY externalSignerPathChanged)
    Q_PROPERTY(QFont moneyFont READ moneyFont NOTIFY moneyFontChanged)

public:
    bool m_listen{true};
    bool m_natpmp{false};
    bool m_server{false};
    int m_max_mempool_size_mb{300};
    int m_max_max_mempool_size_mb{99999};
    int m_min_max_mempool_size_mb{1};
    bool m_prune{true};
    int m_prune_size_gb{2};
    QString m_data_dir{QStringLiteral("/tmp/bitcoin-default")};
    QString m_custom_data_dir{QStringLiteral("/tmp/bitcoin-custom")};
    bool m_connection_settings_dirty{false};
    bool m_storage_settings_dirty{false};
    bool m_developer_settings_dirty{false};
    bool m_mempool_settings_dirty{false};
    bool m_proxy_settings_dirty{false};
    bool m_wallet_settings_dirty{false};
    bool m_restart_required{false};
    QString m_preview_error;
    int m_assumed_blockchain_size{610};
    int m_assumed_chainstate_size{12};
    bool m_existing_profile{false};
    bool m_storage_check_pending{false};
    int m_storage_available_gb{123};
    QString m_storage_path_message{QStringLiteral("Directory already exists.")};
    QString m_storage_warning_text;
    QString m_storage_error_text;
    QString m_third_party_transaction_urls;
    QString m_money_font_choice{QStringLiteral("embedded")};
    QString m_external_signer_path;
    QVariantMap m_core_setting_status_overrides;
    MockCoreSettingsModel m_core_settings;

    MockOptionsModel()
    {
        connect(&m_core_settings, &MockCoreSettingsModel::changed, this, &MockOptionsModel::syncCoreSettingsFromDocument);
        m_core_settings.setListen(m_listen);
        m_core_settings.setNatpmp(m_natpmp);
        m_core_settings.setServer(m_server);
        m_core_settings.setPrune(m_prune);
        m_core_settings.setPruneSizeGB(m_prune_size_gb);
        m_core_settings.setStatuses(coreSettingStatuses());
    }

    bool listen() const { return m_core_settings.listen(); }
    void setListen(bool value) { m_core_settings.setListen(value); }
    bool natpmp() const { return m_core_settings.natpmp(); }
    void setNatpmp(bool value) { m_core_settings.setNatpmp(value); }
    bool server() const { return m_core_settings.server(); }
    void setServer(bool value) { m_core_settings.setServer(value); }
    int maxMempoolSizeMB() const { return m_max_mempool_size_mb; }
    void setMaxMempoolSizeMB(int value)
    {
        if (value == m_max_mempool_size_mb) return;
        m_max_mempool_size_mb = value;
        Q_EMIT maxMempoolSizeMBChanged(value);
    }
    QString getDefaultDataDirString() const { return QStringLiteral("/tmp/bitcoin-default"); }
    bool canFinish() const { return m_preview_error.isEmpty() && !m_storage_check_pending && m_storage_error_text.isEmpty(); }
    QString storageStatus() const
    {
        if (m_storage_check_pending) return QStringLiteral("checking");
        if (!m_storage_error_text.isEmpty()) return QStringLiteral("error");
        if (!m_storage_warning_text.isEmpty()) return QStringLiteral("warning");
        return QStringLiteral("ok");
    }
    QString storageAvailableText() const
    {
        return m_storage_check_pending ? QStringLiteral("Checking available storage...") : QStringLiteral("%1GB available").arg(m_storage_available_gb);
    }
    bool existingProfile() const { return m_existing_profile; }
    void setExistingProfile(bool value)
    {
        if (m_existing_profile == value) return;
        m_existing_profile = value;
        Q_EMIT storageStatusChanged();
        Q_EMIT canFinishChanged();
    }
    int storageMinimumRequiredGB() const { return m_existing_profile ? 1 : m_assumed_chainstate_size + 2; }
    int fullStorageRequiredGB() const { return m_assumed_blockchain_size + m_assumed_chainstate_size; }
    bool prune() const { return m_core_settings.prune(); }
    void setPrune(bool value) { m_core_settings.setPrune(value); }
    int pruneSizeGB() const { return m_core_settings.pruneSizeGB(); }
    void setPruneSizeGB(int value) { m_core_settings.setPruneSizeGB(value); }
    int prunedStorageRequiredGB() const { return pruneSizeGB() + m_assumed_chainstate_size; }
    int selectedStorageRequiredGB() const { return m_existing_profile ? storageMinimumRequiredGB() : (prune() ? prunedStorageRequiredGB() : fullStorageRequiredGB()); }
    bool storageEnoughForSelected() const { return m_storage_available_gb >= selectedStorageRequiredGB(); }
    bool storageEnoughForFull() const { return m_existing_profile ? storageEnoughForSelected() : m_storage_available_gb >= fullStorageRequiredGB(); }
    Q_INVOKABLE QString getCustomDataDirString() const { return m_custom_data_dir; }
    Q_INVOKABLE void setCustomDataDirString(const QString& dir) { m_custom_data_dir = dir; }
    Q_INVOKABLE void setCustomDataDirArgs(const QString& dir) { selectCustomDataDir(dir); }
    Q_INVOKABLE QString validateCustomDataDir(const QString&) const { return {}; }
    Q_INVOKABLE bool selectCustomDataDir(const QString& dir) { m_custom_data_dir = dir; m_data_dir = dir; Q_EMIT dataDirChanged(); return true; }
    Q_INVOKABLE void useDefaultDataDir() { m_data_dir = getDefaultDataDirString(); Q_EMIT dataDirChanged(); }
    Q_INVOKABLE void setStorageStatusForTest(bool pending, int available_gb, const QString& error_text, const QString& warning_text) {
        m_storage_check_pending = pending;
        m_storage_available_gb = available_gb;
        m_storage_error_text = error_text;
        m_storage_warning_text = warning_text;
        Q_EMIT storageStatusChanged();
        Q_EMIT canFinishChanged();
    }
    Q_INVOKABLE QString validateProxyLocation(const QString& location) const { return location.length() > 0 ? QString{} : QStringLiteral("Proxy location is required."); }
    Q_INVOKABLE bool commitProxyLocation(const QString&) { return true; }
    Q_INVOKABLE bool commitTorLocation(const QString&) { return true; }
    Q_INVOKABLE QString defaultProxyAddress() const { return QStringLiteral("127.0.0.1:9050"); }
    QObject* coreSettings() { return &m_core_settings; }
    QVariantMap coreSettingStatuses() const {
        QVariantMap statuses;
        const QStringList names{
            QStringLiteral("listen"),
            QStringLiteral("natpmp"),
            QStringLiteral("server"),
            QStringLiteral("prune"),
            QStringLiteral("dbcache"),
            QStringLiteral("par"),
            QStringLiteral("maxmempool"),
            QStringLiteral("proxy"),
            QStringLiteral("onion"),
            QStringLiteral("signer"),
            QStringLiteral("lang"),
        };
        for (const QString& name : names) {
            statuses.insert(name, coreSettingStatus(name));
        }
        return statuses;
    }
    Q_INVOKABLE QVariantMap coreSettingStatus(const QString& name) const {
        if (m_core_setting_status_overrides.contains(name)) {
            return m_core_setting_status_overrides.value(name).toMap();
        }
        return defaultCoreSettingStatus();
    }
    Q_INVOKABLE void setCoreSettingStatusForTest(const QString& name, bool can_edit, const QString& source, const QString& info_text, bool creates_gui_override) {
        QVariantMap status = defaultCoreSettingStatus();
        status.insert(QStringLiteral("source"), source);
        status.insert(QStringLiteral("canEdit"), can_edit);
        status.insert(QStringLiteral("commandLineOverridden"), !can_edit);
        status.insert(QStringLiteral("hasRwSetting"), source == QStringLiteral("settings_json"));
        status.insert(QStringLiteral("hasConfigSetting"), source == QStringLiteral("bitcoin_conf"));
        status.insert(QStringLiteral("createsGuiOverride"), creates_gui_override);
        status.insert(QStringLiteral("infoText"), info_text);
        m_core_setting_status_overrides.insert(name, status);
        m_core_settings.setStatuses(coreSettingStatuses());
        Q_EMIT coreSettingStatusesChanged();
    }
    Q_INVOKABLE void clearCoreSettingStatusesForTest() {
        m_core_setting_status_overrides.clear();
        m_core_settings.setStatuses(coreSettingStatuses());
        Q_EMIT coreSettingStatusesChanged();
    }
    QVariantMap defaultCoreSettingStatus() const {
        QVariantMap status;
        status.insert(QStringLiteral("source"), QStringLiteral("default"));
        status.insert(QStringLiteral("canEdit"), true);
        status.insert(QStringLiteral("commandLineOverridden"), false);
        status.insert(QStringLiteral("hasRwSetting"), false);
        status.insert(QStringLiteral("hasConfigSetting"), false);
        status.insert(QStringLiteral("createsGuiOverride"), false);
        status.insert(QStringLiteral("infoText"), QString{});
        return status;
    }
    Q_INVOKABLE QVariantList thirdPartyTransactionLinks(const QString& txid) const {
        QVariantList result;
        if (m_third_party_transaction_urls.isEmpty()) return result;
        QVariantMap link;
        link.insert("host", "example.com");
        link.insert("url", QString("https://example.com/tx/%1").arg(txid));
        result.push_back(link);
        return result;
    }

    int displayUnit() const { return m_displayUnit; }
    void setDisplayUnit(int u) {
        if (u != m_displayUnit) { m_displayUnit = u; Q_EMIT displayUnitChanged(u); }
    }
    QString displayUnitLabel() const
    {
        if (m_displayUnit == 1) return QStringLiteral("mBTC");
        if (m_displayUnit == 2) return QStringLiteral("bits");
        if (m_displayUnit == 3) return QStringLiteral("sat");
        return QStringLiteral("BTC");
    }
    Q_INVOKABLE QString displayUnitLabelForAmount(qint64 satoshi) const {
        if (m_displayUnit == 1) return QString("mBTC");
        if (m_displayUnit == 2) return QString("bits");
        if (m_displayUnit != 3) return QString("₿");
        return (qAbs(satoshi) == 1) ? QString("sat") : QString("sats");
    }
    QString language() const { return m_language; }
    void setLanguage(const QString& l) {
        if (l != m_language) { m_language = l; Q_EMIT languageChanged(); }
    }
    QString languageSummary() const { return m_language.isEmpty() ? "System default" : m_language; }
    QStringList availableLanguages() const { return {"", "de", "es", "fr"}; }
    QFont moneyFont() const { return QFont(QStringLiteral("Roboto Mono")); }
    Q_INVOKABLE QString languageLabel(const QString& tag) const {
        if (tag.isEmpty()) return "System default";
        if (tag == "de") return "Deutsch — German";
        if (tag == "es") return "Español — Spanish";
        if (tag == "fr") return "Français — French";
        return tag;
    }

    void syncCoreSettingsFromDocument() {
        if (m_listen != m_core_settings.listen()) {
            m_listen = m_core_settings.listen();
            Q_EMIT listenChanged();
        }
        if (m_natpmp != m_core_settings.natpmp()) {
            m_natpmp = m_core_settings.natpmp();
            Q_EMIT natpmpChanged();
        }
        if (m_server != m_core_settings.server()) {
            m_server = m_core_settings.server();
            Q_EMIT serverChanged();
        }
        if (m_prune != m_core_settings.prune()) {
            m_prune = m_core_settings.prune();
            Q_EMIT pruneChanged();
            Q_EMIT storageStatusChanged();
        }
        if (m_prune_size_gb != m_core_settings.pruneSizeGB()) {
            m_prune_size_gb = m_core_settings.pruneSizeGB();
            Q_EMIT pruneSizeGBChanged();
            Q_EMIT storageStatusChanged();
        }
    }

Q_SIGNALS:
    void listenChanged();
    void natpmpChanged();
    void serverChanged();
    void maxMempoolSizeMBChanged(int value);
    void pruneChanged();
    void pruneSizeGBChanged();
    void dataDirChanged();
    void displayUnitChanged(int unit);
    void languageChanged();
    void connectionSettingsDirtyChanged();
    void storageSettingsDirtyChanged();
    void developerSettingsDirtyChanged();
    void mempoolSettingsDirtyChanged();
    void proxySettingsDirtyChanged();
    void walletSettingsDirtyChanged();
    void restartRequiredChanged();
    void coreSettingStatusesChanged();
    void previewErrorChanged();
    void canFinishChanged();
    void assumedSizesChanged();
    void storageStatusChanged();
    void thirdPartyTransactionUrlsChanged();
    void moneyFontChoiceChanged();
    void externalSignerPathChanged();
    void moneyFontChanged();

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
    Q_PROPERTY(int numPeers MEMBER m_num_peers NOTIFY numPeersChanged)
    Q_PROPERTY(int numInboundPeers MEMBER m_num_inbound_peers NOTIFY numInboundPeersChanged)
    Q_PROPERTY(int numOutboundPeers MEMBER m_num_outbound_peers NOTIFY numOutboundPeersChanged)
    Q_PROPERTY(int maxNumOutboundPeers MEMBER m_max_num_outbound_peers NOTIFY maxNumOutboundPeersChanged)
    Q_PROPERTY(double verificationProgress MEMBER m_verification_progress NOTIFY verificationProgressChanged)
    Q_PROPERTY(int remainingSyncTime MEMBER m_remaining_sync_time NOTIFY remainingSyncTimeChanged)
    Q_PROPERTY(bool headerSyncActive MEMBER m_header_sync_active NOTIFY headerSyncChanged)
    Q_PROPERTY(bool blockSyncActive MEMBER m_block_sync_active NOTIFY blockSyncActiveChanged)
    Q_PROPERTY(bool headerPresync MEMBER m_header_presync NOTIFY headerSyncChanged)
    Q_PROPERTY(double headerSyncProgress MEMBER m_header_sync_progress NOTIFY headerSyncChanged)
    Q_PROPERTY(bool faulted MEMBER m_faulted NOTIFY faultedChanged)
    Q_PROPERTY(QString startupError MEMBER m_startup_error NOTIFY startupErrorChanged)
    Q_PROPERTY(QString warnings MEMBER m_warnings NOTIFY warningsChanged)
    Q_PROPERTY(QStringList warningList MEMBER m_warning_list NOTIFY warningsChanged)
    Q_PROPERTY(bool hasWarnings READ hasWarnings NOTIFY warningsChanged)
    Q_PROPERTY(bool runtimeDialogVisible MEMBER m_runtime_dialog_visible NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogTitle MEMBER m_runtime_dialog_title NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogMessage MEMBER m_runtime_dialog_message NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogIcon MEMBER m_runtime_dialog_icon NOTIFY runtimeDialogChanged)
    Q_PROPERTY(unsigned int runtimeDialogButtons MEMBER m_runtime_dialog_buttons NOTIFY runtimeDialogChanged)
    Q_PROPERTY(bool runtimeDialogQuestion MEMBER m_runtime_dialog_question NOTIFY runtimeDialogChanged)
    Q_PROPERTY(int blockTipHeight MEMBER m_block_tip_height NOTIFY blockTipHeightChanged)
    Q_PROPERTY(int mempoolTransactionCount MEMBER m_mempool_transaction_count NOTIFY mempoolInfoChanged)
    Q_PROPERTY(double mempoolUsageMB MEMBER m_mempool_usage_mb NOTIFY mempoolInfoChanged)
    Q_PROPERTY(double mempoolMaxUsageMB MEMBER m_mempool_max_usage_mb NOTIFY mempoolInfoChanged)
    Q_PROPERTY(bool mempoolInfoPollingActive READ mempoolInfoPollingActive WRITE setMempoolInfoPollingActive NOTIFY mempoolInfoPollingActiveChanged)
    Q_PROPERTY(bool mempoolInformationAvailable MEMBER m_mempool_information_available NOTIFY mempoolInformationAvailableChanged)
    Q_PROPERTY(bool disconnectPeerResult MEMBER m_disconnect_peer_result NOTIFY peerActionStateChanged)
    Q_PROPERTY(bool banPeerResult MEMBER m_ban_peer_result NOTIFY peerActionStateChanged)
    Q_PROPERTY(int disconnectPeerCalls READ disconnectPeerCalls NOTIFY peerActionCallsChanged)
    Q_PROPERTY(int banPeerCalls READ banPeerCalls NOTIFY peerActionCallsChanged)

public:
    bool m_pause{false};
    int m_num_peers{0};
    int m_num_inbound_peers{0};
    int m_num_outbound_peers{0};
    int m_max_num_outbound_peers{8};
    double m_verification_progress{0.0};
    int m_remaining_sync_time{0};
    bool m_header_sync_active{false};
    bool m_block_sync_active{false};
    bool m_header_presync{false};
    double m_header_sync_progress{0.0};
    bool m_faulted{false};
    QString m_startup_error;
    QString m_warnings;
    QStringList m_warning_list;
    bool m_runtime_dialog_visible{false};
    QString m_runtime_dialog_title;
    QString m_runtime_dialog_message;
    QString m_runtime_dialog_icon{QStringLiteral("image://images/info-filled")};
    unsigned int m_runtime_dialog_buttons{0};
    bool m_runtime_dialog_question{false};
    int m_block_tip_height{0};
    int m_mempool_transaction_count{0};
    double m_mempool_usage_mb{0.0};
    double m_mempool_max_usage_mb{300.0};
    bool m_mempool_info_polling_active{false};
    bool m_mempool_information_available{true};
    bool m_disconnect_peer_result{true};
    bool m_ban_peer_result{true};
    int m_disconnect_peer_calls{0};
    int m_ban_peer_calls{0};
    bool hasWarnings() const { return !m_warning_list.isEmpty(); }
    bool mempoolInfoPollingActive() const { return m_mempool_info_polling_active; }
    int disconnectPeerCalls() const { return m_disconnect_peer_calls; }
    int banPeerCalls() const { return m_ban_peer_calls; }
    void setMempoolInfoPollingActive(bool active)
    {
        if (m_mempool_info_polling_active == active) return;
        m_mempool_info_polling_active = active;
        Q_EMIT mempoolInfoPollingActiveChanged(active);
    }

    Q_INVOKABLE void startNodeInitializionThread() {}
    Q_INVOKABLE void requestShutdown() { Q_EMIT requestedShutdown(); }
    Q_INVOKABLE void answerRuntimeDialog(unsigned int button)
    {
        Q_UNUSED(button);
        m_runtime_dialog_visible = false;
        Q_EMIT runtimeDialogChanged();
    }
    Q_INVOKABLE QVariantList nodeInformationRows() const
    {
        QVariantMap version;
        version.insert(QStringLiteral("label"), QStringLiteral("Client version"));
        version.insert(QStringLiteral("value"), QStringLiteral("Bitcoin Core test"));

        QVariantMap network;
        network.insert(QStringLiteral("label"), QStringLiteral("Network"));
        network.insert(QStringLiteral("value"), QStringLiteral("regtest"));

        QVariantMap peers;
        peers.insert(QStringLiteral("label"), QStringLiteral("Peers"));
        peers.insert(QStringLiteral("value"), QStringLiteral("0 total (0 inbound, 0 outbound)"));

        QVariantList rows;
        rows.push_back(QVariant::fromValue(version));
        rows.push_back(QVariant::fromValue(network));
        rows.push_back(QVariant::fromValue(peers));
        if (!m_warning_list.isEmpty()) {
            QVariantMap warnings;
            warnings.insert(QStringLiteral("label"), QStringLiteral("Warnings"));
            warnings.insert(QStringLiteral("value"), m_warning_list.join(QStringLiteral("\n")));
            rows.push_back(QVariant::fromValue(warnings));
        }
        return rows;
    }
    Q_INVOKABLE void setWarningsForTest(const QStringList& warnings)
    {
        m_warning_list = warnings;
        m_warnings = warnings.join(QStringLiteral("<hr />"));
        Q_EMIT warningsChanged();
    }
    Q_INVOKABLE void setBlockSyncActiveForTest(bool active)
    {
        if (m_block_sync_active == active) return;
        m_block_sync_active = active;
        Q_EMIT blockSyncActiveChanged();
    }
    Q_INVOKABLE void setRuntimeDialogForTest(const QString& title, const QString& message, unsigned int buttons, bool question)
    {
        m_runtime_dialog_title = title;
        m_runtime_dialog_message = message;
        m_runtime_dialog_icon = question ? QStringLiteral("image://images/alert-filled") : QStringLiteral("image://images/info-filled");
        m_runtime_dialog_buttons = buttons;
        m_runtime_dialog_question = question;
        m_runtime_dialog_visible = true;
        Q_EMIT runtimeDialogChanged();
    }
    Q_INVOKABLE void setStartupErrorForTest(const QString& error)
    {
        m_startup_error = error;
        m_faulted = !error.isEmpty();
        Q_EMIT startupErrorChanged();
        Q_EMIT faultedChanged();
    }
    Q_INVOKABLE void resetMempoolInfoPollingTestState()
    {
        setMempoolInfoPollingActive(false);
    }
    Q_INVOKABLE void resetPeerActionTestState()
    {
        m_disconnect_peer_result = true;
        m_ban_peer_result = true;
        m_disconnect_peer_calls = 0;
        m_ban_peer_calls = 0;
        Q_EMIT peerActionStateChanged();
        Q_EMIT peerActionCallsChanged();
    }
    Q_INVOKABLE QString defaultProxyAddress() const { return QStringLiteral("127.0.0.1:9050"); }
    Q_INVOKABLE bool validateProxyAddress(const QString& value) const
    {
        // Minimal test stub to emulate host:port and [ipv6]:port acceptance.
        static const QRegularExpression pattern{
            QStringLiteral(R"(^(\[[0-9A-Fa-f:.]+\]|[0-9]{1,3}(\.[0-9]{1,3}){3}):([0-9]{1,5})$)")
        };
        return pattern.match(value).hasMatch();
    }
    Q_INVOKABLE bool disconnectPeer(int node_id)
    {
        Q_UNUSED(node_id);
        ++m_disconnect_peer_calls;
        Q_EMIT peerActionCallsChanged();
        return m_disconnect_peer_result;
    }
    Q_INVOKABLE bool banPeer(const QString& raw_address, qint64 ban_duration)
    {
        Q_UNUSED(raw_address);
        Q_UNUSED(ban_duration);
        ++m_ban_peer_calls;
        Q_EMIT peerActionCallsChanged();
        return m_ban_peer_result;
    }

Q_SIGNALS:
    void requestedShutdown();
    void pauseChanged();
    void numPeersChanged();
    void numInboundPeersChanged();
    void numOutboundPeersChanged();
    void maxNumOutboundPeersChanged();
    void verificationProgressChanged();
    void remainingSyncTimeChanged();
    void headerSyncChanged();
    void blockSyncActiveChanged();
    void faultedChanged();
    void startupErrorChanged();
    void warningsChanged();
    void runtimeDialogChanged();
    void blockTipHeightChanged();
    void mempoolInfoChanged();
    void mempoolInfoPollingActiveChanged(bool active);
    void mempoolInformationAvailableChanged();
    void peerActionStateChanged();
    void peerActionCallsChanged();
};

class MockPeerTableModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int refreshCalls READ refreshCalls NOTIFY refreshCallsChanged)

public:
    Q_INVOKABLE void startAutoRefresh() {}
    Q_INVOKABLE void stopAutoRefresh() {}
    Q_INVOKABLE void refresh()
    {
        ++m_refresh_calls;
        Q_EMIT refreshCallsChanged();
    }
    Q_INVOKABLE void resetTestState()
    {
        m_refresh_calls = 0;
        Q_EMIT refreshCallsChanged();
    }
    int refreshCalls() const { return m_refresh_calls; }

Q_SIGNALS:
    void refreshCallsChanged();

private:
    int m_refresh_calls{0};
};

class MockNetworkTrafficTower : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(quint64 totalBytesReceived MEMBER m_total_bytes_received NOTIFY totalBytesReceivedChanged)
    Q_PROPERTY(quint64 totalBytesSent MEMBER m_total_bytes_sent NOTIFY totalBytesSentChanged)
    Q_PROPERTY(double maxReceivedRateBps MEMBER m_max_received_rate_bps NOTIFY maxReceivedRateBpsChanged)
    Q_PROPERTY(double maxSentRateBps MEMBER m_max_sent_rate_bps NOTIFY maxSentRateBpsChanged)
    Q_PROPERTY(QVariantList receivedRateList MEMBER m_received_rate_list NOTIFY receivedRateListChanged)
    Q_PROPERTY(QVariantList sentRateList MEMBER m_sent_rate_list NOTIFY sentRateListChanged)
    Q_PROPERTY(int lastFilterWindowSize MEMBER m_last_filter_window_size NOTIFY lastFilterWindowSizeChanged)

public:
    bool active() const { return m_active; }
    void setActive(bool active)
    {
        if (m_active == active) return;
        m_active = active;
        Q_EMIT activeChanged();
    }

    bool m_active{false};
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
    void activeChanged();
    void totalBytesReceivedChanged();
    void totalBytesSentChanged();
    void maxReceivedRateBpsChanged();
    void maxSentRateBpsChanged();
    void receivedRateListChanged();
    void sentRateListChanged();
    void lastFilterWindowSizeChanged();
};

class MockNetworkStatusModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool reachabilityAvailable MEMBER m_reachability_available NOTIFY reachabilityChanged)
    Q_PROPERTY(bool networkOffline MEMBER m_network_offline NOTIFY reachabilityChanged)
    Q_PROPERTY(QString reachability MEMBER m_reachability NOTIFY reachabilityChanged)

public:
    bool m_reachability_available{true};
    bool m_network_offline{false};
    QString m_reachability{QStringLiteral("Online")};

    Q_INVOKABLE void setNetworkOfflineForTest(bool offline)
    {
        if (m_network_offline == offline) return;
        m_network_offline = offline;
        m_reachability = offline ? QStringLiteral("Disconnected") : QStringLiteral("Online");
        Q_EMIT reachabilityChanged();
    }

Q_SIGNALS:
    void reachabilityChanged();
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

class MockBanListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool unbanResult MEMBER m_unban_result NOTIFY actionStateChanged)
    Q_PROPERTY(int unbanCalls READ unbanCalls NOTIFY actionCallsChanged)
    Q_PROPERTY(int refreshCalls READ refreshCalls NOTIFY refreshCallsChanged)

public:
    enum Roles {
        AddressRole = Qt::UserRole,
        BanUntilRole
    };

    int count() const { return 1; }
    int unbanCalls() const { return m_unban_calls; }
    int refreshCalls() const { return m_refresh_calls; }

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override
    {
        Q_UNUSED(parent);
        return count();
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
        switch (role) {
        case AddressRole:
            return QStringLiteral("127.0.0.1/32");
        case BanUntilRole:
            return QStringLiteral("January 1, 2030 12:00 AM");
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {AddressRole, "address"},
            {BanUntilRole, "banUntil"},
        };
    }

    Q_INVOKABLE bool unbanAt(int row)
    {
        ++m_unban_calls;
        Q_EMIT actionCallsChanged();
        return row >= 0 && row < count() && m_unban_result;
    }

    Q_INVOKABLE void refresh()
    {
        ++m_refresh_calls;
        Q_EMIT refreshCallsChanged();
    }

    Q_INVOKABLE void resetTestState()
    {
        m_unban_result = true;
        m_unban_calls = 0;
        m_refresh_calls = 0;
        Q_EMIT actionStateChanged();
        Q_EMIT actionCallsChanged();
        Q_EMIT refreshCallsChanged();
    }

Q_SIGNALS:
    void countChanged();
    void actionStateChanged();
    void actionCallsChanged();
    void refreshCallsChanged();

private:
    bool m_unban_result{true};
    int m_unban_calls{0};
    int m_refresh_calls{0};
};

class MockWalletListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int listWalletDirCalls READ listWalletDirCalls NOTIFY listWalletDirCallsChanged)
    Q_PROPERTY(bool walletDirLoaded READ walletDirLoaded WRITE setWalletDirLoaded NOTIFY walletDirLoadedChanged)

public:
    enum class LoadState {
        Closed = 0,
        Open = 1,
        Loading = 2,
        LoadError = 3,
    };
    Q_ENUM(LoadState)

    enum Roles {
        NameRole = Qt::UserRole + 1,
        FormatRole,
        DisplayNameRole,
        LoadStateRole,
        ErrorMessageRole,
        BalanceRole,
        KeySchemeKindRole,
    };

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override
    {
        Q_UNUSED(parent);
        return m_wallet_names.size();
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_wallet_names.size()) return {};
        if (role == Qt::DisplayRole || role == DisplayNameRole) return m_wallet_names.at(index.row());
        if (role == NameRole) return m_wallet_names.at(index.row());
        if (role == FormatRole) return QStringLiteral("sqlite");
        if (role == LoadStateRole) return m_wallet_load_states.at(index.row());
        if (role == ErrorMessageRole) return QString{};
        if (role == BalanceRole) return QString{};
        if (role == KeySchemeKindRole) return 0;
        return {};
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {NameRole, "name"},
            {FormatRole, "format"},
            {DisplayNameRole, "displayName"},
            {LoadStateRole, "loadState"},
            {ErrorMessageRole, "errorMessage"},
            {BalanceRole, "balance"},
            {KeySchemeKindRole, "keySchemeKind"},
        };
    }

    int listWalletDirCalls() const { return m_list_wallet_dir_calls; }
    bool walletDirLoaded() const { return m_wallet_dir_loaded; }

    Q_INVOKABLE void listWalletDir()
    {
        ++m_list_wallet_dir_calls;
        Q_EMIT listWalletDirCallsChanged();
        setWalletDirLoaded(true);
    }
    Q_INVOKABLE void reset()
    {
        m_list_wallet_dir_calls = 0;
        m_wallet_dir_loaded = false;
        Q_EMIT listWalletDirCallsChanged();
        Q_EMIT walletDirLoadedChanged();
        setWalletLoadState(QStringLiteral("testwallet"), 1);
        setWalletLoadState(QStringLiteral("secondarywallet"), 0);
    }
    Q_INVOKABLE void setWalletDirLoaded(bool loaded)
    {
        if (m_wallet_dir_loaded == loaded) return;
        m_wallet_dir_loaded = loaded;
        Q_EMIT walletDirLoadedChanged();
    }
    Q_INVOKABLE void setWalletLoadState(const QString& name, int state)
    {
        const int row = m_wallet_names.indexOf(name);
        if (row < 0 || m_wallet_load_states.at(row) == state) return;
        m_wallet_load_states[row] = state;
        const QModelIndex changed_index = index(row, 0);
        Q_EMIT dataChanged(changed_index, changed_index, {LoadStateRole});
    }

Q_SIGNALS:
    void listWalletDirCallsChanged();
    void walletDirLoadedChanged();

private:
    int m_list_wallet_dir_calls{0};
    bool m_wallet_dir_loaded{false};
    QStringList m_wallet_names{QStringLiteral("testwallet"), QStringLiteral("secondarywallet")};
    QVector<int> m_wallet_load_states{1, 0};
};

class MockBumpTransactionModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int state READ state WRITE setState NOTIFY stateChanged)
    Q_PROPERTY(bool requireUnlock READ requireUnlock WRITE setRequireUnlock NOTIFY resultChanged)
    Q_PROPERTY(QString oldFee MEMBER m_old_fee NOTIFY resultChanged)
    Q_PROPERTY(QString newFee MEMBER m_new_fee NOTIFY resultChanged)
    Q_PROPERTY(QString feeIncrease MEMBER m_fee_increase NOTIFY resultChanged)
    Q_PROPERTY(QString oldTxid MEMBER m_old_txid NOTIFY resultChanged)
    Q_PROPERTY(QString newTxid MEMBER m_new_txid NOTIFY resultChanged)
    Q_PROPERTY(QString errorText MEMBER m_error_text NOTIFY resultChanged)
    Q_PROPERTY(bool needsUnlock MEMBER m_needs_unlock NOTIFY needsUnlockChanged)

public:
    enum State { Idle, Preparing, NeedsConfirmation, Committing, Succeeded, Failed };
    Q_ENUM(State)

    enum ActionType { SpeedUp };
    Q_ENUM(ActionType)

    int state() const { return m_state; }
    bool requireUnlock() const { return m_require_unlock; }

    void setState(int state)
    {
        if (m_state == state) return;
        m_state = state;
        Q_EMIT stateChanged();
    }

    void setRequireUnlock(bool require_unlock)
    {
        if (m_require_unlock == require_unlock) return;
        m_require_unlock = require_unlock;
        Q_EMIT resultChanged();
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

    Q_INVOKABLE bool confirmFeeBump()
    {
        if (m_require_unlock) {
            m_error_text = QStringLiteral("Enter your wallet password to update this transaction.");
            m_needs_unlock = true;
            Q_EMIT resultChanged();
            Q_EMIT needsUnlockChanged();
            return false;
        }

        m_new_txid = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
        m_needs_unlock = false;
        setState(Succeeded);
        Q_EMIT resultChanged();
        Q_EMIT needsUnlockChanged();
        return true;
    }

    Q_INVOKABLE bool confirmFeeBumpWithPassphrase(const QString& passphrase)
    {
        Q_UNUSED(passphrase);
        m_require_unlock = false;
        return confirmFeeBump();
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
        m_needs_unlock = false;
        m_require_unlock = false;
        Q_EMIT stateChanged();
        Q_EMIT resultChanged();
        Q_EMIT needsUnlockChanged();
    }

Q_SIGNALS:
    void stateChanged();
    void actionTypeChanged();
    void resultChanged();
    void needsUnlockChanged();

private:
    int m_state{Idle};
    QString m_old_fee;
    QString m_new_fee;
    QString m_fee_increase;
    QString m_old_txid;
    QString m_new_txid;
    QString m_error_text;
    bool m_needs_unlock{false};
    bool m_require_unlock{false};
};

class MockActivityListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

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
        ReplacedByTxidRole,
        IsPendingRequestRole,
        RequestIdRole,
        TimestampRole,
        NetAmountSatRole
    };

    int rowCount(const QModelIndex& parent = QModelIndex{}) const override
    {
        Q_UNUSED(parent);
        return m_count;
    }

    int count() const { return m_count; }

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
            case IsPendingRequestRole: return false;
            case RequestIdRole: return QString{};
            case TimestampRole: return 1767225600;
            case NetAmountSatRole: return 1000000;
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
        case IsPendingRequestRole: return false;
        case RequestIdRole: return QString{};
        case TimestampRole: return 1767312000;
        case NetAmountSatRole: return -100000;
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
            {IsPendingRequestRole, "isPendingRequest"},
            {RequestIdRole, "requestId"},
            {TimestampRole, "timestamp"},
            {NetAmountSatRole, "netAmountSat"},
        };
    }

    Q_INVOKABLE void reload() {}
    Q_INVOKABLE void setCountForTest(int count)
    {
        if (m_count == count) return;
        beginResetModel();
        m_count = count;
        endResetModel();
        Q_EMIT countChanged();
    }

Q_SIGNALS:
    void countChanged();

private:
    int m_count{2};
};

class MockActivityFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(DateFilter dateFilter READ dateFilter WRITE setDateFilter NOTIFY dateFilterChanged)
    Q_PROPERTY(TypeFilter typeFilter READ typeFilter WRITE setTypeFilter NOTIFY typeFilterChanged)
    Q_PROPERTY(int displayUnit READ displayUnit WRITE setDisplayUnit NOTIFY displayUnitChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum DateFilter {
        DateAll,
        Today,
        ThisWeek,
        ThisMonth,
        ThisYear
    };
    Q_ENUM(DateFilter)

    enum TypeFilter {
        TypeAll,
        Received,
        Sent,
        SentToSelf,
        Mined,
        PaymentRequest
    };
    Q_ENUM(TypeFilter)

    explicit MockActivityFilterProxyModel(QObject* parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
        connect(this, &QAbstractItemModel::rowsInserted, this, &MockActivityFilterProxyModel::countChanged);
        connect(this, &QAbstractItemModel::rowsRemoved, this, &MockActivityFilterProxyModel::countChanged);
        connect(this, &QAbstractItemModel::modelReset, this, &MockActivityFilterProxyModel::countChanged);
        connect(this, &QAbstractItemModel::layoutChanged, this, &MockActivityFilterProxyModel::countChanged);
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return sourceModel() ? sourceModel()->roleNames() : QHash<int, QByteArray>{};
    }

    void setSourceModel(QAbstractItemModel* source_model) override
    {
        if (sourceModel() == source_model) return;
        QSortFilterProxyModel::setSourceModel(source_model);
        Q_EMIT countChanged();
    }

    QString searchText() const { return m_search_text; }
    void setSearchText(const QString& search_text)
    {
        if (m_search_text == search_text) return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
#endif
        m_search_text = search_text;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
        Q_EMIT searchTextChanged();
        Q_EMIT countChanged();
    }

    DateFilter dateFilter() const { return m_date_filter; }
    void setDateFilter(DateFilter date_filter)
    {
        if (m_date_filter == date_filter) return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
#endif
        m_date_filter = date_filter;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
        Q_EMIT dateFilterChanged();
        Q_EMIT countChanged();
    }

    TypeFilter typeFilter() const { return m_type_filter; }
    void setTypeFilter(TypeFilter type_filter)
    {
        if (m_type_filter == type_filter) return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
#endif
        m_type_filter = type_filter;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
        Q_EMIT typeFilterChanged();
        Q_EMIT countChanged();
    }

    int displayUnit() const { return m_display_unit; }
    void setDisplayUnit(int display_unit)
    {
        if (m_display_unit == display_unit) return;
        m_display_unit = display_unit;
        Q_EMIT displayUnitChanged();
    }

    int count() const { return rowCount(); }

    Q_INVOKABLE bool exportCsv(const QString& path) const
    {
        Q_UNUSED(path);
        return true;
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override
    {
        Q_UNUSED(source_row);
        Q_UNUSED(source_parent);
        return m_search_text.trimmed().isEmpty() &&
            m_date_filter == DateAll &&
            m_type_filter == TypeAll;
    }

Q_SIGNALS:
    void searchTextChanged();
    void dateFilterChanged();
    void typeFilterChanged();
    void displayUnitChanged();
    void countChanged();

private:
    QString m_search_text;
    DateFilter m_date_filter{DateAll};
    TypeFilter m_type_filter{TypeAll};
    int m_display_unit{0};
};

class MockDesktopWindowBehaviorModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool desktopPlatform READ desktopPlatform CONSTANT)
    Q_PROPERTY(bool showTrayIcon READ showTrayIcon WRITE setShowTrayIcon NOTIFY showTrayIconChanged)
    Q_PROPERTY(bool minimizeToTray READ minimizeToTray WRITE setMinimizeToTray NOTIFY minimizeToTrayChanged)
    Q_PROPERTY(bool minimizeOnClose READ minimizeOnClose WRITE setMinimizeOnClose NOTIFY minimizeOnCloseChanged)

public:
    bool desktopPlatform() const { return m_desktop_platform; }

    bool showTrayIcon() const { return m_show_tray_icon; }
    void setShowTrayIcon(bool show) {
        if (m_show_tray_icon == show) return;
        m_show_tray_icon = show;
        if (!show && m_minimize_to_tray) {
            setMinimizeToTray(false);
        }
        Q_EMIT showTrayIconChanged(show);
    }

    bool minimizeToTray() const { return m_minimize_to_tray; }
    void setMinimizeToTray(bool minimize) {
        if (!m_show_tray_icon && minimize) return;
        if (m_minimize_to_tray == minimize) return;
        m_minimize_to_tray = minimize;
        Q_EMIT minimizeToTrayChanged(minimize);
    }

    bool minimizeOnClose() const { return m_minimize_on_close; }
    void setMinimizeOnClose(bool minimize) {
        if (m_minimize_on_close == minimize) return;
        m_minimize_on_close = minimize;
        Q_EMIT minimizeOnCloseChanged(minimize);
    }

    Q_INVOKABLE bool shouldHideToTrayOnMinimize() const {
        return m_desktop_platform && m_show_tray_icon && m_minimize_to_tray;
    }
    Q_INVOKABLE bool shouldMinimizeWindowOnClose() const {
        return m_desktop_platform && m_minimize_on_close;
    }

Q_SIGNALS:
    void showTrayIconChanged(bool show);
    void minimizeToTrayChanged(bool minimize);
    void minimizeOnCloseChanged(bool minimize);

private:
    bool m_desktop_platform{true};
    bool m_show_tray_icon{true};
    bool m_minimize_to_tray{false};
    bool m_minimize_on_close{false};
};

class MockDebugLogModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool hasMoreLines READ hasMoreLines NOTIFY hasMoreLinesChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(QString openError READ openError NOTIFY openErrorChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int loadMoreCalls READ loadMoreCalls NOTIFY loadMoreCallsChanged)

public:
    enum Role {
        LineNumberRole = Qt::UserRole + 1,
        ContentRole,
        RelativeTimeRole,
        CommandRole,
        MessageRole,
        DateLabelRole,
        SeverityRole,
    };
    Q_ENUM(Role)

    enum Severity {
        InfoSeverity = 0,
        WarningSeverity,
        ErrorSeverity,
    };
    Q_ENUM(Severity)

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rows.size();
    }

    int count() const { return m_rows.size(); }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
        const Row& row = m_rows.at(index.row());
        switch (role) {
        case LineNumberRole: return QString::number(index.row() + 1);
        case ContentRole: return row.message;
        case RelativeTimeRole: return row.date_label;
        case CommandRole: return row.command;
        case MessageRole: return row.message;
        case DateLabelRole: return row.date_label;
        case SeverityRole: return row.severity;
        default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {LineNumberRole, "lineNumber"},
            {ContentRole, "content"},
            {RelativeTimeRole, "relativeTime"},
            {CommandRole, "command"},
            {MessageRole, "message"},
            {DateLabelRole, "dateLabel"},
            {SeverityRole, "severity"},
        };
    }

    bool active() const { return m_active; }
    void setActive(bool active)
    {
        if (m_active == active) return;
        m_active = active;
        Q_EMIT activeChanged();
    }

    bool hasMoreLines() const { return m_has_more_lines; }
    QString filter() const { return m_filter; }
    void setFilter(const QString& filter)
    {
        if (m_filter == filter) return;
        m_filter = filter;
        Q_EMIT filterChanged();
    }
    QString openError() const { return {}; }
    int loadMoreCalls() const { return m_load_more_calls; }

    Q_INVOKABLE void refresh(bool = false) {}
    Q_INVOKABLE void loadMore()
    {
        ++m_load_more_calls;
        Q_EMIT loadMoreCallsChanged();
        appendRowsForTest(20);
        setHasMoreLinesForTest(false);
    }
    Q_INVOKABLE bool openLogFile() { return true; }
    Q_INVOKABLE void updateRelativeTimes() {}

    Q_INVOKABLE void resetForTest(int count, bool has_more_lines)
    {
        beginResetModel();
        m_rows.clear();
        m_rows.reserve(count);
        for (int i = 0; i < count; ++i) {
            m_rows.append(makeRow(QStringLiteral("entry-%1").arg(i)));
        }
        endResetModel();
        m_next_new_row = 0;
        m_next_old_row = count;
        m_load_more_calls = 0;
        Q_EMIT countChanged();
        Q_EMIT loadMoreCallsChanged();
        setHasMoreLinesForTest(has_more_lines);
    }

    Q_INVOKABLE void prependRowsForTest(int count)
    {
        if (count <= 0) return;

        QList<Row> added;
        added.reserve(count);
        for (int i = 0; i < count; ++i) {
            added.append(makeRow(QStringLiteral("new-%1").arg(m_next_new_row++)));
        }

        beginInsertRows(QModelIndex(), 0, count - 1);
        for (int i = count - 1; i >= 0; --i) {
            m_rows.prepend(std::move(added[i]));
        }
        endInsertRows();
        Q_EMIT countChanged();
        Q_EMIT newLinesAdded(count);
    }

    Q_INVOKABLE void appendRowsForTest(int count)
    {
        if (count <= 0) return;

        const int first = m_rows.size();
        beginInsertRows(QModelIndex(), first, first + count - 1);
        for (int i = 0; i < count; ++i) {
            m_rows.append(makeRow(QStringLiteral("old-%1").arg(m_next_old_row++)));
        }
        endInsertRows();
        Q_EMIT countChanged();
    }

    Q_INVOKABLE void removeRowsFromEndForTest(int count)
    {
        count = std::min<int>(count, m_rows.size());
        if (count <= 0) return;

        const int first = m_rows.size() - count;
        beginRemoveRows(QModelIndex(), first, m_rows.size() - 1);
        m_rows.remove(first, count);
        endRemoveRows();
        Q_EMIT countChanged();
    }

    Q_INVOKABLE void prependAndPruneRowsForTest(int prepend_count, int prune_count)
    {
        prependRowsForTest(prepend_count);
        removeRowsFromEndForTest(prune_count);
    }

    Q_INVOKABLE void prependAndAppendRowsForTest(int prepend_count,
                                                 int append_count,
                                                 bool prepend_first)
    {
        if (prepend_first) {
            prependRowsForTest(prepend_count);
            appendRowsForTest(append_count);
        } else {
            appendRowsForTest(append_count);
            prependRowsForTest(prepend_count);
        }
    }

    Q_INVOKABLE void setMessageForTest(int row, const QString& message)
    {
        if (row < 0 || row >= m_rows.size() || m_rows.at(row).message == message) return;
        m_rows[row].message = message;
        const QModelIndex changed_index = index(row, 0);
        Q_EMIT dataChanged(changed_index, changed_index, {ContentRole, MessageRole});
    }

    Q_INVOKABLE QString messageAt(int row) const
    {
        if (row < 0 || row >= m_rows.size()) return {};
        return m_rows.at(row).message;
    }

    Q_INVOKABLE void setHasMoreLinesForTest(bool has_more_lines)
    {
        if (m_has_more_lines == has_more_lines) return;
        m_has_more_lines = has_more_lines;
        Q_EMIT hasMoreLinesChanged();
    }

Q_SIGNALS:
    void activeChanged();
    void hasMoreLinesChanged();
    void filterChanged();
    void openErrorChanged();
    void newLinesAdded(int count);
    void countChanged();
    void loadMoreCallsChanged();

private:
    struct Row {
        QString command;
        QString message;
        QString date_label;
        int severity{InfoSeverity};
    };

    static Row makeRow(const QString& message)
    {
        return Row{
            QStringLiteral("test"),
            message,
            QStringLiteral("just now"),
            InfoSeverity,
        };
    }

    bool m_active{false};
    bool m_has_more_lines{false};
    QString m_filter;
    QList<Row> m_rows;
    int m_load_more_calls{0};
    int m_next_new_row{0};
    int m_next_old_row{0};
};

class MockClipboard : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE void setText(const QString& text) { m_text = text; }
    Q_INVOKABLE QString text() const { return m_text; }

private:
    QString m_text;
};

//! Stands in for the real UrlOpener so the popup's success and failure paths
//! can be driven without launching a browser. Mirrors the production scheme
//! allowlist so a test cannot pass on a URL the real opener would reject.
class MockUrlOpener : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE bool openUrl(const QString& url)
    {
        m_last_url = url;
        const QUrl parsed(url, QUrl::StrictMode);
        if (!parsed.isValid() || parsed.host().isEmpty()) return false;
        const QString scheme = parsed.scheme().toLower();
        if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) return false;
        return m_open_result;
    }
    Q_INVOKABLE void setOpenResult(bool ok) { m_open_result = ok; }
    Q_INVOKABLE QString lastUrl() const { return m_last_url; }
    Q_INVOKABLE void reset()
    {
        m_open_result = true;
        m_last_url.clear();
    }

private:
    bool m_open_result{true};
    QString m_last_url;
};

class QmlTestsSetup : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void qmlEngineAvailable(QQmlEngine* engine)
    {
        engine->addImportPath(QStringLiteral(BITCOINQML_QML_TEST_MOCKS_DIR));
        static MockAppMode app_mode;
        static MockBuildInfo build_info;
        static MockOptionsModel options_model;
        static MockChainModel chain_model;
        static MockNodeModel node_model;
        static MockPeerTableModel peer_table_model;
        static MockNetworkTrafficTower network_traffic_tower;
        static MockNetworkStatusModel network_status_model;
        static MockPeerListModelProxy peer_list_model_proxy;
        static MockBanListModel ban_list_model;
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
        static MockDesktopWindowBehaviorModel desktop_window_behavior_model;
        static MockDebugLogModel debug_log_model;
        static MockClipboard clipboard;
        static MockUrlOpener url_opener;
        recipients_model.setCurrent(&send_recipient);
        wallet_model.setActivityListModel(&activity_list_model);
        wallet_model.setBumpModel(&bump_model);
        wallet_model.setRecipients(&recipients_model);
        wallet_model.setCoinsListModel(&coins_list_model);
        wallet_model.setCurrentTransaction(&wallet_transaction);
        wallet_model.setCurrentPaymentRequest(&payment_request);
        wallet_controller.setSelectedWalletObject(&wallet_model);
        qmlRegisterSingletonInstance<MockAppMode>("org.bitcoincore.qt", 1, 0, "AppMode", &app_mode);
        qmlRegisterSingletonInstance<MockBuildInfo>("org.bitcoincore.qt", 1, 0, "BuildInfo", &build_info);
        qmlRegisterSingletonInstance<MockClipboard>("org.bitcoincore.qt", 1, 0, "Clipboard", &clipboard);
        qmlRegisterSingletonInstance<MockUrlOpener>("org.bitcoincore.qt", 1, 0, "UrlOpener", &url_opener);
        qmlRegisterUncreatableType<MockPeerDetailsModel>(
            "org.bitcoincore.qt",
            1,
            0,
            "PeerDetailsModel",
            "Test stub type"
        );
        qmlRegisterType<MockBitcoinAmount>("org.bitcoincore.qt", 1, 0, "BitcoinAmount");
        qmlRegisterType<MockBitcoinAddress>("org.bitcoincore.qt", 1, 0, "BitcoinAddress");
        qmlRegisterType<MockActivityFilterProxyModel>("org.bitcoincore.qt", 1, 0, "ActivityFilterProxyModel");
        qmlRegisterUncreatableType<MockAddressListModel>("org.bitcoincore.qt", 1, 0, "AddressListModel", "Test stub type");
        qmlRegisterType<MockPaymentRequest>("org.bitcoincore.qt", 1, 0, "PaymentRequest");
        qmlRegisterUncreatableType<MockTransaction>("org.bitcoincore.qt", 1, 0, "Transaction", "Test stub type");
        qmlRegisterUncreatableType<MockSendRecipient>("org.bitcoincore.qt", 1, 0, "SendRecipient", "Test stub type");
        qmlRegisterUncreatableType<MockBumpTransactionModel>("org.bitcoincore.qt", 1, 0, "BumpTransactionModel", "Test stub type");
        qmlRegisterUncreatableType<MockWalletQmlModel>("org.bitcoincore.qt", 1, 0, "WalletQmlModel", "Test stub type");
        qmlRegisterUncreatableType<MockWalletListModel>("org.bitcoincore.qt", 1, 0, "WalletListModel", "Test stub type");
        qmlRegisterUncreatableType<MockWalletQmlModelTransaction>(
            "org.bitcoincore.qt",
            1,
            0,
            "WalletQmlModelTransaction",
            "Test stub type"
        );
        qmlRegisterUncreatableType<MockDebugLogModel>(
            "org.bitcoincore.qt",
            1,
            0,
            "DebugLogModel",
            "Test stub type"
        );
        qmlRegisterType<BlockClockDial>("org.bitcoincore.qt", 1, 0, "BlockClockDial");
        qmlRegisterType<LineGraph>("org.bitcoincore.qt", 1, 0, "LineGraph");
        engine->rootContext()->setContextProperty(QStringLiteral("optionsModel"), &options_model);
        engine->rootContext()->setContextProperty(QStringLiteral("chainModel"), &chain_model);
        engine->rootContext()->setContextProperty(QStringLiteral("nodeModel"), &node_model);
        engine->rootContext()->setContextProperty(QStringLiteral("peerTableModel"), &peer_table_model);
        engine->rootContext()->setContextProperty(QStringLiteral("networkTrafficTower"), &network_traffic_tower);
        engine->rootContext()->setContextProperty(QStringLiteral("testNetworkTrafficTower"), &network_traffic_tower);
        engine->rootContext()->setContextProperty(QStringLiteral("networkStatusModel"), &network_status_model);
        engine->rootContext()->setContextProperty(QStringLiteral("peerListModelProxy"), &peer_list_model_proxy);
        engine->rootContext()->setContextProperty(QStringLiteral("banListModel"), &ban_list_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testPeerDetailsModel"), &peer_details_model);
        engine->rootContext()->setContextProperty(QStringLiteral("walletController"), &wallet_controller);
        engine->rootContext()->setContextProperty(QStringLiteral("walletListModel"), &wallet_list_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testWalletModel"), &wallet_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testWalletTransaction"), &wallet_transaction);
        engine->rootContext()->setContextProperty(QStringLiteral("testPaymentRequest"), &payment_request);
        engine->rootContext()->setContextProperty(QStringLiteral("testActivityListModel"), &activity_list_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testSendRecipient"), &send_recipient);
        engine->rootContext()->setContextProperty(QStringLiteral("testAutomationEnabled"), false);
        engine->rootContext()->setContextProperty(QStringLiteral("testRecipientsModel"), &recipients_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testCoinsListModel"), &coins_list_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testBumpModel"), &bump_model);
        engine->rootContext()->setContextProperty(QStringLiteral("desktopWindowBehaviorModel"), &desktop_window_behavior_model);
        engine->rootContext()->setContextProperty(QStringLiteral("debugLogModel"), &debug_log_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testUrlOpener"), &url_opener);
        engine->rootContext()->setContextProperty(QStringLiteral("testDebugLogModel"), &debug_log_model);
        engine->addImportPath(QStringLiteral(BITCOINQML_QML_SOURCE_DIR));
    }
};

QUICK_TEST_MAIN_WITH_SETUP(bitcoinqml_qmltests, QmlTestsSetup)

#include "qml_tests_main.moc"
