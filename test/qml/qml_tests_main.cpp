// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/texteditsessionmodel.h>
#include <qml/models/activityexportoperation.h>
#include <qml/receivetyperegistration.h>
#include <qml/components/ownedqrimage.h>
#include <qml/models/exacttextdocument.h>
#include <qml/models/messagesigningprompt.h>
#include <qml/guipresentation.h>
#include <QUuid>
#include <QPointer>
#include <QtQuickTest/quicktest.h>
#include <test/qmlcoverage.h>
#include <qml/models/bumptransactionmodel.h>
#include <qml/models/monospaceoutputmodel.h>
#include <qml/models/bitcoinaddress.h>
#include <qml/flowpresentation.h>
#include <qml/models/walletoperationpresentationmodel.h>
#include <qml/models/uipreferences.h>
#include <qml/models/signerdiscoverymodel.h>
#include <qml/models/settingseditormodel.h>
#include <qml/settingsparse.h>
#include <qml/onboarding_storage.h>
#include <QCryptographicHash>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/receiveqmlfixture.h>
#include <qml/models/sendimportmodel.h>
#include <QDir>

#include <QAbstractListModel>
#include <QAccessible>
#include <QColor>
#include <QCoreApplication>
#include <QClipboard>
#include <QDateTime>
#include <QEvent>
#include <QEventLoop>
#include <QFont>
#include <QGuiApplication>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QLocale>
#include <QPixmap>
#include <QPointF>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlError>
#include <QQmlIncubationController>
#include <QQueue>
#include <QInputMethodEvent>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QResource>
#include <QSettings>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <qqml.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

#include <qml/backendexecutor.h>
#include <qml/models/feeselectionmodel.h>
#include <qml/bitcoinamount.h>
#include <qml/buttonkeyactivation.h>
#include <qml/components/blockclockdial.h>
#include <qml/presentationservices.h>
#include <qml/controls/linegraph.h>
#include <qml/qrimageprovider.h>
#include <qml/globalsettings.h>
#include <qml/motionpreference.h>

class MockAppMode : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isDesktop READ isDesktop WRITE setIsDesktop NOTIFY isDesktopChanged)
    Q_PROPERTY(bool walletEnabled READ walletEnabled WRITE setWalletEnabled NOTIFY walletEnabledChanged)
    Q_PROPERTY(QString state READ state WRITE setState NOTIFY stateChanged)
    Q_PROPERTY(bool animationsEnabled READ animationsEnabled CONSTANT)

public:
    bool isDesktop() const { return m_is_desktop; }
    bool animationsEnabled() const { return !qEnvironmentVariableIsSet("BITCOIN_QML_TEST_NO_ANIMATIONS"); }
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
    Q_PROPERTY(bool externalSignerSupport READ externalSignerSupport CONSTANT)
    Q_PROPERTY(bool isDebug READ isDebug CONSTANT)
    Q_PROPERTY(QString fullClientVersion READ fullClientVersion CONSTANT)

public:
    bool externalSignerSupport() const { return true; }
    bool isDebug() const { return false; }
    QString fullClientVersion() const { return QStringLiteral("v0.0.0-test"); }
};

class MockNodeModel;
class MockPeerDetailsModel;
class MockPeerCommandResult;
struct MockPeerOperationState;
class MockPeerCommandOperation : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString operationId READ operationId CONSTANT)
    Q_PROPERTY(QString connectionId READ connectionId CONSTANT)
    Q_PROPERTY(QString nodeIdText READ nodeIdText CONSTANT)
    Q_PROPERTY(QString address READ address CONSTANT)
    Q_PROPERTY(int banDuration READ banDuration CONSTANT)
    Q_PROPERTY(bool confirmationRequired READ confirmationRequired NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool queryPending READ queryPending NOTIFY changed)
    Q_PROPERTY(bool resultPresent READ resultPresent NOTIFY changed)
    Q_PROPERTY(bool outcomeKnown READ outcomeKnown NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QString queryError READ queryError NOTIFY changed)
    Q_PROPERTY(MockPeerCommandResult result READ result NOTIFY changed)
public:
    enum Outcome { Succeeded, Rejected, PartiallyApplied, Uncertain, Cancelled };
    Q_ENUM(Outcome)
    enum BanDuration { Hour = 3600, Day = 86400, Week = 604800, Year = 31536000 };
    Q_ENUM(BanDuration)
    MockPeerCommandOperation(MockNodeModel* node, std::shared_ptr<MockPeerOperationState> original);
    QString operationId() const;
    QString connectionId() const;
    QString nodeIdText() const;
    QString address() const;
    int banDuration() const;
    bool confirmationRequired() const;
    bool pending() const;
    bool queryPending() const;
    bool resultPresent() const;
    bool outcomeKnown() const;
    QString errorText() const;
    QString queryError() const;
    MockPeerCommandResult result() const;
    Q_INVOKABLE bool confirm();
    Q_INVOKABLE bool cancel();
    Q_INVOKABLE bool reconcile();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void retire();
Q_SIGNALS:
    void changed();
private:
    bool cancelOriginal();
    QPointer<MockNodeModel> m_node;
    std::shared_ptr<MockPeerOperationState> m_original;
    bool m_retired{false};
};

class MockPeerCommandResult {
    Q_GADGET
    Q_PROPERTY(QString operationId MEMBER operationId)
    Q_PROPERTY(QString connectionId MEMBER connectionId)
    Q_PROPERTY(QString nodeIdText MEMBER nodeIdText)
    Q_PROPERTY(QString address MEMBER address)
    Q_PROPERTY(QString errorText MEMBER errorText)
    Q_PROPERTY(QString queryError MEMBER queryError)
    Q_PROPERTY(MockPeerCommandOperation::Outcome outcome MEMBER outcome)
    Q_PROPERTY(bool present MEMBER present)
    Q_PROPERTY(bool outcomeKnown MEMBER outcomeKnown)
    Q_PROPERTY(bool mutationAttempted MEMBER mutationAttempted)
    Q_PROPERTY(bool banApplied MEMBER banApplied)
    Q_PROPERTY(bool disconnectConfirmed MEMBER disconnectConfirmed)
    Q_PROPERTY(bool observationAvailable MEMBER observationAvailable)
    Q_PROPERTY(bool targetConnected MEMBER targetConnected)
    Q_PROPERTY(bool banObservationAvailable MEMBER banObservationAvailable)
    Q_PROPERTY(bool targetBanned MEMBER targetBanned)
public:
    QString operationId, connectionId, nodeIdText, address, errorText, queryError;
    MockPeerCommandOperation::Outcome outcome{MockPeerCommandOperation::Rejected};
    bool present{false}, outcomeKnown{false}, mutationAttempted{false}, banApplied{false}, disconnectConfirmed{false};
    bool observationAvailable{false}, targetConnected{false}, banObservationAvailable{false}, targetBanned{false};
};
Q_DECLARE_METATYPE(MockPeerCommandResult)
struct MockPeerOperationState {
    QString id, connection, node_id, raw_address, address;
    int duration{0};
    quint64 ticket{0};
    QPointer<MockPeerDetailsModel> target;
    MockPeerCommandResult result;
    bool confirmed{false}, pending{false}, query_pending{false};
    std::function<void()> notify;
};

class MockPeerDetailsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 nodeId MEMBER m_node_id NOTIFY dataChanged)
    Q_PROPERTY(QString nodeIdText READ nodeIdText CONSTANT)
    Q_PROPERTY(QString connectionId READ connectionId CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY dataChanged)
    Q_PROPERTY(bool canDisconnect READ canDisconnect NOTIFY dataChanged)
    Q_PROPERTY(bool canBan READ canBan NOTIFY dataChanged)
    Q_PROPERTY(QString rawAddress MEMBER m_raw_address NOTIFY dataChanged)
    Q_PROPERTY(QString address MEMBER m_address NOTIFY dataChanged)
    Q_PROPERTY(QString addressLocal MEMBER m_address_local NOTIFY dataChanged)
    Q_PROPERTY(QString type MEMBER m_type NOTIFY dataChanged)
    Q_PROPERTY(QString network MEMBER m_network NOTIFY dataChanged)
    Q_PROPERTY(QString transport MEMBER m_transport NOTIFY dataChanged)
    Q_PROPERTY(QString sessionId MEMBER m_session_id NOTIFY dataChanged)
    Q_PROPERTY(QString permission MEMBER m_permission NOTIFY dataChanged)
    Q_PROPERTY(QString version MEMBER m_version NOTIFY dataChanged)
    Q_PROPERTY(QString userAgent MEMBER m_user_agent NOTIFY dataChanged)
    Q_PROPERTY(QString services MEMBER m_services NOTIFY dataChanged)
    Q_PROPERTY(bool transactionRelay MEMBER m_transaction_relay NOTIFY dataChanged)
    Q_PROPERTY(bool addressRelay MEMBER m_address_relay NOTIFY dataChanged)
    Q_PROPERTY(bool highBandwidth MEMBER m_high_bandwidth NOTIFY dataChanged)
    Q_PROPERTY(QString addressesProcessed MEMBER m_addresses_processed NOTIFY dataChanged)
    Q_PROPERTY(QString addressesRateLimited MEMBER m_addresses_rate_limited NOTIFY dataChanged)
    Q_PROPERTY(QString mappedAS MEMBER m_mapped_as NOTIFY dataChanged)
    Q_PROPERTY(QString startingHeight MEMBER m_starting_height NOTIFY dataChanged)
    Q_PROPERTY(QString syncedHeaders MEMBER m_synced_headers NOTIFY dataChanged)
    Q_PROPERTY(QString syncedBlocks MEMBER m_synced_blocks NOTIFY dataChanged)
    Q_PROPERTY(QString direction MEMBER m_direction NOTIFY dataChanged)
    Q_PROPERTY(QString connectionDuration MEMBER m_connection_duration NOTIFY dataChanged)
    Q_PROPERTY(QString lastSend MEMBER m_last_send NOTIFY dataChanged)
    Q_PROPERTY(QString lastReceived MEMBER m_last_received NOTIFY dataChanged)
    Q_PROPERTY(QString bytesSent MEMBER m_bytes_sent NOTIFY dataChanged)
    Q_PROPERTY(QString bytesReceived MEMBER m_bytes_received NOTIFY dataChanged)
    Q_PROPERTY(QString pingTime MEMBER m_ping_time NOTIFY dataChanged)
    Q_PROPERTY(QString pingWait MEMBER m_ping_wait NOTIFY dataChanged)
    Q_PROPERTY(QString pingMin MEMBER m_ping_min NOTIFY dataChanged)
    Q_PROPERTY(QString timeOffset MEMBER m_time_offset NOTIFY dataChanged)

public:
    explicit MockPeerDetailsModel(QObject* parent = nullptr) : QObject(parent) {}
    QString nodeIdText() const { return QString::number(m_node_id); }
    QString connectionId() const { return m_connection_id; }
    bool connected() const { return m_connected; }
    bool canDisconnect() const;
    bool canBan() const { return canDisconnect() && !m_raw_address.isEmpty(); }
    void setCommands(MockNodeModel* node);
    Q_INVOKABLE MockPeerCommandOperation* prepareDisconnect();
    Q_INVOKABLE MockPeerCommandOperation* prepareBan(MockPeerCommandOperation::BanDuration duration);
    Q_INVOKABLE void resetConnectionForTest() { m_connected = true; Q_EMIT dataChanged(); }
    qint64 m_node_id{7};
    QString m_raw_address{QStringLiteral("127.0.0.1")};
    QString m_address{QStringLiteral("127.0.0.1:8333")};
    QString m_address_local{QStringLiteral("127.0.0.1:18444")};
    QString m_type{QStringLiteral("Outbound Full Relay")};
    QString m_network{QStringLiteral("IPv4")};
    QString m_transport{QStringLiteral("v2")};
    QString m_session_id{QStringLiteral("043604a60a54b3f5")};
    QString m_permission{};
    QString m_version{QStringLiteral("70016")};
    QString m_user_agent{QStringLiteral("/Satoshi:test/")};
    QString m_services{QStringLiteral("NETWORK|WITNESS")};
    bool m_transaction_relay{true};
    bool m_address_relay{false};
    bool m_high_bandwidth{true};
    QString m_addresses_processed{QStringLiteral("1076")};
    QString m_addresses_rate_limited{QStringLiteral("0")};
    QString m_mapped_as{};
    QString m_starting_height{};
    QString m_synced_headers{QStringLiteral("200")};
    QString m_synced_blocks{QStringLiteral("150")};
    QString m_direction{QStringLiteral("Inbound")};
    QString m_connection_duration{QStringLiteral("5 min")};
    QString m_last_send{QStringLiteral("2 s")};
    QString m_last_received{QStringLiteral("3 s")};
    QString m_bytes_sent{QStringLiteral("1.0 MiB")};
    QString m_bytes_received{QStringLiteral("2.0 MiB")};
    QString m_ping_time{QStringLiteral("10 ms")};
    QString m_ping_wait{};
    QString m_ping_min{QStringLiteral("7 ms")};
    QString m_time_offset{QStringLiteral("0 s")};

    Q_INVOKABLE void triggerDisconnected() { if (!m_connected) return; m_connected = false; Q_EMIT dataChanged(); Q_EMIT disconnected(); }
    MockNodeModel* m_commands{nullptr};
    const QString m_connection_id{QUuid::createUuid().toString(QUuid::WithoutBraces)};
    bool m_connected{true};

Q_SIGNALS:
    void dataChanged();
    void disconnected();
};

// Real validated values make the component fixtures exercise production parsing.
class MockBitcoinAmount : public BitcoinAmount
{
    Q_OBJECT
public:
    static constexpr Unit BTC{Unit::BTC};
    static constexpr Unit SAT{Unit::SAT};
    QString display() const { return toDisplay(); }
    void setDisplay(const QString& value) { fromDisplay(value); }
};

class MockBitcoinAddress : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address READ address NOTIFY addressChanged)
    Q_PROPERTY(QString formattedAddress READ formattedAddress NOTIFY formattedAddressChanged)
    Q_PROPERTY(QString ellipsesAddress READ ellipsesAddress NOTIFY ellipsesAddressChanged)
    Q_PROPERTY(int normalizedCursor MEMBER m_cursor NOTIFY formattedAddressChanged)

public:
    QString address() const { return m_address; }
    QString formattedAddress() const { return m_address; }
    QString ellipsesAddress() const { return m_address; }
    Q_INVOKABLE int pasteAddress(const QString& text, int start, int end) {
        start = std::clamp(start, 0, static_cast<int>(m_address.size()));
        end = std::clamp(end, start, static_cast<int>(m_address.size()));
        return setAddress(m_address.left(start) + text + m_address.mid(end), start + static_cast<int>(text.size()));
    }
    Q_INVOKABLE int setAddress(const QString& address, int cursorPosition = 0)
    {
        const auto is_address_char = [](const QChar c) {
            const ushort ch = c.unicode();
            return (ch >= '0' && ch <= '9') ||
                   (ch >= 'a' && ch <= 'z') ||
                   (ch >= 'A' && ch <= 'Z' && ch != 'I' && ch != 'O');
        };

        QString sanitized;
        const int address_size = static_cast<int>(address.size());
        sanitized.reserve(std::min(address_size, 90));
        const int input_cursor = std::clamp(cursorPosition, 0, address_size);
        int sanitized_cursor{0};
        for (int i = 0; i < address_size && sanitized.size() < 90; ++i) {
            if (!is_address_char(address.at(i))) continue;
            sanitized += address.at(i);
            if (i < input_cursor) ++sanitized_cursor;
        }

        m_cursor = sanitized_cursor;
        if (m_address != sanitized) {
            m_address = sanitized;
            Q_EMIT addressChanged();
            Q_EMIT formattedAddressChanged();
            Q_EMIT ellipsesAddressChanged();
        } else if (address != sanitized) {
            Q_EMIT formattedAddressChanged();
        }
        return sanitized_cursor;
    }

Q_SIGNALS:
    void addressChanged();
    void formattedAddressChanged();
    void ellipsesAddressChanged();

private:
    int m_cursor{0};
    QString m_address;
};

class MockReceiveRequestOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool pending MEMBER m_pending NOTIFY changed)
    Q_PROPERTY(bool requestSaved MEMBER m_saved NOTIFY changed)
    Q_PROPERTY(bool requestLoaded MEMBER m_loaded NOTIFY changed)
    Q_PROPERTY(bool erased MEMBER m_erased NOTIFY changed)
    Q_PROPERTY(bool labelSaved MEMBER m_label_saved NOTIFY changed)
    Q_PROPERTY(QString requestId MEMBER m_id NOTIFY changed)
    Q_PROPERTY(QString errorText MEMBER m_error NOTIFY changed)
    Q_PROPERTY(bool addressReady MEMBER m_address_ready NOTIFY changed)
    Q_PROPERTY(QString addressToken MEMBER m_token NOTIFY changed)
    Q_PROPERTY(quint64 viewRevision MEMBER m_view_revision NOTIFY changed)
    Q_PROPERTY(Error error MEMBER m_error_code NOTIFY changed)
public:
    enum Error { None, NeedsUnlock, IncorrectPassphrase, ReservationPersistence };
    Q_ENUM(Error)
    Error m_error_code{None};
    bool m_address_ready{false}, m_next{false};
    quint64 m_view_revision{0};
    QString m_token, m_type;
    explicit MockReceiveRequestOperation(QObject* parent = nullptr) : QObject(parent) {}
    bool m_pending{false}, m_saved{false}, m_loaded{false}, m_erased{false}, m_label_saved{false};
    QString m_id, m_error;
    Q_INVOKABLE void publishSelection(bool address_ready, bool request_loaded, const QString& id, const QString& error) {
        m_address_ready = address_ready; m_loaded = request_loaded; m_id = id; m_error = error; m_pending = false;
        Q_EMIT changed();
    }
    Q_INVOKABLE void complete(bool saved, bool erased, const QString& error) {
        m_pending = false; m_saved = saved; m_erased = erased; m_error = error; Q_EMIT changed();
    }
Q_SIGNALS:
    void changed();
};

class MockAddressListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(Category category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(QVariantList categoryOptions READ categoryOptions CONSTANT)
    Q_PROPERTY(bool showUsed READ showUsed WRITE setShowUsed NOTIFY showUsedChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool setAddressLabelSucceeds MEMBER m_set_address_label_succeeds)

public:
    enum Category {
        SingleUse,
        Change
    };
    Q_ENUM(Category)

    enum AddressRoles {
        AddressRole = Qt::UserRole + 1,
        FormattedAddressRole,
        EllipsesAddressRole,
        LabelRole,
        CategoryRole,
        UsedRole,
        CurrentBalanceRole,
        DisplayAmountRole,
        HasAmountRole,
        ScriptTypeRole,
        CanEditLabelRole,
        CanCreatePaymentRequestRole,
    };

    MockAddressListModel()
    {
        const auto bridge = presentation::MakeGuiObject();
        m_label_scheduler = std::make_shared<execution::TaskQueue>([bridge](execution::Lane, execution::Phase, execution::TaskId, std::function<void()> turn) {
            return QMetaObject::invokeMethod(bridge.get(), std::move(turn), Qt::QueuedConnection);
        }, execution::Options{}, 1);
        populateRows();
    }
    Q_INVOKABLE TextEditSessionModel* labelEditor(const QString& address) const { return m_editors.value(address); }
    Q_INVOKABLE TextEditSessionModel* editLabel(const QString& address) {
        if (auto* held = labelEditor(address); held && held->phase() != TextEditSessionModel::Closed) return held;
        if (!m_notes->contains(address)) return nullptr;
        TextEditSessionModel::Ports ports; ports.execution.scheduler = m_label_scheduler;
        ports.validate = [](const QString& value) { return value.size() <= 16384; };
        ports.read = [notes = m_notes, scheduler = m_label_scheduler, address](auto done) {
            return execution::Submit(*scheduler, [notes, address] { return TextEditSessionModel::Read{notes->value(address), {}}; }, done, {}, {execution::Lane::Core});
        };
        const QPointer<MockAddressListModel> observer{this};
        ports.write = [notes = m_notes, address, observer](const auto& attempt, auto done, const auto& action) {
            return execution::Submit(*action, [notes, address, attempt, observer] {
                TextEditSessionModel::Write value; value.value = attempt.values[0];
                if (!observer || !observer->m_set_address_label_succeeds) {
                    value.error = {TextEditError::Persistence, "The address note could not be saved."}; return value;
                }
                if (notes->value(address) != attempt.base[0]) {
                    value.error = {TextEditError::Changed, "The note changed."}; value.current = TextEditSessionModel::Read{notes->value(address), {}}; return value;
                }
                notes->insert(address, value.value); value.persisted = true; value.outcome = persistentedit::Outcome::Saved;
                return value;
            }, [observer, address, done](auto value) {
                if (observer && value.persisted) observer->projectNote(address, value.value);
                done(std::move(value));
            }, {}, {execution::Lane::Core});
        };
        auto* editor = new TextEditSessionModel(address, std::move(ports), this);
        m_editors.insert(address, editor); return editor;
    }
    void projectNote(const QString& address, const QString& label) {
        for (int row{0}; row < rowCount(); ++row) if (m_rows[row].address == address) {
            m_rows[row].label = label; Q_EMIT dataChanged(index(row), index(row), {LabelRole}); break;
        }
    }

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
    }
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
        const Entry& entry{m_rows.at(index.row())};
        switch (role) {
        case AddressRole:
        case FormattedAddressRole:
        case EllipsesAddressRole:
            return entry.address;
        case LabelRole:
            return entry.label;
        case CategoryRole:
            return QStringLiteral("single-use");
        case UsedRole:
        case HasAmountRole:
            return false;
        case CurrentBalanceRole:
            return QStringLiteral("0");
        case DisplayAmountRole:
            return QStringLiteral("₿ 0");
        case ScriptTypeRole:
            return QStringLiteral("P2WPKH");
        case CanEditLabelRole:
        case CanCreatePaymentRequestRole:
            return true;
        }
        return {};
    }
    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {AddressRole, "address"},
            {FormattedAddressRole, "formattedAddress"},
            {EllipsesAddressRole, "ellipsesAddress"},
            {LabelRole, "label"},
            {CategoryRole, "category"},
            {UsedRole, "isUsed"},
            {CurrentBalanceRole, "currentBalance"},
            {DisplayAmountRole, "displayAmount"},
            {HasAmountRole, "hasAmount"},
            {ScriptTypeRole, "scriptType"},
            {CanEditLabelRole, "canEditLabel"},
            {CanCreatePaymentRequestRole, "canCreatePaymentRequest"},
        };
    }

    Category category() const { return m_category; }
    QVariantList categoryOptions() const
    {
        return {
            QVariantMap{{QStringLiteral("value"), static_cast<int>(SingleUse)}, {QStringLiteral("text"), QStringLiteral("Receive")}},
            QVariantMap{{QStringLiteral("value"), static_cast<int>(Change)}, {QStringLiteral("text"), QStringLiteral("Change")}},
        };
    }
    bool showUsed() const { return m_show_used; }
    int count() const { return rowCount(); }

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

    Q_PROPERTY(int refreshCalls READ refreshCalls NOTIFY refreshCallsChanged)
    int refreshCalls() const { return m_refresh_calls; }
    Q_INVOKABLE void refresh() { ++m_refresh_calls; for (const auto& editor : m_editors) if (editor) editor->refresh(); Q_EMIT refreshCallsChanged(); }
    Q_INVOKABLE MockReceiveRequestOperation* setAddressLabel(const QString& address, const QString& label)
    {
        if (!m_set_address_label_succeeds) return nullptr;
        for (int row = 0; row < rowCount(); ++row) {
            Entry& entry{m_rows.at(row)};
            if (entry.address != address) continue;
            entry.label = label;
            m_notes->insert(address, label);
            Q_EMIT dataChanged(index(row), index(row), {LabelRole});
            break;
        }
        auto* operation = new MockReceiveRequestOperation(this); operation->m_label_saved = true;
        return operation;
    }
    Q_INVOKABLE QString addressAt(int row) const
    {
        return row >= 0 && row < rowCount() ? m_rows.at(row).address : QString{};
    }
    Q_INVOKABLE QString labelAt(int row) const
    {
        return row >= 0 && row < rowCount() ? m_rows.at(row).label : QString{};
    }
    Q_INVOKABLE void resetForTest()
    {
        for (const auto& editor : m_editors) if (editor) { editor->retire(); editor->deleteLater(); }
        m_editors.clear();
        beginResetModel();
        m_rows.clear();
        m_notes->clear();
        populateRows();
        endResetModel();
        Q_EMIT countChanged();
    }

Q_SIGNALS:
    void refreshCallsChanged();
    void categoryChanged();
    void showUsedChanged();
    void countChanged();

private:
    struct Entry {
        QString address;
        QString label;
    };

    void populateRows()
    {
        for (int row = 0; row < 20; ++row) {
            m_rows.push_back({
                QStringLiteral("bcrt1qtestaddress%1").arg(row, 2, 10, QLatin1Char('0')),
                QStringLiteral("Original note %1").arg(row),
            });
            m_notes->insert(m_rows.back().address, m_rows.back().label);
        }
    }

    std::vector<Entry> m_rows;
    std::shared_ptr<execution::Scheduler> m_label_scheduler;
    const std::shared_ptr<QMap<QString, QString>> m_notes{std::make_shared<QMap<QString, QString>>()};
    QHash<QString, QPointer<TextEditSessionModel>> m_editors;
    int m_refresh_calls{0};
    Category m_category{SingleUse};
    bool m_show_used{false};
    bool m_set_address_label_succeeds{false};
};

class MockFileDialog : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title MEMBER m_title NOTIFY titleChanged)
    Q_PROPERTY(QString suggestedName MEMBER m_suggested_name NOTIFY suggestedNameChanged)
    Q_PROPERTY(int fileMode MEMBER m_file_mode NOTIFY fileModeChanged)
    Q_PROPERTY(QString defaultSuffix MEMBER m_default_suffix NOTIFY defaultSuffixChanged)
    Q_PROPERTY(bool confirmOverwrite MEMBER m_confirm_overwrite NOTIFY confirmOverwriteChanged)
    Q_PROPERTY(QStringList nameFilters MEMBER m_name_filters NOTIFY nameFiltersChanged)
    Q_PROPERTY(QUrl currentFolder MEMBER m_current_folder NOTIFY currentFolderChanged)
    Q_PROPERTY(QUrl currentFile MEMBER m_current_file NOTIFY currentFileChanged)
    Q_PROPERTY(QUrl selectedFile MEMBER m_selected_file NOTIFY selectedFileChanged)
    Q_PROPERTY(bool visible MEMBER m_visible NOTIFY visibleChanged)
    Q_PROPERTY(QObject* contextObject READ contextObject WRITE setContextObject NOTIFY contextObjectChanged)
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(int openCalls MEMBER m_open_calls NOTIFY openCallsChanged)

public:
    enum FileMode { OpenFile, SaveFile, Directory };
    Q_ENUM(FileMode)

    QString m_title;
    QString m_suggested_name;
    int m_file_mode{OpenFile};
    QString m_default_suffix;
    bool m_confirm_overwrite{true};
    QStringList m_name_filters;
    QUrl m_current_folder;
    QUrl m_current_file;
    QUrl m_selected_file;
    bool m_visible{false};
    int m_open_calls{0};
    QPointer<QObject> m_context, m_request_context;
    QMetaObject::Connection m_context_destroyed;
    bool m_enabled{true};
    bool m_completing{false};
    inline static QPointer<MockFileDialog> s_active;

    QObject* contextObject() const { return m_context; }
    bool isEnabled() const { return m_enabled; }
    void setContextObject(QObject* context)
    {
        if (m_context == context) return;
        disconnect(m_context_destroyed);
        m_context = context;
        if (context) m_context_destroyed = connect(context, &QObject::destroyed, this, [this] { close(); Q_EMIT contextObjectChanged(); });
        close(); Q_EMIT contextObjectChanged();
    }
    void setEnabled(bool enabled)
    {
        if (m_enabled == enabled) return;
        m_enabled = enabled;
        if (!enabled) close();
        Q_EMIT enabledChanged();
    }

    Q_INVOKABLE bool open()
    {
        if (!m_enabled || m_visible || m_completing || s_active) return false;
        s_active = this;
        m_request_context = m_context;
        ++m_open_calls;
        m_visible = true;
        Q_EMIT openCallsChanged();
        Q_EMIT visibleChanged();
        return true;
    }

    Q_INVOKABLE void close()
    {
        if (!m_visible) return;
        m_visible = false;
        if (s_active == this) s_active.clear();
        Q_EMIT visibleChanged();
        Q_EMIT rejected();
    }

    /** Stand in for the user picking a file. */
    Q_INVOKABLE void selectAndAccept(const QUrl& file)
    {
        if (!m_visible || !m_enabled || m_completing) return;
        m_completing = true;
        const QPointer<MockFileDialog> guard{this};
        const auto restore = qScopeGuard([guard] { if (guard) guard->m_completing = false; });
        m_selected_file = file;
        Q_EMIT selectedFileChanged();
        if (!guard || !m_visible || !m_enabled) return;
        m_visible = false;
        if (s_active == this) s_active.clear();
        Q_EMIT visibleChanged();
        if (guard && m_enabled) { Q_EMIT chosen(m_request_context, m_selected_file); if (guard) Q_EMIT accepted(); }
    }

    /** Exercise a consumer's stale-result guard independently of this mock's
     * normal chooser adapter, which correctly drops a closed dialog result. */
    Q_INVOKABLE void deliverLateAcceptedForTest(const QUrl& file)
    {
        const QPointer<MockFileDialog> guard{this};
        m_selected_file = file;
        Q_EMIT selectedFileChanged();
        if (guard) Q_EMIT accepted();
    }

Q_SIGNALS:
    void titleChanged();
    void suggestedNameChanged();
    void fileModeChanged();
    void defaultSuffixChanged();
    void confirmOverwriteChanged();
    void nameFiltersChanged();
    void currentFolderChanged();
    void currentFileChanged();
    void selectedFileChanged();
    void visibleChanged();
    void contextObjectChanged();
    void enabledChanged();
    void openCallsChanged();
    void accepted();
    void chosen(QObject* context, const QUrl& file);
    void rejected();
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
    Q_PROPERTY(bool paymentReceived MEMBER m_payment_received NOTIFY paymentReceivedChanged)
    Q_PROPERTY(qint64 receivedAmountSatoshi MEMBER m_received_amount NOTIFY receivedAmountChanged)
    Q_PROPERTY(bool isEditing MEMBER m_is_editing NOTIFY isEditingChanged)
    Q_PROPERTY(quint64 generation MEMBER m_generation NOTIFY generationChanged)
    Q_PROPERTY(quint64 revision MEMBER m_revision NOTIFY draftChanged)

public:
    quint64 m_generation{0}, m_revision{0};
    MockPaymentRequest() {
        const auto edit = [this] { ++m_revision; Q_EMIT draftChanged(); };
        connect(this, &MockPaymentRequest::addressChanged, this, edit);
        connect(this, &MockPaymentRequest::addressTypeChanged, this, edit);
    }
    bool m_payment_received{false};
    qint64 m_received_amount{0};
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
    QString qrPayload() const { return m_address.isEmpty() || m_payment_received ? QString{} : QStringLiteral("bitcoin:") + m_address; }
    Q_INVOKABLE void clear()
    {
        ++m_generation;
        Q_EMIT generationChanged();
        m_payment_received = false;
        m_received_amount = 0;
        Q_EMIT receivedAmountChanged();
        Q_EMIT paymentReceivedChanged();
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
        m_amount.setUnit(MockBitcoinAmount::BTC);
        m_amount.setSatoshi(0);
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
    void generationChanged();
    void draftChanged();
    void paymentReceivedChanged();
    void receivedAmountChanged();
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
    Q_INVOKABLE void publishRequestForAddressForTest(const QString& address, const QString& id) { m_fixture_requests.insert(address,id); }
    Q_INVOKABLE void resetRequestFixturesForTest() { m_fixture_requests.clear(); }
    Q_INVOKABLE QString firstRequestIdForAddress(const QString& address) const
    {
        if (m_fixture_requests.contains(address)) return m_fixture_requests.value(address);
        return address == QStringLiteral("bcrt1qreceiveaddress") ? QStringLiteral("req-1") : QString{};
    }
    QHash<QString,QString> m_fixture_requests;

Q_SIGNALS:
    void countChanged();
};

class MockTransaction : public QObject
{
    Q_OBJECT

public:
    enum Status {
        Confirmed,
        Unconfirmed,
        Confirming,
        Conflicted,
        Abandoned,
        Immature,
        NotAccepted
    };
    Q_ENUM(Status)

    enum Type {
        Other,
        Generated,
        SendToAddress,
        SendToOther,
        RecvWithAddress,
        RecvFromOther,
        SendToSelf
    };
    Q_ENUM(Type)
};

class MockSendRecipient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(AmountMode amountMode MEMBER m_amount_mode NOTIFY amountModeChanged)
    Q_PROPERTY(QString inputBudgetError READ inputBudgetError CONSTANT)
    Q_PROPERTY(QObject* address READ address CONSTANT)
    Q_PROPERTY(QString addressError MEMBER m_address_error NOTIFY addressErrorChanged)
    Q_PROPERTY(QObject* amount READ amount CONSTANT)
    Q_PROPERTY(QString amountError MEMBER m_amount_error NOTIFY amountErrorChanged)
    Q_PROPERTY(QString paymentRequestLabel MEMBER m_request_label NOTIFY paymentRequestChanged)
    Q_PROPERTY(QString message MEMBER m_message NOTIFY paymentRequestChanged)
    Q_PROPERTY(bool hasPaymentRequest MEMBER m_has_request NOTIFY paymentRequestChanged)
    Q_PROPERTY(QString label MEMBER m_label NOTIFY labelChanged)
    Q_PROPERTY(bool isValid MEMBER m_is_valid NOTIFY isValidChanged)
    Q_PROPERTY(quint64 validationRevision MEMBER m_validation_revision NOTIFY isValidChanged)
    Q_PROPERTY(int appliedPaymentRequestCalls READ appliedPaymentRequestCalls NOTIFY paymentRequestChanged)
    Q_PROPERTY(bool validationPending MEMBER m_validation_pending NOTIFY isValidChanged)

public:
    enum AmountMode { Fixed, Maximum };
    Q_ENUM(AmountMode)
    AmountMode m_amount_mode{Fixed};
    QString inputBudgetError() const { return {}; }
    Q_INVOKABLE bool editPrivateNote(const QString& value) { if (value.size() > 256) return false; if (m_label != value) { m_label = value; Q_EMIT labelChanged(); } return true; }
    void setAmountMode(AmountMode mode) { if (m_amount_mode != mode) { m_amount_mode = mode; Q_EMIT amountModeChanged(); } }
    MockSendRecipient()
    {
        m_address.setAddress(QStringLiteral("bcrt1qsendtoaddress"));
        const auto edit = [this] { ++m_validation_revision; Q_EMIT isValidChanged(); };
        connect(&m_address, &MockBitcoinAddress::addressChanged, this, edit);
        connect(&m_amount, &BitcoinAmount::inputChanged, this, edit);
        connect(&m_amount, &BitcoinAmount::amountChanged, this, edit);
        connect(this, &MockSendRecipient::labelChanged, this, edit);
        connect(this, &MockSendRecipient::paymentRequestChanged, this, edit);
    }

    MockBitcoinAddress m_address{};
    QString m_address_error;
    MockBitcoinAmount m_amount{};
    QString m_amount_error;
    QString m_label;
    QString m_request_label, m_message;
    bool m_has_request{false};
    Q_INVOKABLE void applyPaymentRequest(const QString& address, const QString& label, const QString& message) { m_address.setAddress(address); m_request_label = label; m_message = message; m_has_request = true; Q_EMIT paymentRequestChanged(); }
    bool m_is_valid{true};
    bool m_validation_pending{false};
    quint64 m_validation_revision{0};
    int appliedPaymentRequestCalls() const { return m_applied_payment_request_calls; }
    int m_applied_payment_request_calls{0};
    Q_INVOKABLE void applyParsedPaymentRequest(const QVariantMap& parsed, bool replace_private_note = false) {
        ++m_applied_payment_request_calls;
        if (!parsed.value("success").toBool()) return;
        applyPaymentRequest(parsed.value("address").toString(), parsed.value("hasLabel").toBool() ? parsed.value("label").toString() : QString{}, parsed.value("hasMessage").toBool() ? parsed.value("uriMessage").toString() : QString{});
        if (parsed.value("hasAmount").toBool()) m_amount.setSatoshi(parsed.value("amountSats").toLongLong());
        const auto note = parsed.value("privateNote").toString();
        if (!note.isEmpty() && (m_label.isEmpty() || replace_private_note)) { m_label = note; Q_EMIT labelChanged(); }
    }

    QObject* address() { return &m_address; }
    QObject* amount() { return &m_amount; }

Q_SIGNALS:
    void amountModeChanged();
    void paymentRequestChanged();
    void addressErrorChanged();
    void amountErrorChanged();
    void labelChanged();
    void isValidChanged();
};

class MockRecipientsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(bool importPending MEMBER m_import_pending NOTIFY validationChanged)
    Q_PROPERTY(bool canAddRecipient READ canAddRecipient NOTIFY countChanged)
    Q_PROPERTY(QString budgetError READ budgetError CONSTANT)
    Q_PROPERTY(bool validationPending READ validationPending CONSTANT)
    Q_PROPERTY(QObject* current READ current NOTIFY currentChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(qint64 totalAmountSatoshi READ totalAmountSatoshi WRITE setTotalAmountSatoshi NOTIFY totalAmountChanged)
    Q_PROPERTY(bool allValid READ allValid WRITE setAllValid NOTIFY validationChanged)
    Q_PROPERTY(QString validationError READ validationError WRITE setValidationError NOTIFY validationChanged)

public:
    bool m_import_pending{false};
    QString revision() const { return QString::number(m_revision); }
    void editRevision() { ++m_revision; Q_EMIT revisionChanged(); }
    bool canAddRecipient() const { return count() < 25; }
    QString budgetError() const { return {}; }
    bool validationPending() const { return false; }
    enum Roles {
        AddressRole = Qt::UserRole + 1,
        LabelRole,
        AmountRole, RecipientRole
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
        for (const auto& connection : m_revision_connections) disconnect(connection);
        m_revision_connections.clear();
        m_current = recipient;
        if (auto* editor = qobject_cast<MockSendRecipient*>(recipient)) {
            m_revision_connections.push_back(connect(&editor->m_amount, &BitcoinAmount::valueEdited, this, &MockRecipientsModel::editRevision));
            m_revision_connections.push_back(connect(&editor->m_address, &MockBitcoinAddress::addressChanged, this, &MockRecipientsModel::editRevision));
            m_revision_connections.push_back(connect(editor, &MockSendRecipient::labelChanged, this, &MockRecipientsModel::editRevision));
            m_revision_connections.push_back(connect(editor, &MockSendRecipient::paymentRequestChanged, this, &MockRecipientsModel::editRevision));
            m_revision_connections.push_back(connect(editor, &MockSendRecipient::amountModeChanged, this, &MockRecipientsModel::editRevision));
        }
        editRevision();
        Q_EMIT currentChanged();
        Q_EMIT currentRecipientChanged();
    }

    Q_INVOKABLE void setCurrentIndex(int index)
    {
        const int bounded = std::clamp(index + 1, 1, std::max(1, count()));
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
        case RecipientRole: return QVariant::fromValue(m_current);
        case AddressRole: return row.address;
        case LabelRole: return row.label;
        case AmountRole: return row.amount;
        default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {RecipientRole, "recipientObject"},
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

    Q_INVOKABLE void prev() { setCurrentIndex(m_current_index - 2); }
    Q_INVOKABLE void next() { setCurrentIndex(m_current_index); }

    Q_INVOKABLE void clear() { clearToFront(); }
    Q_INVOKABLE void removeAt(int) { remove(); }
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
    void revisionChanged();
    void currentChanged();
    void currentRecipientChanged();
    void currentIndexChanged();
    void countChanged();
    void totalAmountChanged();
    void listCleared();
    void validationChanged();

private:
    quint64 m_revision{0};
    std::vector<QMetaObject::Connection> m_revision_connections;
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
    Q_PROPERTY(QString readError READ readError CONSTANT)
    Q_PROPERTY(QString lockError READ lockError CONSTANT)
    Q_PROPERTY(bool lockPending READ lockPending CONSTANT)
    Q_PROPERTY(int markedCount READ markedCount NOTIFY marksChanged)
    Q_PROPERTY(bool marksPending READ marksPending CONSTANT)
    Q_PROPERTY(qint64 availableMinAmount READ availableMinAmount NOTIFY viewChanged)
    Q_PROPERTY(qint64 availableMaxAmount READ availableMaxAmount NOTIFY viewChanged)
    Q_PROPERTY(qint64 minAmount MEMBER m_min_amount NOTIFY viewChanged)
    Q_PROPERTY(qint64 maxAmount MEMBER m_max_amount NOTIFY viewChanged)
    Q_PROPERTY(int lockCalls READ lockCalls NOTIFY viewChanged)
    Q_PROPERTY(QStringList lastLockIds READ lastLockIds NOTIFY viewChanged)
    Q_PROPERTY(int markCalls READ markCalls NOTIFY viewChanged)

public:
    qint64 availableMinAmount() const { return m_available_min; }
    qint64 availableMaxAmount() const { return m_available_max; }
    Q_INVOKABLE void controlAvailableRange(qint64 minimum, qint64 maximum) { m_available_min = minimum; m_available_max = maximum; Q_EMIT viewChanged(); }
    int lockCalls() const { return m_lock_calls; }
    int markCalls() const { return m_mark_calls; }
    QStringList lastLockIds() const { return m_last_lock_ids; }
    Q_INVOKABLE void controlCoinActions(bool admit_marks, bool admit_locks) { m_admit_marks=admit_marks; m_admit_locks=admit_locks; m_lock_calls=0; m_mark_calls=0; m_last_lock_ids.clear(); }
    QString readError() const { return {}; }
    QString lockError() const { return {}; }
    bool lockPending() const { return false; }
    bool marksPending() const { return false; }
    int markedCount() const { return std::count_if(m_rows.begin(), m_rows.end(), [](const CoinRow& row) { return row.marked; }); }
    Q_PROPERTY(qint64 totalSelectedSatoshi READ totalSelectedSatoshi NOTIFY selectedCoinsCountChanged)
    Q_PROPERTY(qint64 totalSatoshi READ totalSatoshi NOTIFY coinCountChanged)
    Q_PROPERTY(qint64 lockedSatoshi READ lockedSatoshi NOTIFY viewChanged)
    Q_PROPERTY(int visibleCount READ coinCount NOTIFY coinCountChanged)
    Q_PROPERTY(QString searchText MEMBER m_search NOTIFY viewChanged)
    Q_PROPERTY(QString sortBy MEMBER m_sort NOTIFY viewChanged)
    Q_PROPERTY(QString filter MEMBER m_filter NOTIFY viewChanged)
    Q_PROPERTY(QString groupBy MEMBER m_group NOTIFY viewChanged)
    Q_PROPERTY(bool sortDescending MEMBER m_descending NOTIFY viewChanged)
    Q_PROPERTY(int selectedCoinsCount READ selectedCoinsCount NOTIFY selectedCoinsCountChanged)
    Q_PROPERTY(QString selectionToken READ selectionToken NOTIFY selectionSnapshotChanged)
    Q_PROPERTY(bool selectionPending MEMBER m_selection_pending NOTIFY selectionSnapshotChanged)
    Q_PROPERTY(bool manualInputSelection READ manualInputSelection NOTIFY selectionSnapshotChanged)
    Q_PROPERTY(bool selectionReconciliationRequired MEMBER m_selection_reconciliation_required NOTIFY selectionSnapshotChanged)
    Q_PROPERTY(int coinCount READ coinCount NOTIFY coinCountChanged)
    Q_PROPERTY(QString totalSelected READ totalSelected NOTIFY totalSelectedChanged)

public:
    enum Roles {
        AddressRole = Qt::UserRole + 1,
        AmountRole,
        LabelRole,
        LockedRole,
        SelectedRole, DateRole, IdRole, GroupRole, SatoshiRole, GroupFirstRole, GroupLastRole, MarkedRole
    };

    struct CoinRow {
        QString address;
        QString amount;
        QString label;
        bool locked;
        bool selected;
        QString id;
        bool marked{false};
        qint64 satoshi{0};
    };

    MockCoinsListModel()
    {
        m_rows = {
            {QStringLiteral("bcrt1qcoin1"), QStringLiteral("0.00100000 BTC"), QStringLiteral("utxo-1"), false, false, QStringLiteral("coin-1"), false, 100000},
            {QStringLiteral("bcrt1qcoin2"), QStringLiteral("0.00200000 BTC"), QStringLiteral("utxo-2"), false, false, QStringLiteral("coin-2"), false, 200000},
            {QStringLiteral("bcrt1qcoin3"), QStringLiteral("0.00300000 BTC"), QStringLiteral(""), true, false, QStringLiteral("coin-3"), false, 300000},
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
        case DateRole: return QDateTime::fromSecsSinceEpoch(1700000000);
        case IdRole: return row.id;
        case GroupRole: return QStringLiteral("November 2023");
        case GroupFirstRole: return index.row() == 0;
        case GroupLastRole: return index.row() == rowCount() - 1;
        case SatoshiRole: return row.satoshi;
        case AddressRole: return row.address;
        case AmountRole: return row.amount;
        case LabelRole: return row.label;
        case LockedRole: return row.locked;
        case SelectedRole: return row.selected;
        case MarkedRole: return row.marked;
        default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {DateRole, "date"}, {IdRole, "coinId"}, {GroupRole, "groupTitle"}, {SatoshiRole, "amountSatoshi"},
            {GroupFirstRole, "groupFirst"}, {GroupLastRole, "groupLast"},
            {AddressRole, "address"},
            {AmountRole, "amount"},
            {LabelRole, "label"},
            {LockedRole, "locked"},
            {SelectedRole, "selected"},
            {MarkedRole, "walletMarked"},
        };
    }

    int selectedCoinsCount() const
    {
        return std::count_if(m_rows.begin(), m_rows.end(), [](const CoinRow& row) { return row.selected; });
    }
    bool manualInputSelection() const { return m_manual_input_selection; }
    Q_INVOKABLE bool setManualInputSelection(bool manual)
    {
        const bool mode_changed = m_manual_input_selection != manual;
        const bool has_selection = selectedCoinsCount() != 0;
        if (!mode_changed && (manual || !has_selection)) return true;
        m_manual_input_selection = manual;
        if (!manual) for (auto& row : m_rows) row.selected = false;
        if (!manual && has_selection) {
            emitAggregateSignals();
            Q_EMIT selectionSnapshotChanged();
        } else {
            ++m_selection_revision;
            // The real command publishes one admission and one owned outcome.
            // Keep both observable in this presentation-only fixture.
            Q_EMIT selectionSnapshotChanged();
            Q_EMIT selectionSnapshotChanged();
        }
        return true;
    }

    qint64 totalSelectedSatoshi() const { qint64 sum = 0; for (size_t i=0;i<m_rows.size();++i) if(m_rows[i].selected) sum += m_rows[i].satoshi; return sum; }
    qint64 totalSatoshi() const { qint64 sum=0; for(const auto& row:m_rows)sum+=row.satoshi; return sum; }
    qint64 lockedSatoshi() const { qint64 sum=0; for(const auto& row:m_rows)if(row.locked)sum+=row.satoshi; return sum; }
    Q_INVOKABLE void beginSelection() { m_snapshot = m_rows; }
    Q_INVOKABLE void applySelection() { m_snapshot.clear(); }
    Q_INVOKABLE void cancelSelection() { if (m_snapshot.empty()) return; beginResetModel(); m_rows=m_snapshot; m_snapshot.clear(); endResetModel(); emitAggregateSignals(); }
    Q_INVOKABLE bool setCoinsLocked(const QStringList& ids, bool locked) {
        ++m_lock_calls; m_last_lock_ids=ids;
        if (!m_admit_locks) {Q_EMIT viewChanged();return false;}
        for (const auto& id : ids) {
            const auto coin=std::find_if(m_rows.begin(),m_rows.end(),[&](const auto& row){return row.id==id;});
            if (coin==m_rows.end()) {Q_EMIT viewChanged();return false;}
        }
        for (int row=0;row<rowCount();++row) if (ids.contains(m_rows[row].id)) {m_rows[row].locked=locked;Q_EMIT dataChanged(index(row),index(row),{LockedRole});}
        Q_EMIT viewChanged(); return true;
    }
    Q_INVOKABLE bool toggleMarkById(const QString& id)
    {
        ++m_mark_calls; if (!m_admit_marks) return false;
        for (int row = 0; row < rowCount(); ++row) {
            auto& coin = m_rows[row]; if (coin.id != id) continue;
            coin.marked = !coin.marked;
            Q_EMIT dataChanged(index(row), index(row), {MarkedRole}); Q_EMIT marksChanged(); return true;
        }
        return false;
    }
    Q_INVOKABLE bool clearMarks()
    {
        for (auto& coin : m_rows) coin.marked = false;
        if (rowCount()) Q_EMIT dataChanged(index(0), index(rowCount() - 1), {MarkedRole});
        Q_EMIT marksChanged(); return true;
    }
    Q_INVOKABLE bool lockMarkedCoins(bool locked)
    {
        QStringList ids; for (const auto& coin:m_rows) if (coin.marked) ids.push_back(coin.id);
        if (!setCoinsLocked(ids,locked)) return false;
        clearMarks(); return true;
    }
    QString selectionToken() const { return QString::number(m_selection_revision); }
    int coinCount() const { return rowCount(); }

    QString totalSelected() const
    {
        return QStringLiteral("%1 selected").arg(selectedCoinsCount());
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

    Q_INVOKABLE bool toggleCoinSelectionById(const QString& id)
    {
        for (int i = 0; i < rowCount(); ++i) {
            CoinRow& row = m_rows[static_cast<size_t>(i)];
            if (row.id != id) continue;
            row.selected = !row.selected;
            const QModelIndex model_index = createIndex(i, 0);
            Q_EMIT dataChanged(model_index, model_index, {SelectedRole});
            emitAggregateSignals();
            return true;
        }
        return false;
    }

    Q_INVOKABLE bool isCoinSelected(const QString& id) const
    {
        const auto coin = std::find_if(m_rows.begin(), m_rows.end(), [&id](const CoinRow& row) { return row.id == id; });
        return coin != m_rows.end() && coin->selected;
    }

    Q_INVOKABLE void prependCoinForTest()
    {
        beginInsertRows(QModelIndex{}, 0, 0);
        m_rows.insert(m_rows.begin(), {QStringLiteral("bcrt1qnewcoin"), QStringLiteral("0.00400000 BTC"),
                                      QStringLiteral("new-coin"), false, false, QStringLiteral("new-coin"), false, 400000});
        endInsertRows();
        Q_EMIT coinCountChanged();
    }

    Q_INVOKABLE void removeCoinForTest(const QString& id)
    {
        const auto coin = std::find_if(m_rows.begin(), m_rows.end(), [&id](const CoinRow& row) { return row.id == id; });
        if (coin == m_rows.end()) return;
        const int index = static_cast<int>(std::distance(m_rows.begin(), coin));
        beginRemoveRows(QModelIndex{}, index, index);
        m_rows.erase(coin);
        endRemoveRows();
        Q_EMIT coinCountChanged();
    }

    Q_INVOKABLE void reset()
    {
        m_selection_pending = false;
        controlCoinActions(true, true); m_min_amount = -1; m_max_amount = -1;
        m_manual_input_selection = false; m_selection_reconciliation_required = false;
        beginResetModel();
        m_rows = {
            {QStringLiteral("bcrt1qcoin1"), QStringLiteral("0.00100000 BTC"), QStringLiteral("utxo-1"), false, false, QStringLiteral("coin-1"), false, 100000},
            {QStringLiteral("bcrt1qcoin2"), QStringLiteral("0.00200000 BTC"), QStringLiteral("utxo-2"), false, false, QStringLiteral("coin-2"), false, 200000},
            {QStringLiteral("bcrt1qcoin3"), QStringLiteral("0.00300000 BTC"), QStringLiteral(""), true, false, QStringLiteral("coin-3"), false, 300000},
        };
        endResetModel();
        Q_EMIT coinCountChanged(); Q_EMIT viewChanged();
        emitAggregateSignals();
    }
    Q_INVOKABLE void setSelectionReconciliationRequiredForTest(bool required)
    {
        if (m_selection_reconciliation_required == required) return;
        m_selection_reconciliation_required = required;
        Q_EMIT selectionSnapshotChanged();
    }

    Q_INVOKABLE void update() {}

Q_SIGNALS:
    void marksChanged();
    void viewChanged();
    void selectionSnapshotChanged();
    void selectedCoinsCountChanged();
    void coinCountChanged();
    void totalSelectedChanged();

private:
    qint64 m_available_min{100000}, m_available_max{300000};
    void emitAggregateSignals()
    {
        ++m_selection_revision;
        Q_EMIT selectionSnapshotChanged();
        Q_EMIT selectedCoinsCountChanged();
        Q_EMIT totalSelectedChanged();
    }

    quint64 m_selection_revision{0};
    bool m_selection_pending{false};
    std::vector<CoinRow> m_rows{}, m_snapshot{};
    qint64 m_min_amount{-1},m_max_amount{-1}; bool m_admit_marks{true},m_admit_locks{true};
    int m_lock_calls{0},m_mark_calls{0}; QStringList m_last_lock_ids;
    QString m_search, m_sort{"date"}, m_filter{"all"}, m_group{"month"};
    bool m_descending{true}, m_manual_input_selection{false}, m_selection_reconciliation_required{false};
};

class MockBackupFileOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int phase MEMBER m_phase NOTIFY changed)
    Q_PROPERTY(bool pending MEMBER m_pending NOTIFY changed)
    Q_PROPERTY(bool confirmationRequired MEMBER m_confirmation NOTIFY changed)
    Q_PROPERTY(bool replacing MEMBER m_replacing NOTIFY changed)
    Q_PROPERTY(QString finalPath MEMBER m_path NOTIFY changed)
    Q_PROPERTY(QString error MEMBER m_error NOTIFY changed)
public:
    explicit MockBackupFileOperation(QObject* parent = nullptr) : QObject(parent) {}
    enum Phase { Queued, Inspecting, AwaitingConfirmation, Ready, Writing, Succeeded, Failed, Declined, Retired };
    Q_ENUM(Phase)
    int m_phase{Succeeded};
    bool m_pending{false}, m_confirmation{false}, m_replacing{false};
    QString m_path, m_error;
    Q_INVOKABLE bool approve() { m_phase = Succeeded; m_pending = m_confirmation = false; Q_EMIT changed(); return true; }
    Q_INVOKABLE bool refuse() { m_phase = Declined; m_pending = m_confirmation = false; Q_EMIT changed(); return true; }
Q_SIGNALS:
    void changed();
};

class MockFileExportOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool pending MEMBER m_pending NOTIFY changed)
    Q_PROPERTY(bool confirmationRequired MEMBER m_confirmation NOTIFY changed)
    Q_PROPERTY(bool replacing MEMBER m_replacing NOTIFY changed)
    Q_PROPERTY(bool saved MEMBER m_saved NOTIFY changed)
    Q_PROPERTY(QString finalPath MEMBER m_path NOTIFY changed)
    Q_PROPERTY(QString errorText MEMBER m_error NOTIFY changed)
public:
    explicit MockFileExportOperation(QObject* parent = nullptr) : QObject(parent) {}
    bool m_pending{false}, m_confirmation{false}, m_replacing{false}, m_saved{false};
    QString m_path, m_error;
    Q_INVOKABLE bool approve() { m_confirmation = false; Q_EMIT changed(); return true; }
    Q_INVOKABLE bool refuse() { m_confirmation = false; Q_EMIT changed(); return true; }
    Q_INVOKABLE void retire() { m_pending = m_confirmation = false; Q_EMIT changed(); }
Q_SIGNALS:
    void changed();
};

class MockImageExportOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool pending MEMBER m_pending NOTIFY changed)
    Q_PROPERTY(bool saved MEMBER m_saved NOTIFY changed)
    Q_PROPERTY(QString errorText MEMBER m_error NOTIFY changed)
    Q_PROPERTY(QUrl path MEMBER m_path CONSTANT)
    Q_PROPERTY(int retireCalls MEMBER m_retired NOTIFY changed)
public:
    explicit MockImageExportOperation(QObject* parent = nullptr) : QObject(parent) {}
    bool m_pending{true}, m_saved{false};
    int m_retired{0};
    QUrl m_path;
    QString m_error;
    Q_INVOKABLE void retire() { ++m_retired; Q_EMIT changed(); }
    Q_INVOKABLE void complete(bool saved, const QString& error) {
        m_pending = false; m_saved = saved; m_error = error; Q_EMIT changed();
    }
Q_SIGNALS:
    void changed();
};

// Presentation fixture only. Public-interface effects and crypto are covered
// by SignVerifyMessageModelTests and independent Core/RPC integration tests.
class MockSignVerifyMessageModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool canSign READ canSign NOTIFY signingStateChanged)
    Q_PROPERTY(bool canVerify READ canVerify NOTIFY verificationChanged)
    Q_PROPERTY(TextEditInput* address READ address CONSTANT)
    Q_PROPERTY(TextEditInput* message READ message CONSTANT)
    Q_PROPERTY(TextEditInput* verificationAddressInput READ verificationAddressInput CONSTANT)
    Q_PROPERTY(TextEditInput* verificationMessageInput READ verificationMessageInput CONSTANT)
    Q_PROPERTY(TextEditInput* candidateSignature READ candidateSignature CONSTANT)
    Q_PROPERTY(QString signature READ signature WRITE setSignature NOTIFY signatureChanged)
    Q_PROPERTY(QString signingError READ signingError WRITE setSigningError NOTIFY signingStateChanged)
    Q_PROPERTY(bool signingNeedsUnlock READ signingNeedsUnlock WRITE setNeedsUnlock NOTIFY signingStateChanged)
    Q_PROPERTY(MessageSigningPrompt* signingPrompt READ signingPrompt NOTIFY signingStateChanged)
    Q_PROPERTY(bool signingBusy MEMBER m_sign_busy NOTIFY signingStateChanged)
    Q_PROPERTY(bool signingSucceeded READ signingSucceeded NOTIFY signingStateChanged)
    Q_PROPERTY(quint64 signingId MEMBER m_sign_id NOTIFY signingStateChanged)
    Q_PROPERTY(QString signingAddress READ signingAddress WRITE setSigningAddress NOTIFY inputsChanged)
    Q_PROPERTY(QString signingMessage READ signingMessage WRITE setSigningMessage NOTIFY inputsChanged)
    Q_PROPERTY(QString verificationAddress READ verificationAddress WRITE setVerificationAddress NOTIFY inputsChanged)
    Q_PROPERTY(QString verificationMessage READ verificationMessage WRITE setVerificationMessage NOTIFY inputsChanged)
    Q_PROPERTY(QString verificationSignature READ verificationSignature WRITE setVerificationSignature NOTIFY inputsChanged)
    Q_PROPERTY(bool signingAddressValid READ signingAddressValid NOTIFY inputsChanged)
    Q_PROPERTY(bool verificationAddressValid READ verificationAddressValid NOTIFY inputsChanged)
    Q_PROPERTY(bool addressValidationPending READ addressValidationPending CONSTANT)
    Q_PROPERTY(bool verificationBusy MEMBER m_verify_busy NOTIFY verificationChanged)
    Q_PROPERTY(bool verificationValid MEMBER m_verify_valid NOTIFY verificationChanged)
    Q_PROPERTY(QString verificationText MEMBER m_verify_text NOTIFY verificationChanged)
    Q_PROPERTY(quint64 verificationId MEMBER m_verify_id NOTIFY verificationChanged)
    Q_PROPERTY(bool manualCompletion MEMBER m_manual)
    Q_PROPERTY(int signCalls MEMBER m_sign_calls NOTIFY fixtureChanged)
    Q_PROPERTY(int passphraseCalls MEMBER m_passphrase_calls NOTIFY fixtureChanged)
    Q_PROPERTY(int verifyCalls MEMBER m_verify_calls NOTIFY fixtureChanged)
    Q_PROPERTY(QString lastSigningAddress MEMBER m_last_address NOTIFY fixtureChanged)
    Q_PROPERTY(QString lastSigningMessage MEMBER m_last_message NOTIFY fixtureChanged)
    Q_PROPERTY(QString lastPassphrase MEMBER m_last_passphrase NOTIFY fixtureChanged)
public:
    explicit MockSignVerifyMessageModel(QObject* parent = nullptr) : QObject(parent) {
        connect(&m_address_text, &TextEditInput::edited, this, &MockSignVerifyMessageModel::setSigningAddress);
        connect(&m_message_text, &TextEditInput::edited, this, &MockSignVerifyMessageModel::setSigningMessage);
        connect(&m_verify_address_text, &TextEditInput::edited, this, &MockSignVerifyMessageModel::setVerificationAddress);
        connect(&m_verify_message_text, &TextEditInput::edited, this, &MockSignVerifyMessageModel::setVerificationMessage);
        connect(&m_verify_signature_text, &TextEditInput::edited, this, &MockSignVerifyMessageModel::setVerificationSignature);
    }
    TextEditInput* address() { return &m_address_text; }
    TextEditInput* message() { return &m_message_text; }
    TextEditInput* verificationAddressInput() { return &m_verify_address_text; }
    TextEditInput* verificationMessageInput() { return &m_verify_message_text; }
    TextEditInput* candidateSignature() { return &m_verify_signature_text; }
    bool canSign() const { return signingAddressValid() && !m_sign_busy; }
    bool canVerify() const { return verificationAddressValid() && !m_verify_busy; }
    Q_INVOKABLE QString sign() { const auto id = requestSignMessage(m_address_text.text(), m_message_text.text()); return id ? QString::number(id) : QString{}; }
    Q_INVOKABLE QString verify(const QString& signature) { const auto id = requestVerifyMessage(m_verify_address_text.text(), m_verify_message_text.text(), signature); return id ? QString::number(id) : QString{}; }
    QString signature() const { return m_signature; }
    QString signingError() const { return m_signing_error; }
    bool signingNeedsUnlock() const { return m_needs_unlock; }
    MessageSigningPrompt* signingPrompt() const { return m_prompt; }
    QString signingAddress() const { return m_sign_address; }
    QString signingMessage() const { return m_sign_message; }
    QString verificationAddress() const { return m_verify_address; }
    QString verificationMessage() const { return m_verify_message; }
    QString verificationSignature() const { return m_verify_signature; }
    bool signingSucceeded() const { return !m_sign_busy && !m_needs_unlock && !m_signature.isEmpty(); }
    bool signingAddressValid() const { return m_sign_address == QStringLiteral("1ValidLegacyAddress"); }
    bool verificationAddressValid() const { return m_verify_address == QStringLiteral("1ValidLegacyAddress"); }
    bool addressValidationPending() const { return false; }
    void setSignature(const QString& value) { m_signature = value; Q_EMIT signatureChanged(); Q_EMIT signingStateChanged(); }
    void setSigningError(const QString& value) { m_signing_error = value; updatePrompt(); Q_EMIT signingStateChanged(); }
    void setNeedsUnlock(bool value) { m_needs_unlock = value; updatePrompt(); Q_EMIT signingStateChanged(); }
    void setSigningAddress(const QString& value) { if (m_sign_address == value) return; m_sign_address = value; m_address_text.project(value); invalidateSign(); Q_EMIT inputsChanged(); }
    void setSigningMessage(const QString& value) { if (m_sign_message == value) return; m_sign_message = value; m_message_text.project(value); invalidateSign(); Q_EMIT inputsChanged(); }
    void setVerificationAddress(const QString& value) { if (m_verify_address == value) return; m_verify_address = value; m_verify_address_text.project(value); clearVerification(); Q_EMIT inputsChanged(); }
    void setVerificationMessage(const QString& value) { if (m_verify_message == value) return; m_verify_message = value; m_verify_message_text.project(value); clearVerification(); Q_EMIT inputsChanged(); }
    void setVerificationSignature(const QString& value) { if (m_verify_signature == value) return; m_verify_signature = value; m_verify_signature_text.project(value); clearVerification(); Q_EMIT inputsChanged(); }
    Q_INVOKABLE void clear() { clearSigningStatus(); clearVerification(); }
    Q_INVOKABLE void clearSigningStatus() { if (!m_sign_busy) invalidateSign(); }
    Q_INVOKABLE void clearVerification() { ++m_verify_revision; m_verify_valid = false; m_verify_text.clear(); Q_EMIT verificationChanged(); }
    Q_INVOKABLE void resetForTest() {
        m_manual = false; m_pending_sign.reset(); m_pending_verify.reset(); m_sign_busy = false; m_verify_busy = false;
        setSigningAddress({}); setSigningMessage({}); setVerificationAddress({}); setVerificationMessage({}); setVerificationSignature({});
        clear(); m_sign_calls = m_passphrase_calls = m_verify_calls = 0; m_last_address.clear(); m_last_message.clear(); m_last_passphrase.clear();
        Q_EMIT fixtureChanged();
    }
    Q_INVOKABLE quint64 requestSignMessage(const QString& address, const QString& message) { ++m_sign_calls; Q_EMIT fixtureChanged(); return queueSign(address, message); }
    Q_INVOKABLE quint64 requestSignMessageWithPassphrase(const QString& address, const QString& message, const QString& secret) {
        ++m_passphrase_calls; m_last_passphrase = secret; Q_EMIT fixtureChanged(); return queueSign(address, message);
    }
    Q_INVOKABLE bool completeSigningForTest(const QString& outcome, const QString& error) {
        if (!m_pending_sign || (outcome != "success" && outcome != "unlock" && outcome != "error")) return false;
        const auto pending = std::exchange(m_pending_sign, {});
        if (pending->id != m_sign_id) return false;
        m_sign_busy = false;
        if (pending->revision == m_sign_revision) {
            if (outcome == "success") m_signature = pending->address == "1ValidLegacyAddress" && !pending->message.isEmpty() ? QStringLiteral("test-signature") : QString{};
            m_needs_unlock = outcome == "unlock"; m_signing_error = error;
        }
        updatePrompt(); Q_EMIT signatureChanged(); Q_EMIT signingStateChanged(); return true;
    }
    Q_INVOKABLE quint64 requestVerifyMessage(const QString& address, const QString& message, const QString& signature) {
        ++m_verify_calls; Q_EMIT fixtureChanged(); if (m_verify_busy) return 0;
        setVerificationAddress(address); setVerificationMessage(message); setVerificationSignature(signature);
        const auto id = ++m_verify_id; m_pending_verify = Pending{id, m_verify_revision, address, message, signature};
        m_verify_busy = true; Q_EMIT verificationChanged();
        if (!m_manual) QTimer::singleShot(0, this, [this, id] { if (m_pending_verify && m_pending_verify->id == id) completeVerificationForTest(); });
        return id;
    }
    Q_INVOKABLE bool completeVerificationForTest() {
        if (!m_pending_verify) return false;
        const auto pending = std::exchange(m_pending_verify, {}); if (pending->id != m_verify_id) return false;
        if (pending->revision == m_verify_revision) {
            m_verify_valid = pending->address == "1ValidLegacyAddress" && pending->message == "Signed statement" && pending->signature == "test-signature";
            m_verify_text = m_verify_valid ? "Message verified successfully." : "Message verification failed.";
        }
        m_verify_busy = false; Q_EMIT verificationChanged(); return true;
    }
Q_SIGNALS:
    void signatureChanged();
    void signingStateChanged();
    void verificationChanged();
    void inputsChanged();
    void fixtureChanged();
private:
    struct Pending { quint64 id, revision; QString address, message, signature; };
    void retirePrompt() {
        auto* prompt = std::exchange(m_prompt, nullptr); if (!prompt) return;
        prompt->project(false, false, {}); prompt->deleteLater();
    }
    void updatePrompt() {
        if (m_prompt && (m_prompt_revision != m_sign_revision || (!m_needs_unlock && !m_sign_busy))) { retirePrompt(); return; }
        if (!m_prompt && m_needs_unlock) {
            const auto revision = m_sign_revision;
            const auto address = m_sign_address, message = m_sign_message;
            m_prompt = new MessageSigningPrompt([this, revision, address, message](const QString& secret) {
                return revision == m_sign_revision ? requestSignMessageWithPassphrase(address, message, secret) : 0;
            }, [this, revision] {
                if (revision != m_sign_revision) return;
                ++m_sign_revision; m_needs_unlock = false; m_signing_error.clear(); retirePrompt(); Q_EMIT signingStateChanged();
            }, this);
            m_prompt_revision = revision;
        }
        if (m_prompt) m_prompt->project(true, m_sign_busy, m_signing_error);
    }
    void invalidateSign() {
        ++m_sign_revision; m_signature.clear(); m_signing_error.clear(); m_needs_unlock = false;
        updatePrompt(); Q_EMIT signatureChanged(); Q_EMIT signingStateChanged();
    }
    quint64 queueSign(const QString& address, const QString& message) {
        if (m_sign_busy) return 0;
        setSigningAddress(address); setSigningMessage(message);
        const auto id = ++m_sign_id; m_pending_sign = Pending{id, m_sign_revision, address, message, {}};
        m_last_address = address; m_last_message = message; m_needs_unlock = false; m_signing_error.clear(); m_sign_busy = true;
        updatePrompt(); Q_EMIT fixtureChanged(); Q_EMIT signingStateChanged();
        if (!m_manual) QTimer::singleShot(0, this, [this, id] { if (m_pending_sign && m_pending_sign->id == id) completeSigningForTest("success", {}); });
        return id;
    }
    QString m_signature, m_signing_error, m_sign_address, m_sign_message, m_verify_address, m_verify_message, m_verify_signature, m_verify_text;
    QString m_last_address, m_last_message, m_last_passphrase;
    bool m_needs_unlock{false}, m_sign_busy{false}, m_verify_busy{false}, m_verify_valid{false}, m_manual{false};
    int m_sign_calls{0}, m_passphrase_calls{0}, m_verify_calls{0};
    quint64 m_sign_id{0}, m_verify_id{0}, m_sign_revision{0}, m_verify_revision{0}, m_prompt_revision{0};
    std::optional<Pending> m_pending_sign, m_pending_verify;
    TextEditInput m_address_text{this}, m_message_text{this}, m_verify_address_text{this}, m_verify_message_text{this}, m_verify_signature_text{this};
    MessageSigningPrompt* m_prompt{nullptr};
};

// Presentation fixture only. Native executor/ownership behavior is exercised by
// test_flowdetailsmodel.cpp; this supplies prepared rows to QML component tests.
class MockFlowRows : public QAbstractListModel
{
public:
    explicit MockFlowRows(QObject* parent) : QAbstractListModel(parent) {}
    QVariantList values;
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : values.size(); }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "entry"}}; }
    QVariant data(const QModelIndex& index, int role) const override { return index.isValid() && index.row() < values.size() && (role == Qt::UserRole || role == Qt::DisplayRole) ? values[index.row()] : QVariant{}; }
    void set(QVariantList rows) { beginResetModel(); values = std::move(rows); endResetModel(); }
};
class MockFlowDetails : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* context READ context NOTIFY snapshotChanged)
    Q_PROPERTY(quint64 revision MEMBER m_revision NOTIFY snapshotChanged)
    Q_PROPERTY(bool pending READ pending CONSTANT)
    Q_PROPERTY(QString error READ error CONSTANT)
    Q_PROPERTY(QAbstractItemModel* inputRows READ inputRows CONSTANT)
    Q_PROPERTY(QAbstractItemModel* outputRows READ outputRows CONSTANT)
    Q_PROPERTY(QAbstractItemModel* recipientRows READ recipientRows CONSTANT)
    Q_PROPERTY(QAbstractItemModel* entryDocument READ entryDocument CONSTANT)
    Q_PROPERTY(bool entryDetailsBusy MEMBER m_document_pending NOTIFY entryDetailsChanged)
    Q_PROPERTY(QString entryDetailsError READ error CONSTANT)
public:
    explicit MockFlowDetails(QObject* parent) : QObject(parent) {}
    bool pending() const { return false; } QString error() const { return {}; } QObject* context() const { return m_context; }
    QAbstractItemModel* inputRows() { return &m_inputs; } QAbstractItemModel* outputRows() { return &m_outputs; }
    QAbstractItemModel* recipientRows() { return &m_recipients; } QAbstractItemModel* entryDocument() { return &m_document; }
    void setSource(QVariantMap flow, QVariantList recipients, QObject* context, quint64 revision) {
        m_flow = std::move(flow); m_full_recipients = std::move(recipients); m_context = context; m_revision = revision;
        QVariantList inputs, outputs, values;
        for (const auto& row : m_flow.value("inputs").toList()) inputs.append(BoundedFlowEntry(row.toMap()));
        for (const auto& row : m_flow.value("outputs").toList()) outputs.append(BoundedFlowEntry(row.toMap()));
        for (const auto& row : m_full_recipients) values.append(BoundedFlowEntry(row.toMap()));
        m_inputs.set(inputs); m_outputs.set(outputs); m_recipients.set(values); m_document.set({}); m_document_pending = false;
        Q_EMIT snapshotChanged(); Q_EMIT entryDetailsChanged();
    }
    Q_INVOKABLE quint64 requestEntryDetails(const QString& side, int row, QObject* context, quint64 revision) {
        if (context != m_context || revision != m_revision || row < 0) return 0;
        const auto values = side == "recipients" ? m_full_recipients : m_flow.value(side).toList(); if (row >= values.size()) return 0;
        const auto& input = values[row]; const auto ticket = ++m_ticket; m_document_pending = true; Q_EMIT entryDetailsChanged();
        QTimer::singleShot(0, this, [this, input, ticket, revision] {
            if (ticket != m_ticket || revision != m_revision) return;
            m_document.set(FlowEntryDocument(input)); m_document_pending = false; Q_EMIT entryDetailsChanged();
        }); return ticket;
    }
Q_SIGNALS:
    void snapshotChanged(); void entryDetailsChanged();
private:
    QPointer<QObject> m_context;
    quint64 m_revision{0}, m_ticket{0}; bool m_document_pending{false};
    QVariantMap m_flow; QVariantList m_full_recipients;
    MockFlowRows m_inputs{this}, m_outputs{this}, m_recipients{this}, m_document{this};
};

// PSBT document presentation owned by the original mock wallet. The async
// command below owns its result separately from this observation object.
class MockPsbtDocument : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString matchedTxid READ matchedTxid NOTIFY changed)
public:
    explicit MockPsbtDocument(QObject* parent=nullptr):QObject(parent){}
    QString error()const{return m_error;}
    QString matchedTxid()const{return m_matched_txid;}
    Q_INVOKABLE void setMatchedTxidForTest(const QString& id){if(m_matched_txid==id)return;m_matched_txid=id;Q_EMIT changed();}
    void setError(QString error){if(m_error==error)return;m_error=std::move(error);Q_EMIT changed();}
    Q_INVOKABLE void clear(){setError({});setMatchedTxidForTest({});}
Q_SIGNALS:void changed();
private:QString m_error,m_matched_txid;
};

namespace {
execution::SnapshotServices& MockFeeServices() { static execution::SnapshotServices value; return value; }
std::vector<QPointer<FeeSelectionModel>>& MockFeeOwners() { static std::vector<QPointer<FeeSelectionModel>> value; return value; }
}
class MockSecurityOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString operationId MEMBER m_id CONSTANT)
    Q_PROPERTY(QString promptId MEMBER m_id CONSTANT)
    Q_PROPERTY(Kind kind MEMBER m_kind CONSTANT)
    Q_PROPERTY(Phase phase MEMBER m_phase NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canSubmit READ canSubmit NOTIFY changed)
    Q_PROPERTY(bool canCancel READ canSubmit NOTIFY changed)
    Q_PROPERTY(QString error MEMBER m_error NOTIFY changed)
public:
    enum Kind { Encrypt, ChangePassphrase };
    Q_ENUM(Kind)
    enum Phase { AwaitingCredentials, Pending, Succeeded, Failed, Uncertain, Cancelled };
    Q_ENUM(Phase)
    MockSecurityOperation(Kind kind, std::function<bool()> submit, QObject* parent)
        : QObject(parent), m_kind(kind), m_submit(std::move(submit)) {}
    bool busy() const { return m_phase == Pending; }
    bool canSubmit() const { return m_phase == AwaitingCredentials; }
    Q_INVOKABLE bool submitCredentials(const QString& prompt, const QString&, const QString& replacement, const QString& confirmation)
    {
        if (prompt != m_id || !canSubmit() || !m_submit()) return false;
        m_phase = Pending;
        if (replacement != confirmation) { m_phase = Failed; m_error = QStringLiteral("The new passwords do not match."); }
        Q_EMIT changed();
        return true;
    }
    Q_INVOKABLE bool cancel()
    {
        if (!canSubmit()) return false;
        m_phase = Cancelled; Q_EMIT changed(); return true;
    }
    void finish(bool success)
    {
        if (!busy()) return;
        m_phase = success ? Succeeded : Failed;
        m_error = success ? QString{} : QStringLiteral("Security operation failed");
        Q_EMIT changed();
    }
    const QString m_id{QUuid::createUuid().toString(QUuid::WithoutBraces)};
    const Kind m_kind;
    Phase m_phase{AwaitingCredentials};
    QString m_error;
Q_SIGNALS:
    void changed();
private:
    const std::function<bool()> m_submit;
};

class MockWalletSecurity : public QObject
{
    Q_OBJECT
    Q_PROPERTY(MockSecurityOperation* activeOperation MEMBER m_active NOTIFY changed)
    Q_PROPERTY(bool canEncrypt READ canBegin NOTIFY changed)
    Q_PROPERTY(bool canChangePassphrase READ canBegin NOTIFY changed)
public:
    MockWalletSecurity(QObject* parent, std::function<bool()> available, std::function<bool()> submit)
        : QObject(parent), m_available(std::move(available)), m_submit(std::move(submit)) {}
    Q_INVOKABLE MockSecurityOperation* beginEncryption() { return begin(MockSecurityOperation::Encrypt); }
    Q_INVOKABLE MockSecurityOperation* beginPassphraseChange() { return begin(MockSecurityOperation::ChangePassphrase); }
    bool canBegin() const { return m_available() && (!m_active || (!m_active->busy() && !m_active->canSubmit())); }
    MockSecurityOperation* begin(MockSecurityOperation::Kind kind)
    {
        if (!m_available() || (m_active && (m_active->busy() || m_active->canSubmit()))) return nullptr;
        if (m_active) m_active->deleteLater();
        m_active = new MockSecurityOperation(kind, m_submit, this);
        connect(m_active, &MockSecurityOperation::changed, this, &MockWalletSecurity::changed);
        Q_EMIT changed();
        return m_active;
    }
    void finish(bool success) { if (m_active) m_active->finish(success); }
    void reset() { if (m_active) m_active->deleteLater(); m_active = nullptr; Q_EMIT changed(); }
    MockSecurityOperation* m_active{nullptr};
Q_SIGNALS:
    void changed();
private:
    const std::function<bool()> m_available, m_submit;
};

class MockWalletQmlModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(FeeSelectionModel* fees READ fees CONSTANT)
    Q_PROPERTY(QString backupFileName READ backupFileName NOTIFY nameChanged)
    Q_PROPERTY(bool recipientLabelsPending READ recipientLabelsPending NOTIFY recipientLabelsChanged)
    Q_PROPERTY(QString recipientLabelsError READ recipientLabelsError NOTIFY recipientLabelsChanged)
    Q_PROPERTY(QString recipientLabelsTransactionId READ recipientLabelsTransactionId NOTIFY recipientLabelsChanged)
    Q_PROPERTY(qint64 estimatedFeeSatoshi READ estimatedFeeSatoshi NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(QString estimatedFeeRate READ estimatedFeeRate NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(int estimatedInputCount READ estimatedInputCount NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(qint64 sendTotalSatoshi READ sendTotalSatoshi NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(qint64 availableSendBalanceSatoshi READ availableSendBalanceSatoshi NOTIFY balanceChanged)
    Q_PROPERTY(QObject* maximumRecipient READ maximumRecipient NOTIFY maximumRecipientChanged)
    Q_PROPERTY(QString name MEMBER m_name NOTIFY nameChanged)
    Q_PROPERTY(QString rpcName READ rpcName NOTIFY nameChanged)
    Q_PROPERTY(QString displayName MEMBER m_name NOTIFY nameChanged)
    Q_PROPERTY(bool available MEMBER m_available NOTIFY availableChanged)
    Q_PROPERTY(bool capabilitiesKnown MEMBER m_capabilities_known NOTIFY walletInfoChanged)
    Q_PROPERTY(int ensureReceivingAddressCalls MEMBER m_ensure_receiving_calls NOTIFY walletInfoChanged)
    Q_PROPERTY(QString closeQuestionToken MEMBER m_close_question_token NOTIFY closeQuestionChanged)
    Q_PROPERTY(QString balance MEMBER m_balance NOTIFY balanceChanged)
    Q_PROPERTY(bool balanceKnown MEMBER m_balance_known NOTIFY balanceChanged)
    Q_PROPERTY(QObject* transactionActivityModel READ transactionActivityModel CONSTANT)
    Q_PROPERTY(QObject* bumpModel READ bumpModel CONSTANT)
    Q_PROPERTY(MockSignVerifyMessageModel* signVerifyMessageModel READ signVerifyMessageModel CONSTANT)
    Q_PROPERTY(QObject* recipients READ recipients CONSTANT)
    Q_PROPERTY(QObject* coinsListModel READ coinsListModel CONSTANT)
    Q_PROPERTY(QObject* currentTransaction READ currentTransaction NOTIFY currentTransactionChanged)
    Q_PROPERTY(QObject* currentTransactionFlowDetails READ currentTransactionFlowDetails CONSTANT)
    Q_PROPERTY(quint64 currentTransactionFlowRevision READ currentTransactionFlowRevision NOTIFY currentTransactionChanged)
    Q_PROPERTY(QVariantMap currentTransactionFlow READ currentTransactionFlow WRITE setCurrentTransactionFlow NOTIFY currentTransactionChanged)
    Q_PROPERTY(bool currentTransactionIsImportedPsbt MEMBER m_current_transaction_is_imported_psbt NOTIFY currentTransactionChanged)
    Q_PROPERTY(QString importedReviewToken MEMBER m_imported_review_token NOTIFY currentTransactionChanged)
    Q_PROPERTY(bool currentTransactionCanSend MEMBER m_current_transaction_can_send NOTIFY currentTransactionChanged)
    Q_PROPERTY(bool currentTransactionCanSignOnly MEMBER m_current_transaction_can_sign_only NOTIFY currentTransactionChanged)
    Q_PROPERTY(bool currentTransactionNeedsRevalidation MEMBER m_current_transaction_needs_revalidation NOTIFY preparationChanged)
    Q_PROPERTY(bool transactionFeePending MEMBER m_transaction_fee_pending NOTIFY currentTransactionChanged)
    Q_PROPERTY(bool currentTransactionCanBroadcast MEMBER m_current_transaction_can_broadcast NOTIFY currentTransactionChanged)
    Q_PROPERTY(QString currentTransactionReviewMessage MEMBER m_current_transaction_review_message NOTIFY currentTransactionChanged)
    Q_PROPERTY(QObject* currentPaymentRequest READ currentPaymentRequest CONSTANT)
    Q_PROPERTY(QObject* receivingAddress READ receivingAddress CONSTANT)
    Q_PROPERTY(bool ensureReceivingAddressResult MEMBER m_ensure_receiving_address_result)
    Q_PROPERTY(QObject* detailPaymentRequest READ detailPaymentRequest CONSTANT)
    Q_PROPERTY(QObject* receiveRequests READ receiveRequests CONSTANT)
    Q_PROPERTY(bool receiveRequestReconciliationPending MEMBER m_receive_request_reconciliation_pending NOTIFY receiveRequestReconciliationPendingChanged)
    Q_PROPERTY(QString receiveRequestReconciliationError MEMBER m_receive_reconciliation_error NOTIFY receiveRequestReconciliationPendingChanged)
    Q_PROPERTY(int receiveRequestReloadCalls MEMBER m_receive_reload_calls NOTIFY receiveRequestReconciliationPendingChanged)
    Q_PROPERTY(MockAddressListModel* addressListModel READ addressListModel CONSTANT)
    Q_PROPERTY(bool hasExternalSigner MEMBER m_has_external_signer NOTIFY walletInfoChanged)
    Q_PROPERTY(int displayUnit MEMBER m_display_unit NOTIFY displayUnitChanged)
    Q_PROPERTY(int targetBlocks READ targetBlocks WRITE setTargetBlocks NOTIFY targetBlocksChanged)
    Q_PROPERTY(QString estimatedFee READ estimatedFee NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(bool customFeeEnabled READ customFeeEnabled WRITE setCustomFeeEnabled NOTIFY customFeeEnabledChanged)
    Q_PROPERTY(QString customFeeRate READ customFeeRate WRITE setCustomFeeRate NOTIFY customFeeRateChanged)
    Q_PROPERTY(bool customFeeRateValid READ customFeeRateValid NOTIFY customFeeRateValidChanged)
    Q_PROPERTY(double minimumCustomFeeRate READ minimumCustomFeeRate WRITE setMinimumCustomFeeRate NOTIFY customFeeRateValidChanged)
    Q_PROPERTY(bool feeEstimatePending MEMBER m_fee_estimate_pending NOTIFY feeEstimatePendingChanged)
    Q_PROPERTY(int feeEstimateRevision MEMBER m_fee_estimate_revision NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(bool sendAmountExhaustsBalance READ sendAmountExhaustsBalance WRITE setSendAmountExhaustsBalance NOTIFY sendAmountExhaustsBalanceChanged)
    Q_PROPERTY(bool prepareTransactionResult MEMBER m_prepare_transaction_result NOTIFY prepareTransactionResultChanged)
    Q_PROPERTY(bool sendTransactionResult MEMBER m_send_transaction_result NOTIFY sendTransactionResultChanged)
    Q_PROPERTY(int prepareTransactionCalls READ prepareTransactionCalls NOTIFY prepareTransactionCallsChanged)
    Q_PROPERTY(int preparationState MEMBER m_preparation_state NOTIFY preparationChanged)
    Q_PROPERTY(quint64 preparationId MEMBER m_preparation_ticket NOTIFY preparationChanged)
    Q_PROPERTY(bool preparationBusy READ preparationBusy NOTIFY preparationChanged)
    Q_PROPERTY(bool customFeeTargetPending READ customFeeTargetPending CONSTANT)
    Q_PROPERTY(bool coinSelectionPending READ coinSelectionPending CONSTANT)
    Q_PROPERTY(bool coinSelectionBlocked READ coinSelectionBlocked NOTIFY coinSelectionChanged)
    Q_PROPERTY(bool holdPreparationForTest MEMBER m_hold_preparation)
    Q_PROPERTY(int scheduleFeeEstimatesCalls READ scheduleFeeEstimatesCalls NOTIFY scheduleFeeEstimatesCallsChanged)
    Q_PROPERTY(int sendTransactionCalls READ sendTransactionCalls NOTIFY sendTransactionCallsChanged)
    Q_PROPERTY(int signOnlyTransactionCalls READ signOnlyTransactionCalls NOTIFY sendOperationChanged)
    Q_PROPERTY(int sendState READ sendState NOTIFY sendOperationChanged)
    Q_PROPERTY(bool sendSignOnly MEMBER m_send_sign_only NOTIFY sendOperationChanged)
    Q_PROPERTY(quint64 sendOperationId MEMBER m_send_operation_id NOTIFY sendOperationChanged)
    Q_PROPERTY(int signerApprovalState MEMBER m_approval_state NOTIFY signerApprovalChanged)
    Q_PROPERTY(QString signerUnverifiedResultToken MEMBER m_unverified_signer_token NOTIFY signerApprovalChanged)
    Q_PROPERTY(QString signerApprovalError MEMBER m_approval_error NOTIFY signerApprovalChanged)
    Q_PROPERTY(bool signerApprovalSignerNotFound MEMBER m_approval_signer_not_found NOTIFY signerApprovalChanged)
    Q_PROPERTY(bool sendBusy READ sendBusy NOTIFY sendOperationChanged)
    Q_PROPERTY(QString sendResultTxid READ sendResultTxid NOTIFY sendOperationChanged)
    Q_PROPERTY(int sendSubmission MEMBER m_send_submission NOTIFY sendOperationChanged)
    Q_PROPERTY(int sendRecordState MEMBER m_send_record_state NOTIFY sendOperationChanged)
    Q_PROPERTY(bool holdSendForTest MEMBER m_hold_send)
    Q_PROPERTY(int sendTransactionWithPassphraseCalls MEMBER m_send_transaction_with_passphrase_calls NOTIFY sendTransactionCallsChanged)
    Q_PROPERTY(bool sendDraftSweepsWallet MEMBER m_send_draft_sweeps_wallet NOTIFY feeEstimateRevisionChanged)
    Q_PROPERTY(bool currentTransactionSweepsWallet MEMBER m_current_transaction_sweeps_wallet NOTIFY currentTransactionChanged)
    Q_PROPERTY(int useMaximumCalls MEMBER m_use_maximum_calls NOTIFY walletInfoChanged)
    Q_PROPERTY(int broadcastCurrentTransactionCalls READ broadcastCurrentTransactionCalls NOTIFY broadcastCurrentTransactionCallsChanged)
    Q_PROPERTY(QString lastSavedPsbtPath MEMBER m_last_saved_psbt_path NOTIFY lastSavedPsbtPathChanged)
    Q_PROPERTY(PsbtActionState psbtActionState READ psbtActionState NOTIFY psbtActionChanged)
    Q_PROPERTY(quint64 psbtActionId READ psbtActionId NOTIFY psbtActionChanged)
    Q_PROPERTY(bool psbtActionBusy READ psbtActionBusy NOTIFY psbtActionChanged)
    Q_PROPERTY(PsbtImportResult psbtImportResult READ psbtImportResult NOTIFY psbtActionChanged)
    Q_PROPERTY(QString psbtActionError READ psbtActionError NOTIFY psbtActionChanged)
    Q_PROPERTY(QString psbtSavedPath READ psbtSavedPath NOTIFY psbtActionChanged)
    Q_PROPERTY(QString psbtSubmittedTxid READ psbtSubmittedTxid NOTIFY psbtActionChanged)
    Q_PROPERTY(QObject* importedPsbt READ importedPsbt CONSTANT)
    Q_PROPERTY(bool holdPsbtPublicationForTest MEMBER m_hold_psbt_publication)
    Q_PROPERTY(bool failNextPsbtSaveForTest MEMBER m_fail_next_psbt_save_for_test)
    Q_PROPERTY(PsbtImportResult psbtImportResultForTest MEMBER m_next_psbt_import_result)
    Q_PROPERTY(int discardCurrentTransactionCalls READ discardCurrentTransactionCalls NOTIFY discardCurrentTransactionCallsChanged)
    Q_PROPERTY(bool isEncrypted MEMBER m_is_encrypted NOTIFY securityStateChanged)
    Q_PROPERTY(bool isLocked MEMBER m_is_locked NOTIFY securityStateChanged)
    Q_PROPERTY(QString keyScheme MEMBER m_key_scheme NOTIFY walletInfoChanged)
    Q_PROPERTY(int keySchemeKind MEMBER m_key_scheme_kind NOTIFY walletInfoChanged)
    Q_PROPERTY(QString privateKeysStatus MEMBER m_private_keys_status NOTIFY walletInfoChanged)
    Q_PROPERTY(QString externalSignerStatus MEMBER m_external_signer_status NOTIFY walletInfoChanged)
    Q_PROPERTY(QVariantList receiveAddressTypes READ availableReceiveAddressTypes NOTIFY walletInfoChanged)
    Q_PROPERTY(bool canManagePassphrase MEMBER m_can_manage_passphrase NOTIFY walletInfoChanged)
    Q_PROPERTY(QString settingsError MEMBER m_settings_error NOTIFY settingsErrorChanged)
    Q_PROPERTY(MockWalletSecurity* security READ security CONSTANT)
    Q_PROPERTY(WalletReceiveModel* receive READ receive NOTIFY receiveChanged)
    Q_PROPERTY(int securityState MEMBER m_security_state NOTIFY securityOperationChanged)
    Q_PROPERTY(bool securityBusy READ securityBusy NOTIFY securityOperationChanged)
    Q_PROPERTY(QString securityError MEMBER m_settings_error NOTIFY securityOperationChanged)
    Q_PROPERTY(MockBackupFileOperation* backupOperation MEMBER m_backup_operation NOTIFY backupWalletCallsChanged)
    Q_PROPERTY(MockFileExportOperation* fileExportOperation MEMBER m_file_export_operation NOTIFY psbtActionChanged)
    Q_PROPERTY(QString lastBackupPath READ lastBackupPath NOTIFY backupWalletCallsChanged)
    Q_PROPERTY(int backupWalletCalls READ backupWalletCalls NOTIFY backupWalletCallsChanged)
    Q_PROPERTY(QString transactionError MEMBER m_transaction_error NOTIFY transactionErrorChanged)
    Q_PROPERTY(bool transactionNeedsUnlock MEMBER m_transaction_needs_unlock NOTIFY transactionNeedsUnlockChanged)
    Q_PROPERTY(QString lastCommitAddressType MEMBER m_last_commit_address_type NOTIFY lastCommitAddressTypeChanged)
    Q_PROPERTY(QString lastLoadedPaymentRequestId MEMBER m_last_loaded_payment_request_id NOTIFY lastLoadedPaymentRequestIdChanged)
    Q_PROPERTY(QString lastLoadedPaymentRequestDetailId MEMBER m_last_loaded_payment_request_detail_id NOTIFY lastLoadedPaymentRequestDetailIdChanged)
    Q_PROPERTY(QString lastTemplateRequestId MEMBER m_last_template_request_id NOTIFY lastTemplateRequestIdChanged)
    Q_PROPERTY(QString lastRemovedRequestId MEMBER m_last_removed_request_id NOTIFY lastRemovedRequestIdChanged)
    Q_PROPERTY(bool removeReceiveRequestResult MEMBER m_remove_receive_request_result NOTIFY removeReceiveRequestResultChanged)

    Q_PROPERTY(bool receiveRequestsLoaded MEMBER m_requests_loaded NOTIFY receiveRequestStorageChanged)
    Q_PROPERTY(bool receiveRequestsBusy MEMBER m_requests_busy NOTIFY receiveRequestStorageChanged)
    Q_PROPERTY(QString receiveRequestStorageError MEMBER m_requests_error NOTIFY receiveRequestStorageChanged)
    Q_PROPERTY(bool receivingAddressReady MEMBER m_receiving_ready NOTIFY receivingAddressStateChanged)
    Q_PROPERTY(bool receivingAddressPending MEMBER m_receiving_pending NOTIFY receivingAddressStateChanged)
    Q_PROPERTY(bool ensureReceivingAddressNeedsUnlock MEMBER m_ensure_needs_unlock)
public:
    Q_INVOKABLE quint64 requestRecheckCurrentTransaction() { ++m_recheck_calls; return quint64(m_recheck_calls); }
    Q_INVOKABLE int recheckTransactionCalls() const { return m_recheck_calls; }
    int m_recheck_calls{0};
    QString backupFileName() const { auto value = m_name.isEmpty() ? QStringLiteral("wallet") : m_name; value.replace(QLatin1Char{'/'}, QLatin1Char{'_'}); value.replace(QLatin1Char{'\\'}, QLatin1Char{'_'}); return value + QStringLiteral(".bak"); }

    QString m_rpc_namespace;
    QString rpcName() const { return m_rpc_namespace.isNull() ? m_name : m_rpc_namespace; }
    Q_INVOKABLE void setRpcNamespace(const QString& value) { m_rpc_namespace = value; Q_EMIT nameChanged(); }
    Q_INVOKABLE void clearRpcNamespace() { m_rpc_namespace = {}; Q_EMIT nameChanged(); }

    FeeSelectionModel* fees() const { return m_fees; }
    explicit MockWalletQmlModel(QObject* parent = nullptr) : QObject(parent), m_fees(new FeeSelectionModel(this))
    {
        connect(this, &MockWalletQmlModel::currentTransactionChanged, this, [this] {
            ++m_flow_revision;
            m_flow_details.setSource(m_current_transaction_flow, m_current_transaction ? m_current_transaction->property("reviewedRecipients").toList() : QVariantList{}, m_current_transaction, m_flow_revision);
        });
        MockFeeOwners().emplace_back(m_fees);
        m_fees->setServices(MockFeeServices());
        m_fees->observe(100, {{2, 2500}, {3, 2500}, {4, 2500}, {6, 2500}, {10, 2500}, {25, 2500}, {50, 2500}});
        connect(m_fees, &FeeSelectionModel::policyChanged, this, [this] {
            const bool mode_changed = m_custom_fee_enabled != (m_fees->mode() == FeeSelectionModel::Manual);
            const bool target_changed = m_target_blocks != m_fees->targetBlocks();
            const bool rate_changed = m_custom_fee_rate != m_fees->customRate()->text();
            m_custom_fee_enabled = m_fees->mode() == FeeSelectionModel::Manual;
            m_target_blocks = m_fees->targetBlocks(); m_custom_fee_rate = m_fees->customRate()->text();
            if (mode_changed) Q_EMIT customFeeEnabledChanged();
            if (target_changed) Q_EMIT targetBlocksChanged();
            if (rate_changed) Q_EMIT customFeeRateChanged();
            Q_EMIT customFeeRateValidChanged();
            if (m_fees->policyPending()) {
                m_custom_fee_estimate.clear(); ++m_fee_estimate_revision;
                Q_EMIT feeEstimateRevisionChanged(); return;
            }
            FeeReadResult result;
            result.estimates.insert(2, SendFeePreview{750, 1, 2500, {}, false, feeestimate::Provenance::Estimated, 2});
            result.estimates.insert(6, SendFeePreview{500, 1, 2500, {}, false, feeestimate::Provenance::Estimated, 6});
            result.estimates.insert(10, SendFeePreview{250, 1, 2500, {}, false, feeestimate::Provenance::Estimated, 10});
            // Preserve the existing fixture's fixed 400-sat Native outcome;
            // the actual fee-selection domain owns validity and rate intent.
            if (m_fees->ratePerKvB()) result.custom = SendFeePreview{400, 1, *m_fees->ratePerKvB(), {}, false, feeestimate::Provenance::Manual, 0};
            m_custom_fee_estimate = m_fees->ratePerKvB() ? QStringLiteral("0.00000400 BTC") : QString{};
            ++m_fee_estimate_revision;
            m_fees->publishEstimate(feeestimate::Phase::Current, std::move(result), true);
            Q_EMIT feeEstimateRevisionChanged();
        });
    }

    WalletReceiveModel* receive() const { return m_receive; }
    Q_INVOKABLE void setReceive(WalletReceiveModel* value) { if (m_receive != value) { m_receive = value; Q_EMIT receiveChanged(); } }
    QObject* currentTransactionFlowDetails() { return &m_flow_details; }
    quint64 currentTransactionFlowRevision() const { return m_flow_revision; }

Q_SIGNALS:
    void receiveChanged();
public:
    QPointer<WalletReceiveModel> m_receive;

    QVariantMap currentTransactionFlow() const { return {{"presentation", PrepareTransactionFlowPresentation(m_current_transaction_flow)}}; }
    void setCurrentTransactionFlow(const QVariantMap& flow) { m_current_transaction_flow = flow; Q_EMIT currentTransactionChanged(); }

    bool recipientLabelsPending() const { return false; }
    QString recipientLabelsError() const { return m_recipient_labels_error; }
    QString recipientLabelsTransactionId() const { return {}; }
    Q_INVOKABLE void setRecipientLabelsErrorForTest(const QString& error) { m_recipient_labels_error = error; Q_EMIT recipientLabelsChanged(); }
    Q_INVOKABLE void setCurrentTransactionNeedsRevalidationForTest(bool value)
    {
        if (m_current_transaction_needs_revalidation == value) return;
        m_current_transaction_needs_revalidation = value;
        Q_EMIT preparationChanged();
    }

    bool m_receiving_ready{true}, m_receiving_pending{false}, m_ensure_needs_unlock{false};
    quint64 m_address_operation_sequence{0};

    bool m_requests_loaded{true}, m_requests_busy{false};
    QString m_requests_error;

    enum SendSubmission { SubmissionUnknown, SubmissionMempool, SubmissionConfirmed, SubmissionNotInMempool, SubmissionConflicted, SubmissionAbandoned, SubmissionAccepted };
    Q_ENUM(SendSubmission)
    enum SendRecord { SendRecordUnknown, SendRecordAbsentAtObservation, SendRecordRecorded };
    Q_ENUM(SendRecord)
    int m_send_submission{SubmissionMempool};
    int m_send_record_state{2};
    enum SendState { SendIdle, SendPending, SendNeedsConfirmation, SendNeedsUnlock, SendFailed, SendCommitted, SendUncertain, SendClosed, SendSignedOnly, SendPartiallySigned };
    enum PreparationState { PreparationIdle, PreparationPending, PreparationNeedsUnlock, PreparationReady, PreparationFailed, PreparationStale, PreparationClosed, PreparationUncertain };
    Q_ENUM(PreparationState)
    int m_preparation_state{PreparationIdle};
    bool m_hold_preparation{false}, m_pending_preparation_success{false};
    quint64 m_preparation_ticket{0};
    bool preparationBusy() const { return m_preparation_state == PreparationPending; }
    bool customFeeTargetPending() const { return false; }
    bool coinSelectionPending() const { return false; }
    bool coinSelectionBlocked() const
    {
        auto* coins = qobject_cast<MockCoinsListModel*>(m_coins_list_model);
        return coins && (coins->property("selectionReconciliationRequired").toBool() ||
            (coins->manualInputSelection() && coins->selectedCoinsCount() == 0));
    }
    Q_INVOKABLE quint64 requestPrepareTransaction()
    {
        if (preparationBusy() || psbtActionBusy()) return 0;
        m_pending_preparation_success = prepareTransaction();
        m_preparation_state = PreparationPending;
        const auto ticket = ++m_preparation_ticket;
        Q_EMIT preparationChanged();
        if (!m_hold_preparation) QTimer::singleShot(0, this, [this, ticket] {
            if (ticket == m_preparation_ticket) finishPreparationForTest();
        });
        return ticket;
    }
    Q_INVOKABLE quint64 requestPrepareTransactionWithPassphrase(const QString&) { return requestPrepareTransaction(); }
    Q_INVOKABLE void finishPreparationForTest()
    {
        if (!preparationBusy()) return;
        m_preparation_state = m_pending_preparation_success ? PreparationReady
            : m_transaction_needs_unlock ? PreparationNeedsUnlock : PreparationFailed;
        Q_EMIT preparationChanged();
    }
    Q_INVOKABLE void finishPreparationStaleForTest()
    {
        if (!preparationBusy()) return;
        m_preparation_state = PreparationStale;
        Q_EMIT preparationChanged();
    }
    enum ApprovalState { ApprovalInitial, ApprovalWaiting, ApprovalSigned, ApprovalPartiallySigned, ApprovalFailed, ApprovalClosed, ApprovalOtherReview, ApprovalUncertain, ApprovalUnverified };
    Q_ENUM(ApprovalState)
    int m_approval_state{ApprovalInitial};
    QString m_approval_error;
    QString m_unverified_signer_token;
    bool m_approval_signer_not_found{false};
    Q_INVOKABLE void approveExternalSignerTransaction()
    {
        if (m_approval_state == ApprovalWaiting) return;
        m_approval_state = ApprovalWaiting;
        m_approval_error.clear();
        Q_EMIT signerApprovalChanged();
    }
    Q_ENUM(SendState)
    int m_send_state{SendIdle};
    quint64 m_send_operation_id{0};
    bool m_hold_send{false}, m_send_sign_only{false};
    int m_sign_only_transaction_calls{0};
    QPointer<QObject> m_send_review;
    int sendState() const { return m_send_state; }
    bool sendBusy() const { return m_send_state == SendPending || m_send_state == SendNeedsConfirmation; }
    QString sendResultTxid() const { return m_current_transaction ? m_current_transaction->property("txid").toString() : QString{}; }
    Q_INVOKABLE void resetSendForTest() { resetPsbtForTest();m_hold_send = false; m_send_sign_only = false; m_send_transaction_calls = 0; m_sign_only_transaction_calls = 0; m_send_state = SendIdle; m_send_submission = SubmissionMempool; m_send_review.clear(); Q_EMIT sendOperationChanged(); }
    Q_INVOKABLE void finishSendForTest() {
        if (!sendBusy()) return;
        m_send_state = m_send_review != m_current_transaction ? SendClosed : m_send_sign_only ? SendSignedOnly : m_send_transaction_result ? SendCommitted : m_transaction_needs_unlock ? SendNeedsUnlock : SendFailed;
        Q_EMIT sendOperationChanged();
    }
    Q_INVOKABLE quint64 requestSendTransaction() {
        if (sendBusy() || psbtActionBusy()) return 0;
        ++m_send_operation_id; m_send_review = m_current_transaction;
        if (m_current_transaction_sweeps_wallet) m_send_state = SendNeedsConfirmation;
        else { sendTransaction(); m_send_state = SendPending; if (!m_hold_send) QTimer::singleShot(0, this, [this] { finishSendForTest(); }); }
        Q_EMIT sendOperationChanged();
        return m_send_operation_id;
    }
    Q_INVOKABLE quint64 requestSignOnlyTransaction() {
        if (!m_current_transaction_can_sign_only || sendBusy() || psbtActionBusy()) return 0;
        ++m_sign_only_transaction_calls; ++m_send_operation_id; m_send_review = m_current_transaction;
        m_send_sign_only = true;
        m_send_state = m_transaction_needs_unlock ? SendNeedsUnlock : SendSignedOnly;
        Q_EMIT sendOperationChanged();
        return m_send_operation_id;
    }
    Q_INVOKABLE quint64 confirmSendTransaction(quint64 request_id) {
        if (request_id != m_send_operation_id || m_send_state != SendNeedsConfirmation) return 0;
        sendTransaction(); m_send_state = SendPending; Q_EMIT sendOperationChanged();
        if (!m_hold_send) QTimer::singleShot(0, this, [this] { finishSendForTest(); });
        return request_id;
    }
    Q_INVOKABLE quint64 cancelSendTransaction(quint64 request_id) {
        if (request_id != m_send_operation_id || (m_send_state != SendNeedsConfirmation && m_send_state != SendNeedsUnlock)) return 0;
        m_send_state = SendFailed; Q_EMIT sendOperationChanged(); return request_id;
    }
    Q_INVOKABLE quint64 requestSendTransactionWithPassphrase(const QString& passphrase) {
        if (psbtActionBusy()) return 0;
        if (m_send_state != SendNeedsUnlock) {
            if (sendBusy()) return 0;
            ++m_send_operation_id; m_send_review = m_current_transaction;
        }
        sendTransactionWithPassphrase(passphrase); m_send_state = SendPending; Q_EMIT sendOperationChanged();
        if (!m_hold_send) QTimer::singleShot(0, this, [this] { finishSendForTest(); });
        return m_send_operation_id;
    }
    enum SecurityState { SecurityIdle, SecurityPending, SecuritySucceeded, SecurityFailed, SecurityUncertain };
    Q_ENUM(SecurityState)
    enum class KeyScheme {
        SingleKey = 0,
        WatchOnly,
        MultiKey,
        ExternalSigner,
    };
    Q_ENUM(KeyScheme)
    enum class PsbtImportResult {
        PsbtUnsupported = 0,
        WalletCanSign,
        WalletCannotSign,
        TransactionAlreadyKnown,
        ReviewPending,
    };
    Q_ENUM(PsbtImportResult)
    enum class PsbtActionState { PsbtIdle, PsbtPending, PsbtImported, PsbtKnown, PsbtSaved, PsbtSubmitted, PsbtFailed, PsbtUncertain, PsbtStale, PsbtClosed };
    Q_ENUM(PsbtActionState)
    enum class PsbtKind { Import,Save,Broadcast };
    struct PsbtPacket {
        PsbtKind kind;quint64 ticket{0},review_revision{0};QPointer<QObject> review;
        QString path,txid,error;PsbtImportResult imported{PsbtImportResult::PsbtUnsupported};
        PsbtActionState terminal{PsbtActionState::PsbtFailed};bool allowed{false},ready{false},saved{false},submitted{false};
    };
    PsbtActionState psbtActionState()const{return m_psbt_action_state;}
    quint64 psbtActionId()const{return m_psbt_action_id;}
    bool psbtActionBusy()const{return m_psbt_action_state==PsbtActionState::PsbtPending;}
    PsbtImportResult psbtImportResult()const{return m_psbt_import_result;}
    QString psbtActionError()const{return m_psbt_action_error;}
    QString psbtSavedPath()const{return m_saved_psbt_receipt&&m_saved_psbt_receipt->saved?m_saved_psbt_receipt->path:QString{};}
    QString psbtSubmittedTxid()const{return m_submitted_psbt_receipt&&m_submitted_psbt_receipt->submitted?m_submitted_psbt_receipt->txid:QString{};}
    QObject* importedPsbt(){return &m_imported_psbt;}
    Q_INVOKABLE void adoptImportReviewForTest(quint64 owner, quint64 child) {
        m_imported_review_token = QString::number(owner) + QLatin1Char(':') + QString::number(child);
        m_current_transaction_is_imported_psbt = true;
        Q_EMIT currentTransactionChanged();
    }
    Q_INVOKABLE quint64 requestImportPsbtFromFile(const QString& path){return requestPsbtAction(PsbtKind::Import,path);}
    Q_INVOKABLE quint64 requestSaveCurrentTransactionAsPsbt(const QString& path){return requestPsbtAction(PsbtKind::Save,path);}
    Q_INVOKABLE quint64 requestSaveUnverifiedSignerResult(const QString& token, const QString& path) {
        return !token.isEmpty() && token == m_unverified_signer_token ? requestPsbtAction(PsbtKind::Save, path) : 0;
    }
    Q_INVOKABLE quint64 requestBroadcastCurrentTransaction(){return requestPsbtAction(PsbtKind::Broadcast,{});}
    Q_INVOKABLE void completePsbtWorkForTest(){if(m_psbt_operation)completePsbtPacket(m_psbt_operation);}
    Q_INVOKABLE void finishPsbtPublicationForTest(){if(m_psbt_operation&&m_psbt_operation->ready)publishPsbtPacket(m_psbt_operation);}
    Q_INVOKABLE void resetPsbtForTest(){
        m_hold_psbt_publication=false;m_psbt_operation.reset();m_saved_psbt_receipt.reset();m_submitted_psbt_receipt.reset();
        m_psbt_action_state=PsbtActionState::PsbtIdle;m_psbt_import_result=PsbtImportResult::PsbtUnsupported;m_next_psbt_import_result=PsbtImportResult::WalletCanSign;
        m_psbt_action_error.clear();m_imported_review_token.clear();m_imported_psbt.clear();Q_EMIT psbtActionChanged();
    }
    static void completePsbtPacket(const std::shared_ptr<PsbtPacket>& packet){
        if(packet->ready)return;
        packet->ready=true;
        if(!packet->allowed){packet->terminal=PsbtActionState::PsbtFailed;packet->error=QStringLiteral("The original PSBT action could not be completed.");return;}
        if(packet->kind==PsbtKind::Save){packet->saved=true;packet->terminal=PsbtActionState::PsbtSaved;}
        else if(packet->kind==PsbtKind::Broadcast){packet->submitted=true;packet->terminal=PsbtActionState::PsbtSubmitted;}
        else if(packet->imported==PsbtImportResult::TransactionAlreadyKnown)packet->terminal=PsbtActionState::PsbtKnown;
        else if(packet->imported==PsbtImportResult::PsbtUnsupported){packet->terminal=PsbtActionState::PsbtFailed;packet->error=QStringLiteral("This PSBT is not supported yet.");}
        else packet->terminal=PsbtActionState::PsbtImported;
    }
    quint64 requestPsbtAction(PsbtKind kind,const QString& path){
        if(!m_available||sendBusy()||preparationBusy()||(psbtActionBusy()&&!(kind==PsbtKind::Import&&m_psbt_operation&&m_psbt_operation->kind==PsbtKind::Import)))return 0;
        const QPointer<MockWalletQmlModel> observer{this};
        auto packet=std::make_shared<PsbtPacket>();packet->kind=kind;packet->ticket=++m_psbt_action_id;packet->review_revision=m_review_revision;
        packet->review=m_current_transaction;packet->path=path;packet->txid=kind==PsbtKind::Import&&!m_imported_psbt.matchedTxid().isEmpty()?m_imported_psbt.matchedTxid():m_current_transaction?m_current_transaction->property("txid").toString():QString{};
        packet->imported=kind==PsbtKind::Import?m_next_psbt_import_result:PsbtImportResult::PsbtUnsupported;
        const bool reject_save = kind == PsbtKind::Save && std::exchange(m_fail_next_psbt_save_for_test, false);
        packet->allowed=!reject_save&&(kind==PsbtKind::Import?!path.isEmpty():m_current_transaction&&(kind==PsbtKind::Save?!path.isEmpty():m_current_transaction_can_broadcast&&!m_transaction_fee_pending));
        m_psbt_operation=packet;m_psbt_action_state=PsbtActionState::PsbtPending;m_psbt_action_error.clear();m_psbt_import_result=PsbtImportResult::PsbtUnsupported;
        if(kind==PsbtKind::Save){m_saved_psbt_receipt=packet;m_last_saved_psbt_path=path;}
        if(kind==PsbtKind::Broadcast)m_submitted_psbt_receipt=packet;
        // The first turn records the actual original outcome independently of
        // any view. A separate turn publishes only into the matching owner.
        QTimer::singleShot(0,QCoreApplication::instance(),[packet,observer]{
            completePsbtPacket(packet);
            if(observer&&packet->kind==PsbtKind::Broadcast){++observer->m_broadcast_current_transaction_calls;if(observer->m_available)Q_EMIT observer->broadcastCurrentTransactionCallsChanged();}
            QTimer::singleShot(0,QCoreApplication::instance(),[packet,observer]{if(observer&&!observer->m_hold_psbt_publication)observer->publishPsbtPacket(packet);});
        });
        if(kind==PsbtKind::Save){Q_EMIT lastSavedPsbtPathChanged();if(!observer)return packet->ticket;}
        if(m_available&&m_psbt_operation==packet)Q_EMIT psbtActionChanged();
        return packet->ticket;
    }
    void publishPsbtPacket(const std::shared_ptr<PsbtPacket>& packet){
        if(m_psbt_operation!=packet||!packet->ready)return;
        if(!m_available){m_psbt_action_state=PsbtActionState::PsbtClosed;return;}
        const bool same=packet->review_revision==m_review_revision&&(packet->kind==PsbtKind::Import||packet->review==m_current_transaction);
        m_psbt_action_state=same?packet->terminal:PsbtActionState::PsbtStale;
        m_psbt_action_error=same?packet->error:QStringLiteral("The transaction review changed before publication.");
        m_psbt_import_result=packet->imported;const QPointer<MockWalletQmlModel> observer{this};
        m_imported_psbt.setError(m_psbt_action_error);if(!observer||!m_available||m_psbt_operation!=packet)return;
        if(same&&packet->terminal==PsbtActionState::PsbtKnown){m_imported_psbt.setMatchedTxidForTest(packet->txid);if(!observer||!m_available||m_psbt_operation!=packet)return;}
        if(same&&packet->kind==PsbtKind::Import&&packet->terminal==PsbtActionState::PsbtImported){
            ++m_review_revision;m_current_transaction_is_imported_psbt=true;m_current_transaction_can_send=packet->imported==PsbtImportResult::WalletCanSign;
            m_transaction_fee_pending=packet->imported==PsbtImportResult::ReviewPending;m_current_transaction_can_broadcast=false;Q_EMIT currentTransactionChanged();
        }else if(same&&packet->submitted){m_current_transaction_can_send=false;m_current_transaction_can_broadcast=false;Q_EMIT currentTransactionChanged();}
        if(observer&&m_available&&m_psbt_operation==packet)Q_EMIT psbtActionChanged();
    }



    int m_security_state{SecurityIdle};
    QString m_name{QStringLiteral("testwallet")};
    QString m_balance{QStringLiteral("1.00000000 BTC")};
    bool m_balance_known{true};
    bool m_available{true};
    Q_INVOKABLE void closeBeforeAvailabilityNotificationForTest() { m_available = false; }
    Q_INVOKABLE void checkBeforeReconciliationNotificationForTest() { m_receive_request_reconciliation_pending = true; }
    Q_INVOKABLE MockImageExportOperation* savePaymentRequestImage(MockPaymentRequest*, const QVariant&, const QUrl& path) {
        auto* operation = new MockImageExportOperation(this); operation->m_path = path; return operation;
    }
    QString m_close_question_token;
    quint64 m_close_sequence{0};
    QObject* m_transaction_activity_model{nullptr};
    QObject* m_bump_model{nullptr};
    MockSignVerifyMessageModel* m_sign_verify_model{nullptr};
    QObject* m_recipients{nullptr};
    QObject* m_maximum_recipient{nullptr};
    QMetaObject::Connection m_maximum_amount_connection;
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
    bool m_remove_receive_request_result{true};
    bool m_receive_request_reconciliation_pending{false};
    QString m_receive_reconciliation_error;
    int m_receive_reload_calls{0};
    Q_INVOKABLE void reloadReceiveRequests() { ++m_receive_reload_calls; Q_EMIT receiveRequestReconciliationPendingChanged(); }
    QString m_saved_payment_request_label;
    QString m_saved_payment_request_message;
    QString m_saved_payment_request_note_self;
    qint64 m_saved_payment_request_amount_sat{0};
    QString m_saved_payment_request_address_type;
    QString m_saved_payment_request_address;
    int m_target_blocks{2};
    bool m_prepare_transaction_result{true};
    bool m_current_transaction_can_send{true};
    bool m_current_transaction_can_sign_only{false};
    bool m_current_transaction_needs_revalidation{false};
    QVariantMap m_current_transaction_flow;
    bool m_current_transaction_is_imported_psbt{false};
    QString m_imported_review_token;
    bool m_current_transaction_can_broadcast{false};
    bool m_transaction_fee_pending{false};
    QString m_current_transaction_review_message;
    QString m_last_saved_psbt_path;

    QObject* transactionActivityModel() const { return m_transaction_activity_model; }
    QObject* bumpModel() const { return m_bump_model; }
    MockSignVerifyMessageModel* signVerifyMessageModel() const { return m_sign_verify_model; }
    QObject* recipients() const { return m_recipients; }
    QObject* coinsListModel() const { return m_coins_list_model; }
    QObject* currentTransaction() const { return m_current_transaction; }
    QObject* currentPaymentRequest() const { return m_current_payment_request; }
    MockPaymentRequest m_receiving_address;
    QObject* receivingAddress() { return &m_receiving_address; }
    int m_receiving_sequence{0}, m_ensure_receiving_calls{0};
    bool m_capabilities_known{true};
    bool m_ensure_receiving_address_result{true};
    Q_PROPERTY(int setReceiveAddressCalls READ setReceiveAddressCalls NOTIFY receiveSelectionFixtureChanged)
    Q_PROPERTY(QString lastSelectedReceiveAddress READ lastSelectedReceiveAddress NOTIFY receiveSelectionFixtureChanged)
    int setReceiveAddressCalls() const { return m_set_receive_address_calls; }
    QString lastSelectedReceiveAddress() const { return m_selected_receive_address; }
    Q_INVOKABLE void controlReceiveSelection(bool admitted) {
        m_receive_selection_admitted = admitted; m_set_receive_address_calls = 0;
        m_selected_receive_address.clear(); m_selection_operation = nullptr;
        Q_EMIT receiveSelectionFixtureChanged();
    }
    Q_INVOKABLE MockReceiveRequestOperation* setCurrentPaymentRequestAddress(const QString& address) {
        ++m_set_receive_address_calls; m_selected_receive_address = address;
        Q_EMIT receiveSelectionFixtureChanged();
        if (!m_available || !m_receive_selection_admitted || address.isEmpty()) return nullptr;
        auto* operation = new MockReceiveRequestOperation(this);
        operation->m_pending = true; operation->m_token = QString::number(++m_address_operation_sequence);
        m_selection_operation = operation;
        return operation;
    }
    Q_INVOKABLE MockReceiveRequestOperation* selectedReceiveOperation() const { return m_selection_operation; }
    Q_INVOKABLE void publishSelectedReceiveAddress() {
        if (!m_selection_operation) return;
        auto* request = qobject_cast<MockPaymentRequest*>(m_current_payment_request);
        if (!request) return;
        request->m_address = m_selected_receive_address; Q_EMIT request->addressChanged();
        m_receiving_address.m_address = m_selected_receive_address; Q_EMIT m_receiving_address.addressChanged();
        m_receiving_ready = true; Q_EMIT receivingAddressStateChanged();
    }
    Q_INVOKABLE void publishSelectedReceiveRequest(const QString& id) {
        auto* request = qobject_cast<MockPaymentRequest*>(m_current_payment_request);
        if (!request) return;
        request->m_id = id; Q_EMIT request->idChanged();
        publishSelectedReceiveAddress();
    }
    Q_INVOKABLE void deliverReceiveSelection(bool address_ready, bool request_loaded, const QString& request_id, const QString& error) {
        if (!m_selection_operation) return;
        m_selection_operation->m_address_ready = address_ready;
        m_selection_operation->m_loaded = request_loaded; m_selection_operation->m_id = request_id;
        m_selection_operation->m_error = error; m_selection_operation->m_pending = false;
        Q_EMIT m_selection_operation->changed();
    }
    bool m_receive_selection_admitted{true}; int m_set_receive_address_calls{0};
    QString m_selected_receive_address; QPointer<MockReceiveRequestOperation> m_selection_operation;
    Q_INVOKABLE MockReceiveRequestOperation* ensureReceivingAddress(bool next = false, const QString& type = {})
    {
        ++m_ensure_receiving_calls;
        if (!m_ensure_receiving_address_result) return nullptr;
        const QString selected = type.isEmpty() ? m_default_receive_address_type : type;
        auto* operation = new MockReceiveRequestOperation(this);
        operation->m_token = QString::number(++m_address_operation_sequence);
        operation->m_view_revision = m_receiving_address.m_revision;
        operation->m_next = next; operation->m_type = selected;
        if (m_ensure_needs_unlock) {
            m_receiving_ready = false; Q_EMIT receivingAddressStateChanged();
            operation->m_error_code = MockReceiveRequestOperation::NeedsUnlock;
            return operation;
        }
        if (next || m_receiving_address.m_address.isEmpty() || m_receiving_address.m_payment_received || selected != m_receiving_address.m_address_type) {
            m_receiving_address.clear();
            m_receiving_address.m_address = QStringLiteral("bcrt1qreceivingaddress%1").arg(++m_receiving_sequence);
            m_receiving_address.m_address_type = selected;
            m_default_receive_address_type = selected;
            Q_EMIT m_receiving_address.addressChanged();
            Q_EMIT m_receiving_address.addressTypeChanged();
        }
        m_receiving_ready = true; Q_EMIT receivingAddressStateChanged();
        operation->m_address_ready = true; operation->m_view_revision = m_receiving_address.m_revision;
        return operation;
    }
    Q_INVOKABLE MockReceiveRequestOperation* resumeReceivingAddress(MockReceiveRequestOperation* original, const QString&)
    {
        if (!original) return nullptr;
        m_ensure_needs_unlock = false;
        return ensureReceivingAddress(original->m_next, original->m_type);
    }
    Q_INVOKABLE MockReceiveRequestOperation* retryReceivingAddress(MockReceiveRequestOperation* original)
    { return original ? ensureReceivingAddress(original->m_next, original->m_type) : nullptr; }
    Q_INVOKABLE QString receiveAddressTypeLabel(const QString& type) const { return type; }
    Q_INVOKABLE MockReceiveRequestOperation* pendingReceiveOperation(const QString& id)
    {
        auto* operation = new MockReceiveRequestOperation(this);
        operation->m_id = id; operation->m_pending = true; return operation;
    }
    Q_INVOKABLE MockReceiveRequestOperation* commitReceivingPaymentRequest()
    {
        auto* request = qobject_cast<MockPaymentRequest*>(m_current_payment_request);
        if (!request || m_receiving_address.m_payment_received || m_receiving_address.m_address.isEmpty()) return nullptr;
        request->m_address = m_receiving_address.m_address;
        request->m_address_type = m_receiving_address.m_address_type;
        if (!commitPaymentRequest()) return nullptr;
        m_receiving_address.clear();
        auto* operation = new MockReceiveRequestOperation(this);
        operation->m_saved = true; operation->m_id = request->m_id;
        return operation;
    }
    MockPaymentRequest m_detail_payment_request;
    QObject* detailPaymentRequest() { return &m_detail_payment_request; }
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
            QVariantMap{
                {QStringLiteral("id"), QStringLiteral("legacy")},
                {QStringLiteral("label"), QStringLiteral("Base58 (Legacy)")},
                {QStringLiteral("description"), QStringLiteral("Older wallet compatibility")},
            },
        };
    }
    Q_INVOKABLE QString defaultReceiveAddressType() const { return m_default_receive_address_type; }
    qint64 estimatedFeeSatoshi() const { return m_custom_fee_enabled ? 600 : m_target_blocks == 2 ? 750 : m_target_blocks == 6 ? 500 : 250; }
    QString estimatedFeeRate() const { return m_custom_fee_enabled ? m_custom_fee_rate : QStringLiteral("2.5"); }
    int estimatedInputCount() const { return 1; }
    qint64 sendTotalSatoshi() const { const auto* r = qobject_cast<MockRecipientsModel*>(m_recipients); return r ? r->totalAmountSatoshi() + estimatedFeeSatoshi() : 0; }
    qint64 availableSendBalanceSatoshi() const { return 1000000; }
    QObject* maximumRecipient() const { return m_maximum_recipient; }
    Q_INVOKABLE void clearMaximum()
    {
        if (!m_maximum_recipient) return;
        QObject::disconnect(m_maximum_amount_connection);
        if (auto* recipient = qobject_cast<MockSendRecipient*>(m_maximum_recipient)) recipient->setAmountMode(MockSendRecipient::Fixed);
        m_maximum_recipient = nullptr;
        Q_EMIT maximumRecipientChanged();
    }
    Q_INVOKABLE void clearSelectedCoins() { clearMaximum(); if (auto* c = qobject_cast<MockCoinsListModel*>(m_coins_list_model)) c->reset(); }
    bool m_send_draft_sweeps_wallet{false};
    bool m_current_transaction_sweeps_wallet{false};
    int m_use_maximum_calls{0};
    Q_INVOKABLE void useMaximum()
    {
        auto* recipients = qobject_cast<MockRecipientsModel*>(m_recipients);
        useMaximumFor(recipients ? recipients->current() : nullptr);
    }
    Q_INVOKABLE void useMaximumFor(QObject* value)
    {
        ++m_use_maximum_calls;
        auto* recipient = qobject_cast<MockSendRecipient*>(value);
        if (recipient && recipient != m_maximum_recipient) {
            clearMaximum();
            m_maximum_recipient = recipient; recipient->setAmountMode(MockSendRecipient::Maximum);
            m_maximum_amount_connection = connect(&recipient->m_amount, &BitcoinAmount::valueEdited, this, [this] { clearMaximum(); });
            Q_EMIT maximumRecipientChanged();
        }
        Q_EMIT walletInfoChanged();
    }
    Q_INVOKABLE void setCustomFeeTarget(int target) { m_fees->chooseCustomTarget(target); }
    int targetBlocks() const { return m_fees->targetBlocks(); }
    QString estimatedFee() const
    {
        return m_custom_fee_enabled ? m_custom_fee_estimate : estimatedFeeForTarget(m_target_blocks);
    }
    bool customFeeEnabled() const { return m_fees->mode() == FeeSelectionModel::Manual; }
    QString customFeeRate() const { return m_fees->customRate()->text(); }
    bool customFeeRateValid() const { return m_fees->ratePerKvB().has_value(); }
    double minimumCustomFeeRate() const { return m_minimum_custom_fee_rate; }
    void setMinimumCustomFeeRate(double minimum) {
        if (minimum == m_minimum_custom_fee_rate) return;
        m_minimum_custom_fee_rate = minimum;
        m_fees->observe(static_cast<CAmount>(minimum * 1000), {{2, 2500}, {3, 2500}, {4, 2500}, {6, 2500}, {10, 2500}, {25, 2500}, {50, 2500}});
        Q_EMIT customFeeRateValidChanged();
    }
    double m_minimum_custom_fee_rate{0.1};
    int prepareTransactionCalls() const { return m_prepare_transaction_calls; }
    int scheduleFeeEstimatesCalls() const { return m_schedule_fee_estimates_calls; }
    int sendTransactionCalls() const { return m_send_transaction_calls; }
    int signOnlyTransactionCalls() const { return m_sign_only_transaction_calls; }
    int broadcastCurrentTransactionCalls() const { return m_broadcast_current_transaction_calls; }
    int discardCurrentTransactionCalls() const { return m_discard_current_transaction_calls; }
    bool sendAmountExhaustsBalance() const { return m_send_amount_exhausts_balance; }
    QString lastBackupPath() const { return m_last_backup_path; }
    int backupWalletCalls() const { return m_backup_wallet_calls; }
    Q_INVOKABLE QString estimatedFeeForTarget(const int target) const
    {
        QString estimate = m_fee_estimates.value(target);
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
    void setTransactionActivityModel(QObject* model) { m_transaction_activity_model = model; }
    void setBumpModel(QObject* model) { m_bump_model = model; }
    void setSignVerifyMessageModel(MockSignVerifyMessageModel* model) { m_sign_verify_model = model; }
    void setRecipients(QObject* model) { m_recipients = model; }
    void setCoinsListModel(QObject* model)
    {
        if (m_coins_list_model == model) return;
        if (m_coins_list_model) QObject::disconnect(m_coins_selection_connection);
        m_coins_list_model = model;
        if (auto* coins = qobject_cast<MockCoinsListModel*>(model)) {
            m_coins_selection_connection = connect(coins, &MockCoinsListModel::selectionSnapshotChanged, this, [this] {
                Q_EMIT coinSelectionChanged();
                scheduleFeeEstimates();
            });
        }
    }
    void setCurrentTransaction(QObject* transaction) {
        ++m_review_revision;QObject::disconnect(m_review_rows_connection); m_current_transaction = transaction;
        if (transaction) m_review_rows_connection = connect(transaction, SIGNAL(reviewedRecipientsChanged()), this, SIGNAL(currentTransactionChanged()));
        Q_EMIT currentTransactionChanged();
    }
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
        auto* request = &m_detail_payment_request;
        request->clear();
        if (!request) return false;
        request->m_id = request_id;
        request->m_label = request_id == "1" ? m_saved_payment_request_label : QStringLiteral("Alice");
        request->m_message = request_id == "1" ? m_saved_payment_request_message : QStringLiteral("Coffee");
        request->m_note_self = request_id == "1" ? m_saved_payment_request_note_self : QStringLiteral("Counter");
        request->m_address = request_id == "1" ? m_saved_payment_request_address : QStringLiteral("bcrt1qrequestaddress0000000000000000000000");
        request->m_created_iso = QStringLiteral("2026-01-02T00:00:00Z");
        request->m_is_editing = false;
        request->m_amount.setUnit(MockBitcoinAmount::BTC);
        request->m_amount.setSatoshi(request_id == "1" ? m_saved_payment_request_amount_sat : 100'000);
        request->m_address_type = m_saved_payment_request_address_type;
        Q_EMIT request->idChanged();
        Q_EMIT request->labelChanged();
        Q_EMIT request->messageChanged();
        Q_EMIT request->noteSelfChanged();
        Q_EMIT request->addressChanged();
        Q_EMIT request->createdIsoChanged();
        Q_EMIT request->isEditingChanged();
        Q_EMIT request->m_amount.displayChanged();
        Q_EMIT request->m_amount.unitChanged();
        Q_EMIT request->m_amount.amountChanged();
        return true;
    }
    void setTargetBlocks(const int value)
    { m_fees->captureTarget(static_cast<unsigned int>(value)); }
    void setCustomFeeEnabled(const bool value)
    { m_fees->captureMode(value); }
    void setCustomFeeRate(const QString& value)
    { m_fees->editRate(value); }
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
                m_coins_list_model && (m_coins_list_model->property("manualInputSelection").toBool() ||
                    m_coins_list_model->property("selectedCoinsCount").toInt() > 0)};
            setTransactionStatus(selected_inputs_active
                ? QStringLiteral("Selected inputs do not cover the amount plus fee")
                : QStringLiteral("Amount plus fee exceeds available balance"), false);
        }
        return m_prepare_transaction_result;
    }
    MockWalletSecurity* security() { return &m_security; }
    bool securityBusy() const { return m_security_state == SecurityPending; }
    Q_INVOKABLE bool encryptWallet(const QString&) { return startSecurityForTest(); }
    Q_INVOKABLE bool changeWalletPassphrase(const QString&, const QString&) { return startSecurityForTest(); }
    bool startSecurityForTest()
    {
        if (securityBusy() || !m_available) return false;
        m_security_state = SecurityPending;
        Q_EMIT securityOperationChanged();
        return true;
    }
    Q_INVOKABLE void finishSecurityForTest(bool success)
    {
        if (!securityBusy()) return;
        m_security_state = success ? SecuritySucceeded : SecurityFailed;
        m_settings_error = success ? QString{} : QStringLiteral("Security operation failed");
        m_security.finish(success);
        Q_EMIT settingsErrorChanged();
        Q_EMIT securityOperationChanged();
    }
    Q_INVOKABLE void requestClose()
    {
        if (!m_available) return;
        m_close_question_token = m_name + QStringLiteral(":%1").arg(++m_close_sequence);
        Q_EMIT closeQuestionChanged();
    }
    Q_INVOKABLE void answerClose(const QString& token, bool accept)
    {
        if (!m_available || token.isEmpty() || token != m_close_question_token) return;
        m_close_question_token.clear();
        Q_EMIT closeQuestionChanged();
        if (accept) Q_EMIT closeConfirmed();
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
    Q_INVOKABLE void resetFeePolicyForTest()
    {
        m_fees->choosePreset(2); m_fees->chooseCustomTarget(6); m_fees->choosePreset(6);
    }
    Q_INVOKABLE void publishFeeResultForTest(int source, const QString& error)
    {
        FeeReadResult result;
        if (error.isEmpty()) {
            for (unsigned int target : {2U, 6U, 10U}) result.estimates.insert(target,
                SendFeePreview{500, 1, 2500, {}, false, static_cast<feeestimate::Provenance>(source), source == int(feeestimate::Provenance::Estimated) ? int(target) : 0});
        } else { result.error = feeestimate::Error::Backend; result.message = error; }
        m_fees->publishEstimate(error.isEmpty() ? feeestimate::Phase::Current : feeestimate::Phase::Failed, std::move(result), error.isEmpty());
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
    Q_INVOKABLE bool sendTransactionWithPassphrase(const QString&)
    {
        ++m_send_transaction_with_passphrase_calls;
        Q_EMIT sendTransactionCallsChanged();
        return m_send_transaction_result;
    }
    /** Stand in for a new review or PSBT import replacing the transaction. */
    Q_INVOKABLE void replaceCurrentTransactionForTest(QObject* transaction)
    {
        setCurrentTransaction(transaction);
    }
    Q_INVOKABLE bool broadcastCurrentTransaction()
    {
        ++m_broadcast_current_transaction_calls;
        Q_EMIT broadcastCurrentTransactionCallsChanged();
        return m_current_transaction_can_broadcast;
    }
    Q_INVOKABLE QString saveCurrentTransactionAsPsbt(const QString& path)
    {
        m_last_saved_psbt_path = path;
        Q_EMIT lastSavedPsbtPathChanged();
        return {};
    }
    Q_INVOKABLE void discardCurrentTransaction()
    {
        ++m_review_revision;++m_discard_current_transaction_calls;
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
        m_saved_payment_request_amount_sat = request->m_amount.satoshi();
        m_saved_payment_request_address_type = request->m_address_type;
        request->m_id = QStringLiteral("1");
        if (request->m_address.isEmpty()) request->m_address = QStringLiteral("bcrt1qrequestaddress0000000000000000000000");
        m_saved_payment_request_address = request->m_address;
        request->m_is_editing = false;
        Q_EMIT request->idChanged();
        Q_EMIT request->addressChanged();
        Q_EMIT request->isEditingChanged();
        return true;
    }
    Q_INVOKABLE MockReceiveRequestOperation* updatePaymentRequest(const QString& id, qint64 amount, const QString& label, const QString& message, const QString& note)
    {
        auto* request = &m_detail_payment_request;
        if (request->m_id != id) return nullptr;
        if (request->m_payment_received && (amount != request->m_amount.satoshi() || label != request->m_label || message != request->m_message)) return nullptr;
        request->m_amount.setSatoshi(amount);
        request->setProperty("label", label);
        request->setProperty("message", message);
        request->setProperty("noteSelf", note);
        auto* operation = new MockReceiveRequestOperation(this); operation->m_saved = true; operation->m_id = id;
        return operation;
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
    Q_INVOKABLE void prepareBackupConfirmationTestState(const QString& path, bool replacing)
    {
        auto* operation = new MockBackupFileOperation(this);
        operation->m_phase = MockBackupFileOperation::AwaitingConfirmation;
        operation->m_pending = operation->m_confirmation = true;
        operation->m_replacing = replacing;
        operation->m_path = path;
        if (m_backup_operation) m_backup_operation->deleteLater();
        m_backup_operation = operation;
        Q_EMIT backupWalletCallsChanged();
    }
    Q_INVOKABLE void resetWalletSettingsTestState()
    {
        m_security_state = SecurityIdle;
        m_security.reset();
        m_available = true;
        Q_EMIT securityOperationChanged();
        Q_EMIT availableChanged();
        if (m_backup_operation) m_backup_operation->deleteLater();
        m_backup_operation = nullptr;
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
        m_receiving_address.clear();
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
        request->m_amount.setSatoshi(m_saved_payment_request_amount_sat);
        Q_EMIT request->idChanged();
        Q_EMIT request->labelChanged();
        Q_EMIT request->messageChanged();
        Q_EMIT request->noteSelfChanged();
        Q_EMIT request->addressChanged();
        Q_EMIT request->addressTypeChanged();
        Q_EMIT request->isEditingChanged();
        Q_EMIT request->m_amount.displayChanged();
    }
    Q_INVOKABLE MockReceiveRequestOperation* removeReceiveRequest(const QString& request_id)
    {
        if (!m_remove_receive_request_result) return nullptr;
        m_last_removed_request_id = request_id;
        Q_EMIT lastRemovedRequestIdChanged();
        auto* operation = new MockReceiveRequestOperation(this); operation->m_erased = true; operation->m_id = request_id;
        return operation;
    }

Q_SIGNALS:
    void psbtActionChanged();
    void recipientLabelsChanged();
    void receiveSelectionFixtureChanged();
    void receivingAddressStateChanged();
    void receiveRequestStorageChanged();
    void securityOperationChanged();
    void closeQuestionChanged();
    void closeConfirmed();
    void availableChanged();
    void nameChanged();
    void balanceChanged();
    void targetBlocksChanged();
    void customFeeEnabledChanged();
    void customFeeRateChanged();
    void customFeeRateValidChanged();
    void feeEstimatePendingChanged();
    void feeEstimateRevisionChanged();
    void maximumRecipientChanged();
    void sendAmountExhaustsBalanceChanged();
    void prepareTransactionResultChanged();
    void sendTransactionResultChanged();
    void prepareTransactionCallsChanged();
    void preparationChanged();
    void scheduleFeeEstimatesCallsChanged();
    void coinSelectionChanged();
    void sendTransactionCallsChanged();
    void sendOperationChanged();
    void broadcastCurrentTransactionCallsChanged();
    void discardCurrentTransactionCallsChanged();
    void currentTransactionChanged();
    void lastSavedPsbtPathChanged();
    void externalSignerApprovalSucceeded();
    void signerApprovalChanged();
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
    void removeReceiveRequestResultChanged();
    void receiveRequestReconciliationPendingChanged();

private:
    FeeSelectionModel* m_fees;
    MockWalletSecurity m_security{this, [this] { return m_available && !securityBusy(); }, [this] { return startSecurityForTest(); }};
    MockPsbtDocument m_imported_psbt{this};
    PsbtActionState m_psbt_action_state{PsbtActionState::PsbtIdle};
    PsbtImportResult m_psbt_import_result{PsbtImportResult::PsbtUnsupported},m_next_psbt_import_result{PsbtImportResult::WalletCanSign};
    quint64 m_psbt_action_id{0},m_review_revision{0};bool m_hold_psbt_publication{false},m_fail_next_psbt_save_for_test{false};QString m_psbt_action_error;
    std::shared_ptr<PsbtPacket> m_psbt_operation,m_saved_psbt_receipt,m_submitted_psbt_receipt;
    MockFlowDetails m_flow_details{this};
    quint64 m_flow_revision{0};
    QMetaObject::Connection m_review_rows_connection;
    QMetaObject::Connection m_coins_selection_connection;

    QString m_recipient_labels_error;
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

    QHash<int, QString> m_fee_estimates{{1, QStringLiteral("0.00000750 BTC")}, {2, QStringLiteral("0.00000500 BTC")}, {6, QStringLiteral("0.00000250 BTC")}};
    bool m_custom_fee_enabled{false};
    QString m_custom_fee_rate;
    QString m_custom_fee_estimate;
    bool m_fee_estimate_pending{false};
    bool m_send_amount_exhausts_balance{false};
    bool m_send_transaction_result{true};
    bool m_is_encrypted{false};
    bool m_is_locked{false};
    QString m_key_scheme{QStringLiteral("Descriptor")};
    int m_key_scheme_kind{0};
    QString m_private_keys_status{QStringLiteral("Enabled")};
    QString m_external_signer_status{QStringLiteral("Not used")};
    bool m_can_manage_passphrase{true};
    QString m_settings_error;
    MockBackupFileOperation* m_backup_operation{nullptr};
    MockFileExportOperation* m_file_export_operation{nullptr};
    QString m_last_backup_path;
    int m_backup_wallet_calls{0};
    QString m_transaction_error;
    bool m_transaction_needs_unlock{false};
    int m_fee_estimate_revision{1};
    int m_prepare_transaction_calls{0};
    int m_schedule_fee_estimates_calls{0};
    int m_send_transaction_calls{0};
    int m_send_transaction_with_passphrase_calls{0};
    int m_broadcast_current_transaction_calls{0};
    int m_discard_current_transaction_calls{0};
    MockAddressListModel m_address_list_model;
};

class MockWalletQmlModelTransaction : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString nodePolicyStatus MEMBER m_node_policy_status CONSTANT)
    Q_PROPERTY(QString address MEMBER m_address CONSTANT)
    Q_PROPERTY(QString label MEMBER m_label CONSTANT)
    Q_PROPERTY(QString amount MEMBER m_amount CONSTANT)
    Q_PROPERTY(QString fee MEMBER m_fee CONSTANT)
    Q_PROPERTY(QString total MEMBER m_total CONSTANT)
    Q_PROPERTY(QString txid MEMBER m_txid CONSTANT)
    Q_PROPERTY(QVariantList reviewedRecipients MEMBER m_reviewed_recipients NOTIFY reviewedRecipientsChanged)
    Q_PROPERTY(int reviewTargetBlocks MEMBER m_review_target_blocks NOTIFY reviewedRecipientsChanged)
    Q_PROPERTY(QString reviewFeeRate MEMBER m_review_fee_rate NOTIFY reviewedRecipientsChanged)
    Q_PROPERTY(int reviewFeeSource MEMBER m_review_fee_source NOTIFY reviewedRecipientsChanged)
    Q_PROPERTY(bool reviewUsesFallback MEMBER m_review_fallback NOTIFY reviewedRecipientsChanged)
    Q_PROPERTY(QObject* amountAmount READ amountAmount CONSTANT)
    Q_PROPERTY(QObject* feeAmount READ feeAmount CONSTANT)
    Q_PROPERTY(QObject* totalAmount READ totalAmount CONSTANT)

public:
    QString m_node_policy_status;
    Q_INVOKABLE void setNodePolicyStatusForTest(const QString& value) { m_node_policy_status = value; }
    MockWalletQmlModelTransaction()
    {
        m_amount_amount.setDisplay(QStringLiteral("0.01000000"));
        m_fee_amount.setDisplay(QStringLiteral("0.00001000"));
        m_total_amount.setDisplay(QStringLiteral("0.01001000"));
    }

    QString m_address{QStringLiteral("bcrt1qexampleaddress")};
    QString m_label{QStringLiteral("example-label")};
    QString m_amount{QStringLiteral("0.01000000 BTC")};
    QString m_fee{QStringLiteral("0.00001000 BTC")};
    QString m_total{QStringLiteral("0.01001000 BTC")};
    QString m_txid{QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")};
    QVariantList m_reviewed_recipients;
    int m_review_target_blocks{6};
    QString m_review_fee_rate{QStringLiteral("2.5")};
    int m_review_fee_source{1};
    bool m_review_fallback{false};
    MockBitcoinAmount m_amount_amount;
    MockBitcoinAmount m_fee_amount;
    MockBitcoinAmount m_total_amount;

    QObject* amountAmount() { return &m_amount_amount; }
    QObject* feeAmount() { return &m_fee_amount; }
    QObject* totalAmount() { return &m_total_amount; }

Q_SIGNALS:
    void reviewedRecipientsChanged();
};

class MockBitcoinUri;

/** Fixture for PaymentImportOperation: the fixture parser's outcome applied
 * to the mock recipient. Native ownership and exact receipts are exercised
 * with the real classes in the C++ unit and integration tests. */
class MockPaymentImportOperation : public ImportOperation
{
    Q_OBJECT
    Q_PROPERTY(quint64 requestId MEMBER m_request_id CONSTANT)
    Q_PROPERTY(MockPaymentImportOperation::State state READ state NOTIFY changed)
    Q_PROPERTY(MockPaymentImportOperation::SourceKind sourceKind READ sourceKind NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool canApply MEMBER m_can_apply NOTIFY changed)
    Q_PROPERTY(bool canReview MEMBER m_can_review NOTIFY changed)
    Q_PROPERTY(quint64 promptToken READ promptToken NOTIFY changed)
    Q_PROPERTY(QString resultToken READ resultToken NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString address MEMBER m_address NOTIFY changed)
    Q_PROPERTY(QString publicLabel MEMBER m_label NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool hasAmount MEMBER m_has_amount NOTIFY changed)
    Q_PROPERTY(qint64 amount MEMBER m_amount NOTIFY changed)
    Q_PROPERTY(bool matchesExistingRequest READ matchesExistingRequest NOTIFY changed)
    Q_PROPERTY(QString selectedPath MEMBER m_selected_path NOTIFY changed)
    Q_PROPERTY(QObject* originalWallet READ originalWallet NOTIFY changed)

public:
    enum class State { Empty, Reading, Analyzing, Inspecting, AwaitingReplacement, Applying, CreatingReview, Reconciling, Applied, ReviewOpened, Failed, Closing, Closed };
    Q_ENUM(State)
    enum class SourceKind { UnknownSource, PaymentSource, PsbtSource };
    Q_ENUM(SourceKind)

    MockPaymentImportOperation(quint64 id, MockBitcoinUri* uri, MockWalletQmlModel* wallet, MockSendRecipient* recipient, QObject* owner)
        : ImportOperation(owner), m_request_id{id}, m_uri{uri}, m_wallet{wallet}, m_recipient{recipient},
          m_revision{wallet ? recipientsOf(wallet)->revision() : QString{}} {}

    State state() const { return m_state; }
    SourceKind sourceKind() const
    {
        return m_candidate.value("psbt").toBool() ? SourceKind::PsbtSource
            : m_candidate.value("success").toBool() ? SourceKind::PaymentSource : SourceKind::UnknownSource;
    }
    QObject* originalWallet() const { return m_wallet; }
    bool matchesExistingRequest() const
    {
        return m_recipient && m_recipient->m_address.address() == m_address
            && (!m_has_amount || (m_recipient->m_amount_mode == MockSendRecipient::Fixed && m_recipient->m_amount.satoshi() == m_amount))
            && m_recipient->m_request_label == m_label && m_recipient->m_message == m_message;
    }

    bool pending() const override { return m_pending; }
    bool applied() const override { return m_state == State::Applied; }
    bool reviewOpened() const override { return m_state == State::ReviewOpened; }
    bool offersPayment() const override
    {
        return m_state == State::Inspecting && sourceKind() == SourceKind::PaymentSource && m_can_apply && !matchesExistingRequest();
    }
    quint64 promptToken() const override { return m_prompt; }
    QString resultToken() const override { return m_result_id ? QString::number(m_request_id) + QLatin1Char(':') + QString::number(m_result_id) : QString{}; }
    QString error() const override { return m_error; }
    QString message() const override { return m_message; }

    void prepare(QVariantMap outcome, bool held)
    {
        m_candidate = std::move(outcome);
        if (!held) QMetaObject::invokeMethod(this, &MockPaymentImportOperation::retryPublication, Qt::QueuedConnection);
    }
    void startEmpty() { m_state = State::Empty; m_pending = false; m_input_started = false; }
    Q_INVOKABLE void retryPublication()
    {
        if (m_closed || m_ready) return;
        m_ready = true;
        m_pending = false;
        const bool success = m_candidate.value("success").toBool();
        const bool psbt = m_candidate.value("psbt").toBool();
        m_address = m_candidate.value("address").toString();
        m_label = m_candidate.value("label").toString();
        m_message = m_candidate.value("uriMessage").toString();
        m_has_amount = m_candidate.value("hasAmount").toBool();
        m_amount = m_candidate.value("amountSats").toLongLong();
        m_error = success ? QString{} : m_candidate.value("error").toString();
        m_state = success ? State::Inspecting : State::Failed;
        m_can_apply = success && !psbt && walletAvailable();
        m_can_review = psbt && walletAvailable();
        if (m_requested) use();
        Q_EMIT changed();
    }
    Q_INVOKABLE void chooseTarget(MockWalletQmlModel* wallet, MockSendRecipient* recipient)
    {
        if (m_closed) return;
        m_wallet = wallet;
        m_recipient = recipient;
        m_revision = wallet ? recipientsOf(wallet)->revision() : QString{};
        if (m_ready && !m_pending) {
            m_can_apply = m_candidate.value("success").toBool() && !m_candidate.value("psbt").toBool() && walletAvailable();
            m_can_review = m_candidate.value("psbt").toBool() && walletAvailable();
        }
    }
    Q_INVOKABLE void use() override
    {
        m_requested = true;
        if (!m_ready) return;
        if (m_can_apply) apply();
        else if (m_can_review) openReview();
    }
    Q_INVOKABLE bool requestFile(const QUrl& file) override;
    Q_INVOKABLE void apply()
    {
        if (m_closed || !m_can_apply || !walletAvailable() || !m_recipient) return;
        if (recipientsOf(m_wallet)->revision() != m_revision) { cancel(); return; }
        const int field = m_candidate.value("field", -1).toInt();
        const bool conflicts = (field != 0 && !m_recipient->m_address.address().isEmpty() && m_recipient->m_address.address() != m_address)
            || (m_has_amount && (m_recipient->m_amount_mode == MockSendRecipient::Maximum
                || (field != 1 && m_recipient->m_amount.satoshi() > 0 && m_recipient->m_amount.satoshi() != m_amount)));
        if (conflicts) {
            m_prompt = m_request_id;
            m_state = State::AwaitingReplacement;
            Q_EMIT changed();
            return;
        }
        applyNow();
    }
    Q_INVOKABLE void replace(quint64 prompt) override
    {
        if (m_closed || prompt == 0 || prompt != m_prompt || !walletAvailable()) return;
        m_prompt = 0;
        applyNow();
    }
    Q_INVOKABLE void keepDraft(quint64 prompt)
    {
        if (prompt != m_prompt) return;
        m_prompt = 0;
        m_state = State::Inspecting;
        Q_EMIT changed();
    }
    Q_INVOKABLE void openReview()
    {
        if (m_closed || !m_can_review || !walletAvailable() || recipientsOf(m_wallet)->revision() != m_revision) return;
        m_can_review = false;
        m_result_id = m_request_id;
        m_wallet->adoptImportReviewForTest(m_request_id, m_result_id);
        m_state = State::ReviewOpened;
        Q_EMIT changed();
    }
    Q_INVOKABLE void reconcile() {}
    Q_INVOKABLE void cancel() override
    {
        m_closed = true;
        m_pending = false;
        m_can_apply = false;
        m_prompt = 0;
        m_state = State::Closed;
        Q_EMIT changed();
    }

private:
    static MockRecipientsModel* recipientsOf(MockWalletQmlModel* wallet) { return qobject_cast<MockRecipientsModel*>(wallet->recipients()); }
    bool walletAvailable() const { return m_wallet && m_wallet->m_available; }
    void applyNow()
    {
        m_can_apply = false;
        m_state = State::Applying;
        auto fields = m_candidate;
        fields.insert(QStringLiteral("privateNote"), QString{});
        const QPointer<MockWalletQmlModel> wallet{m_wallet};
        auto* recipients = recipientsOf(wallet);
        recipients->setProperty("importPending", true);
        m_recipient->applyParsedPaymentRequest(fields, false);
        if (!m_closed && wallet) {
            if (m_has_amount) wallet->clearMaximum();
            wallet->scheduleFeeEstimates();
        }
        if (wallet) recipients->setProperty("importPending", false);
        if (m_closed) return;
        m_result_id = m_request_id;
        m_state = State::Applied;
        Q_EMIT changed();
    }

    const quint64 m_request_id;
    QPointer<MockBitcoinUri> m_uri;
    QPointer<MockWalletQmlModel> m_wallet;
    QPointer<MockSendRecipient> m_recipient;
    QString m_revision;
    QVariantMap m_candidate;
    State m_state{State::Reading};
    bool m_pending{true}, m_can_apply{false}, m_can_review{false}, m_has_amount{false};
    bool m_closed{false}, m_requested{false}, m_ready{false}, m_input_started{true};
    quint64 m_prompt{0}, m_result_id{0};
    QString m_error, m_address, m_label, m_message, m_selected_path;
    qint64 m_amount{0};
};

/** Fixture for the BitcoinUri singleton: a simplified parser that accepts
 * fixture addresses, and test controls to hold and release publications. */
class MockBitcoinUri : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool holdPublications MEMBER m_hold NOTIFY controlsChanged)
    Q_PROPERTY(bool admitRequestsForTest MEMBER m_admit NOTIFY controlsChanged)
    Q_PROPERTY(QString probeErrorForTest MEMBER m_probe_error NOTIFY controlsChanged)
    Q_PROPERTY(int requestCallsForTest MEMBER m_request_calls NOTIFY controlsChanged)

public:
    enum class InputField { AddressField, AmountField, PrivateNoteField };
    Q_ENUM(InputField)

    bool holdingPublications() const { return m_hold; }
    void hold(MockPaymentImportOperation* operation) { m_held.emplace_back(operation); }

    Q_INVOKABLE void resetRequestControlsForTest()
    {
        m_admit = true;
        m_probe_error.clear();
        m_request_calls = 0;
        Q_EMIT controlsChanged();
    }
    Q_INVOKABLE void releasePublications()
    {
        m_hold = false;
        Q_EMIT controlsChanged();
        for (const auto& operation : std::exchange(m_held, {})) if (operation) operation->retryPublication();
    }
    Q_INVOKABLE bool isPaymentUri(const QString& text) const { return text.trimmed().startsWith(QLatin1String{"bitcoin:"}, Qt::CaseInsensitive); }
    Q_INVOKABLE QString pastedPaymentUri(const QString& previous, const QString& current, const QString& clipboard) const
    {
        if (!isPaymentUri(clipboard) || previous == current || current.size() < clipboard.size()) return {};
        const auto unchanged = current.size() - clipboard.size();
        if (unchanged > previous.size()) return {};
        qsizetype prefix{0}, suffix{0};
        while (prefix < std::min(previous.size(), current.size()) && previous[prefix] == current[prefix]) ++prefix;
        while (suffix < std::min(previous.size(), current.size()) && previous[previous.size() - suffix - 1] == current[current.size() - suffix - 1]) ++suffix;
        const auto low = std::max<qsizetype>(0, unchanged - suffix), high = std::min(prefix, unchanged);
        const auto found = current.indexOf(clipboard, low);
        return low <= high && found >= 0 && found <= high ? clipboard.trimmed() : QString{};
    }
    Q_INVOKABLE MockPaymentImportOperation* requestImportText(const QString& text, MockWalletQmlModel* wallet, MockSendRecipient* recipient, QObject* owner)
    {
        return request(Parse(text), wallet, recipient, owner);
    }
    Q_INVOKABLE MockPaymentImportOperation* requestImportFile(const QString&, MockWalletQmlModel* wallet, MockSendRecipient* recipient, QObject* owner)
    {
        return request(Error(QStringLiteral("File parsing is unavailable in QML unit tests.")), wallet, recipient, owner);
    }
    Q_INVOKABLE MockPaymentImportOperation* requestImportDrop(int count, const QUrl& first_url, const QString& text, MockWalletQmlModel* wallet, MockSendRecipient* recipient, QObject* owner)
    {
        const auto value = count == 1 ? first_url.toString() : text;
        if (count > 1) return request(Error(QStringLiteral("Drop one payment request or PSBT at a time.")), wallet, recipient, owner);
        return count == 1 && value.startsWith(QLatin1String{"file:"}, Qt::CaseInsensitive)
            ? requestImportFile(value, wallet, recipient, owner) : requestImportText(value, wallet, recipient, owner);
    }
    Q_INVOKABLE MockPaymentImportOperation* requestImportPaste(const QString& text, int field, MockWalletQmlModel* wallet, MockSendRecipient* recipient, QObject* owner)
    {
        auto outcome = Parse(text);
        outcome.insert(QStringLiteral("field"), field);
        if (!m_probe_error.isEmpty()) { outcome.insert(QStringLiteral("error"), m_probe_error); outcome.insert(QStringLiteral("success"), false); }
        return request(std::move(outcome), wallet, recipient, owner);
    }
    Q_INVOKABLE MockPaymentImportOperation* captureImport(MockWalletQmlModel* wallet, MockSendRecipient* recipient, QObject* owner)
    {
        ++m_request_calls;
        Q_EMIT controlsChanged();
        if (!owner || !m_admit) return nullptr;
        auto* operation = new MockPaymentImportOperation(++m_sequence, this, wallet, recipient, owner);
        operation->startEmpty();
        return operation;
    }

Q_SIGNALS:
    void controlsChanged();

private:
    static QVariantMap Error(const QString& error) { return {{QStringLiteral("success"), false}, {QStringLiteral("error"), error}}; }
    static QVariantMap Parse(QString text)
    {
        text = text.trimmed();
        const auto invalid = QStringLiteral("Not a valid Bitcoin payment URI.");
        if (!text.startsWith(QLatin1String{"bitcoin:"}, Qt::CaseInsensitive)) return Error(invalid);
        if (text.startsWith(QLatin1String{"bitcoin://"}, Qt::CaseInsensitive)) return Error(QStringLiteral("'bitcoin://' is not a valid URI. Use 'bitcoin:' instead."));
        const auto payload = text.mid(8);
        const auto separator = payload.indexOf(QLatin1Char('?'));
        const auto address = separator < 0 ? payload : payload.left(separator);
        if (address.isEmpty()) return Error(invalid);
        QVariantMap result{{QStringLiteral("success"), true}, {QStringLiteral("error"), QString{}}, {QStringLiteral("address"), address},
            {QStringLiteral("amountSats"), qint64{0}}, {QStringLiteral("hasAmount"), false}, {QStringLiteral("label"), QString{}},
            {QStringLiteral("hasLabel"), false}, {QStringLiteral("uriMessage"), QString{}}, {QStringLiteral("hasMessage"), false}};
        for (const auto& item : (separator < 0 ? QString{} : payload.mid(separator + 1)).split(QLatin1Char('&'), Qt::SkipEmptyParts)) {
            const auto equals = item.indexOf(QLatin1Char('='));
            const auto key = QUrl::fromPercentEncoding(item.left(equals < 0 ? item.size() : equals).toUtf8());
            const auto value = equals < 0 ? QString{} : QUrl::fromPercentEncoding(item.mid(equals + 1).toUtf8());
            if (key == QLatin1String{"amount"}) {
                const auto pieces = value.split(QLatin1Char('.'));
                const auto fraction = (pieces.size() > 1 ? pieces[1] : QString{}).left(8).leftJustified(8, QLatin1Char('0'));
                result.insert(QStringLiteral("amountSats"), pieces[0].toLongLong() * 100'000'000 + fraction.toLongLong());
                result.insert(QStringLiteral("hasAmount"), true);
            } else if (key == QLatin1String{"label"}) {
                result.insert(QStringLiteral("label"), value);
                result.insert(QStringLiteral("hasLabel"), true);
            } else if (key == QLatin1String{"message"}) {
                result.insert(QStringLiteral("uriMessage"), value);
                result.insert(QStringLiteral("hasMessage"), true);
            }
        }
        return result;
    }
    MockPaymentImportOperation* request(QVariantMap outcome, MockWalletQmlModel* wallet, MockSendRecipient* recipient, QObject* owner)
    {
        ++m_request_calls;
        Q_EMIT controlsChanged();
        if (!owner || !m_admit) return nullptr;
        auto* operation = new MockPaymentImportOperation(++m_sequence, this, wallet, recipient, owner);
        operation->prepare(std::move(outcome), m_hold);
        if (m_hold) hold(operation);
        return operation;
    }

    bool m_hold{false}, m_admit{true};
    QString m_probe_error;
    int m_request_calls{0};
    quint64 m_sequence{0};
    std::vector<QPointer<MockPaymentImportOperation>> m_held;
};

bool MockPaymentImportOperation::requestFile(const QUrl& file)
{
    if (m_closed || m_input_started) return false;
    m_input_started = true;
    m_selected_path = file.toString();
    m_state = State::Reading;
    m_pending = true;
    const bool held = m_uri && m_uri->holdingPublications();
    prepare({{QStringLiteral("success"), true}, {QStringLiteral("psbt"), true}}, held);
    if (held) m_uri->hold(this);
    Q_EMIT changed();
    return true;
}

/** The wallet fixture's send draft as an ImportTarget, registered as
 * WalletImportTarget so SendCreate runs the real SendImportModel. */
class MockWalletImportTarget : public ImportTarget
{
    Q_OBJECT
    Q_PROPERTY(MockWalletQmlModel* wallet READ wallet WRITE setWallet NOTIFY walletChanged)
    Q_PROPERTY(MockBitcoinUri* uri READ uri WRITE setUri NOTIFY uriChanged)

public:
    using ImportTarget::ImportTarget;
    MockWalletQmlModel* wallet() const { return m_wallet; }
    void setWallet(MockWalletQmlModel* wallet)
    {
        if (m_wallet == wallet) return;
        if (m_wallet) { disconnect(m_wallet, nullptr, this, nullptr); if (recipients()) disconnect(recipients(), nullptr, this, nullptr); }
        m_wallet = wallet;
        if (wallet) {
            connect(wallet, &MockWalletQmlModel::availableChanged, this, &ImportTarget::availableChanged);
            connect(wallet, &MockWalletQmlModel::currentTransactionChanged, this, &ImportTarget::importedReviewChanged);
            if (auto* list = recipients()) {
                connect(list, &MockRecipientsModel::revisionChanged, this, &ImportTarget::draftRevisionChanged);
                connect(list, &MockRecipientsModel::validationChanged, this, &ImportTarget::importPendingChanged);
                connect(list, &MockRecipientsModel::currentRecipientChanged, this, [this] { watchRecipient(); Q_EMIT currentRecipientChanged(); });
            }
        }
        watchRecipient();
        Q_EMIT walletChanged();
        Q_EMIT currentRecipientChanged();
        Q_EMIT availableChanged();
    }
    MockBitcoinUri* uri() const { return m_uri; }
    void setUri(MockBitcoinUri* uri) { if (m_uri == uri) return; m_uri = uri; Q_EMIT uriChanged(); Q_EMIT availableChanged(); }

    bool available() const override { return m_wallet && m_uri && m_wallet->m_available; }
    QString draftRevision() const override { return recipients() ? recipients()->revision() : QString{}; }
    bool importPending() const override { return recipients() && recipients()->m_import_pending; }
    bool recipientAddressEmpty() const override { return !current() || current()->m_address.address().isEmpty(); }
    QString importedReviewToken() const override { return m_wallet ? m_wallet->m_imported_review_token : QString{}; }
    ImportOperation* importText(const QString& text, QObject* owner) override
    {
        return m_uri && m_wallet && current() ? m_uri->requestImportText(text, m_wallet, current(), owner) : nullptr;
    }
    ImportOperation* importPaste(const QString& text, int field, QObject* recipient, QObject* owner) override
    {
        auto* target = recipient ? qobject_cast<MockSendRecipient*>(recipient) : current();
        return m_uri && m_wallet && target ? m_uri->requestImportPaste(text, field, m_wallet, target, owner) : nullptr;
    }
    ImportOperation* importFile(const QString& path, QObject* owner) override
    {
        return m_uri && m_wallet ? m_uri->requestImportFile(path, m_wallet, current(), owner) : nullptr;
    }
    ImportOperation* importDrop(int url_count, const QUrl& first_url, const QString& text, QObject* owner) override
    {
        return m_uri && m_wallet ? m_uri->requestImportDrop(url_count, first_url, text, m_wallet, current(), owner) : nullptr;
    }
    ImportOperation* captureFile(QObject* owner) override
    {
        return m_uri && m_wallet ? m_uri->captureImport(m_wallet, current(), owner) : nullptr;
    }
    bool adopt(ImportOperation* operation) override
    {
        auto* original = qobject_cast<MockPaymentImportOperation*>(operation);
        if (!original || !current() || !available()) return false;
        original->chooseTarget(m_wallet, current());
        return true;
    }

Q_SIGNALS:
    void walletChanged();
    void uriChanged();

private:
    MockRecipientsModel* recipients() const { return m_wallet ? qobject_cast<MockRecipientsModel*>(m_wallet->recipients()) : nullptr; }
    MockSendRecipient* current() const { return recipients() ? qobject_cast<MockSendRecipient*>(recipients()->current()) : nullptr; }
    void watchRecipient()
    {
        for (const auto& connection : std::as_const(m_recipient_connections)) disconnect(connection);
        m_recipient_connections.clear();
        auto* recipient = current();
        if (!recipient) return;
        const auto edited = [this] { Q_EMIT recipientEdited(); };
        m_recipient_connections = {
            connect(&recipient->m_address, &MockBitcoinAddress::addressChanged, this, edited),
            connect(&recipient->m_amount, &BitcoinAmount::amountChanged, this, edited),
            connect(recipient, &MockSendRecipient::labelChanged, this, edited),
            connect(recipient, &MockSendRecipient::paymentRequestChanged, this, edited),
        };
    }

    QPointer<MockWalletQmlModel> m_wallet;
    QPointer<MockBitcoinUri> m_uri;
    QList<QMetaObject::Connection> m_recipient_connections;
};

class MockWalletProvisioningCheck : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY inputChanged)
    Q_PROPERTY(QString xpub READ xpub WRITE setXpub NOTIFY inputChanged)
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY inputChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(QString nameError READ nameError NOTIFY changed)
    Q_PROPERTY(bool xpubValid READ xpubValid NOTIFY changed)
    Q_PROPERTY(bool pathExists READ pathExists NOTIFY changed)
    Q_PROPERTY(QString normalizedPath READ path NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit MockWalletProvisioningCheck(QObject* parent = nullptr) : QObject(parent) { refresh(); }
    QString name() const { return m_name; }
    QString xpub() const { return m_xpub; }
    QString path() const { return m_path; }
    bool pending() const { return m_pending; }
    bool ready() const { return !m_pending && !m_closed; }
    QString nameError() const { return m_name.trimmed().isEmpty() ? QStringLiteral("Enter a wallet name.") : QString{}; }
    bool xpubValid() const { const auto key = m_xpub.trimmed(); return ready() && key.size() >= 100 && (key.startsWith("xpub") || key.startsWith("tpub")); }
    bool pathExists() const { return ready() && !m_path.isEmpty(); }
    QString error() const { return {}; }
    void setName(const QString& value) { if (m_name == value) return; m_name = value; refresh(); Q_EMIT inputChanged(); }
    void setXpub(const QString& value) { if (m_xpub == value) return; m_xpub = value; refresh(); Q_EMIT inputChanged(); }
    void setPath(const QString& value) { if (m_path == value) return; m_path = value; refresh(); Q_EMIT inputChanged(); }
    Q_INVOKABLE void closeAdmission() { m_closed = true; ++m_revision; }
    Q_INVOKABLE void refresh() {
        if (m_closed) return;
        const auto revision = ++m_revision; m_pending = true; Q_EMIT changed();
        QTimer::singleShot(0, this, [this, revision] { if (!m_closed && revision == m_revision) { m_pending = false; Q_EMIT changed(); } });
    }
Q_SIGNALS:
    void inputChanged();
    void changed();
private:
    QString m_name, m_xpub, m_path;
    quint64 m_revision{0};
    bool m_pending{false}, m_closed{false};
};

class MockOpenWalletListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    QVariant data(const QModelIndex& index, int role) const override {
        return index.isValid() && index.row() >= 0 && index.row() < m_rows.size() && role == Qt::UserRole + 1 ? QVariant::fromValue(m_rows[index.row()]) : QVariant{};
    }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole + 1, "wallet"}}; }
    int rowForWallet(QObject* wallet) const { return m_rows.indexOf(wallet); }
    void setWallets(const QList<QObject*>& wallets) {
        for (const auto& connection : m_connections) QObject::disconnect(connection);
        m_connections.clear(); m_wallets = wallets;
        for (auto* wallet : wallets) if (wallet) {
            m_connections.push_back(connect(wallet, SIGNAL(availableChanged()), this, SLOT(refresh())));
            m_connections.push_back(connect(wallet, &QObject::destroyed, this, [this, wallet] { m_wallets.removeAll(wallet); refresh(); }));
        }
        refresh();
    }
public Q_SLOTS:
    void refresh() {
        QList<QObject*> visible;
        for (auto* wallet : m_wallets) if (wallet && wallet->property("available").toBool()) visible.push_back(wallet);
        for (int row = m_rows.size() - 1; row >= 0; --row) if (!visible.contains(m_rows[row])) { beginRemoveRows({}, row, row); m_rows.removeAt(row); endRemoveRows(); }
        for (auto* wallet : visible) if (!m_rows.contains(wallet)) { const int row = m_rows.size(); beginInsertRows({}, row, row); m_rows.push_back(wallet); endInsertRows(); }
    }
private:
    QList<QObject*> m_wallets, m_rows;
    QList<QMetaObject::Connection> m_connections;
};

class MockWalletErrorLines : public QAbstractListModel
{
public:
    explicit MockWalletErrorLines(QObject* parent) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int((m_text.size() + 1023) / 1024); }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "content"}}; }
    QVariant data(const QModelIndex& index, int role) const override { return index.isValid() && role == Qt::UserRole ? m_text.mid(qsizetype(index.row()) * 1024, 1024) : QVariant{}; }
    void setText(QString text) { beginResetModel(); m_text = std::move(text); endResetModel(); }
private:
    QString m_text;
};

class MockWalletCreationText : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit MockWalletCreationText(QObject* parent) : QObject(parent) {}
    QString text() const { return m_text; }
    QString error() const { return m_error; }
    Q_INVOKABLE void edit(const QString& value) { if (m_text == value) return; m_text = value; Q_EMIT edited(); Q_EMIT changed(); }
    void setError(const QString& value) { if (m_error == value) return; m_error = value; Q_EMIT changed(); }
Q_SIGNALS:
    void changed();
    void edited();
private:
    QString m_text, m_error;
};
class MockWalletOperationPrompt final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString promptId READ promptId CONSTANT)
    Q_PROPERTY(QString operationId MEMBER m_operation CONSTANT)
    Q_PROPERTY(Kind kind MEMBER m_kind CONSTANT)
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString message READ message CONSTANT)
    Q_PROPERTY(bool busy MEMBER m_busy NOTIFY changed)
    Q_PROPERTY(QVariantMap error READ error NOTIFY changed)
public:
    enum Kind { Confirmation, Passphrase }; Q_ENUM(Kind)
    enum Response { Decline, Confirm }; Q_ENUM(Response)
    using Answer = std::function<WalletCreationReceipt(const QString&, const QString&, bool)>;
    MockWalletOperationPrompt(QString operation, QString id, Kind kind, Answer answer, QObject* parent)
        : QObject(parent), m_id(std::move(id)), m_operation(std::move(operation)), m_kind(kind), m_answer(std::move(answer)) {}
    QString promptId() const { return m_id; }
    QString title() const { return m_kind == Confirmation ? QStringLiteral("Wallet update required") : QStringLiteral("Enter wallet password"); }
    QString message() const { return QStringLiteral("Original wallet operation ") + m_operation; }
    QVariantMap error() const { return {{QStringLiteral("message"), m_error}}; }
    Q_INVOKABLE WalletCreationReceipt answer(const QString& id, Response response) { return invoke(id, {}, response == Decline); }
    Q_INVOKABLE WalletCreationReceipt submitSecret(const QString& id, const QString& secret) { return invoke(id, secret, false); }
    Q_INVOKABLE WalletCreationReceipt dismiss(const QString& id) { return invoke(id, {}, true); }
    WalletCreationReceipt invoke(const QString& id, const QString& secret, bool decline) {
        if (id != m_id || m_busy) return {id, false, {QStringLiteral("StalePrompt"), QStringLiteral("Wrong original prompt"), {}, {}}};
        return m_answer(id, secret, decline);
    }
    void setError(QString error) { m_error = std::move(error); Q_EMIT changed(); }
Q_SIGNALS:
    void changed();
private:
    const QString m_id, m_operation;
    const Kind m_kind;
    const Answer m_answer;
    QString m_error;
    bool m_busy{false};
};
class MockWalletOperationModel : public QObject, public WalletOperationPresentation
{
    Q_OBJECT
    Q_INTERFACES(WalletOperationPresentation)
    Q_PROPERTY(OperationKind kind READ operationKind CONSTANT)
    Q_PROPERTY(QString targetWalletId MEMBER m_name CONSTANT)
    Q_PROPERTY(QString errorTitle READ errorTitle NOTIFY changed)
    Q_PROPERTY(QString errorDescription READ errorDescription NOTIFY changed)
    Q_PROPERTY(MockWalletOperationPrompt* prompt READ prompt NOTIFY changed)
    Q_PROPERTY(QQmlListProperty<MockWalletOperationPrompt> prompts READ prompts NOTIFY promptsChanged)
    Q_PROPERTY(MockWalletOperationPrompt* confirmationPrompt READ confirmationPrompt NOTIFY promptsChanged)
    Q_PROPERTY(MockWalletOperationPrompt* passphrasePrompt READ passphrasePrompt NOTIFY promptsChanged)
    Q_PROPERTY(QQmlListProperty<MockWalletQmlModel> resultWallets READ resultWallets NOTIFY changed)
    Q_PROPERTY(QString operationId MEMBER m_id CONSTANT)
    Q_PROPERTY(QString submittedName MEMBER m_name CONSTANT)
    Q_PROPERTY(quint64 editorRevision MEMBER m_revision CONSTANT)
    Q_PROPERTY(Kind creationKind MEMBER m_kind CONSTANT)
    Q_PROPERTY(Phase phase READ phase NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool terminal READ terminal NOTIFY changed)
    Q_PROPERTY(bool acceptedForDelivery MEMBER m_accepted CONSTANT)
    Q_PROPERTY(bool outcomeKnown READ terminal NOTIFY changed)
    Q_PROPERTY(bool encrypted MEMBER m_encrypted CONSTANT)
    Q_PROPERTY(QString keyScheme READ keyScheme NOTIFY changed)
    Q_PROPERTY(MockWalletQmlModel* resultWallet READ resultWallet NOTIFY changed)
    Q_PROPERTY(bool canOpenResult READ canOpenResult NOTIFY changed)
    Q_PROPERTY(bool canReconcile READ canReconcile CONSTANT)
    Q_PROPERTY(QVariantMap error READ error NOTIFY changed)
    Q_PROPERTY(QVariantMap result READ result NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel* errorDetails READ errorDetails CONSTANT)
    Q_PROPERTY(QAbstractItemModel* warningDetails READ warningDetails CONSTANT)
public:
    enum OperationKind { Create, Restore, Migrate, Open }; Q_ENUM(OperationKind)
    enum Kind { Software, WatchOnly, ExternalSigner }; Q_ENUM(Kind)
    enum Phase { Requested, Validating, Creating, SettingUp, Publishing, Succeeded, Failed, Partial, Uncertain, Rejected, Cancelled, AwaitingConfirmation, AwaitingPassphrase }; Q_ENUM(Phase)
    MockWalletOperationModel(QString id, QString name, quint64 revision, Kind kind, bool encrypted, bool accepted, QObject* parent)
        : QObject(parent), m_id(std::move(id)), m_name(std::move(name)), m_revision(revision), m_kind(kind), m_accepted(accepted), m_encrypted(encrypted), m_phase(accepted ? Creating : Rejected) { if (!accepted) m_error = QStringLiteral("Transport refused. No wallet was created."); }
    Phase phase() const { return m_phase; }
    bool busy() const { return m_phase <= Publishing; }
    bool terminal() const { return !busy() && m_phase != AwaitingConfirmation && m_phase != AwaitingPassphrase; }
    OperationKind operationKind() const { return m_operation_kind; }
    void setOperationKind(OperationKind kind) { m_operation_kind = kind; }
    QString errorTitle() const { return m_error.isEmpty() ? QString{} : QStringLiteral("Core could not complete this wallet operation"); }
    QString errorDescription() const { return m_error; }
    MockWalletOperationPrompt* prompt() const { return m_phase == AwaitingConfirmation || m_phase == AwaitingPassphrase ? m_prompt : nullptr; }
    QQmlListProperty<MockWalletOperationPrompt> prompts() { return {this, &m_prompts}; }
    MockWalletOperationPrompt* confirmationPrompt() const { return m_confirmation_prompt; }
    MockWalletOperationPrompt* passphrasePrompt() const { return m_passphrase_prompt; }
    QQmlListProperty<MockWalletQmlModel> resultWallets() { return {this, this,
        [](QQmlListProperty<MockWalletQmlModel>* value) -> qsizetype { return static_cast<MockWalletOperationModel*>(value->data)->m_wallet ? 1 : 0; },
        [](QQmlListProperty<MockWalletQmlModel>* value, qsizetype index) -> MockWalletQmlModel* { return index == 0 ? static_cast<MockWalletOperationModel*>(value->data)->m_wallet.data() : nullptr; }}; }
    QString presentationId() const override { return m_id; }
    bool presentationBusy() const override { return busy(); }
    bool presentationTerminal() const override { return terminal(); }
    bool presentationHasPrompt() const override { return prompt() != nullptr; }
    QString presentationPromptId() const override { return prompt() ? m_prompt->promptId() : QString{}; }
    WalletCreationReceipt dismissPresentation() override {
        if (busy()) return {m_id, false, {QStringLiteral("Busy"), QStringLiteral("Original effect still running"), {}, {}}};
        if (!terminal()) { m_phase = Cancelled; Q_EMIT changed(); }
        return {m_id, true, {}};
    }
    QMetaObject::Connection observePresentation(QObject* context, std::function<void()> changed) override { return connect(this, &MockWalletOperationModel::changed, context, std::move(changed)); }
    std::function<void(const QString&)> promptAccepted;
    void requestPrompt(bool passphrase, const QString& error = {}) {
        m_error = error; m_errors.setText(error); m_phase = passphrase ? AwaitingPassphrase : AwaitingConfirmation;
        const QString id = m_id + QLatin1Char(':') + QString::number(m_prompts.size() + 1);
        m_prompt = new MockWalletOperationPrompt(m_id, id, passphrase ? MockWalletOperationPrompt::Passphrase : MockWalletOperationPrompt::Confirmation,
            [this, id](const QString& token, const QString& secret, bool decline) -> WalletCreationReceipt {
                if (token != id || presentationPromptId() != id) return {token, false, {QStringLiteral("StalePrompt"), QStringLiteral("Wrong original prompt"), {}, {}}};
                if (decline) { m_phase = Cancelled; Q_EMIT changed(); return {token, true, {}}; }
                m_phase = Creating; Q_EMIT changed(); if (promptAccepted) promptAccepted(secret); return {token, true, {}};
            }, this);
        if (passphrase) m_passphrase_prompt = m_prompt; else m_confirmation_prompt = m_prompt;
        m_prompt->setError(error); m_prompts.push_back(m_prompt); Q_EMIT promptsChanged(); Q_EMIT changed();
    }
    QString keyScheme() const { if (!m_key_scheme.isEmpty()) return m_key_scheme; return m_kind == WatchOnly ? QStringLiteral("Watch-only") : m_kind == ExternalSigner ? QStringLiteral("External signer") : QStringLiteral("Software wallet"); }
    MockWalletQmlModel* resultWallet() const { return m_wallet; }
    bool canReconcile() const { return false; }
    Q_INVOKABLE void reconcile() {}
    bool canOpenResult() const { return m_phase == Succeeded && m_wallet && m_wallet->m_available; }
    QVariantMap error() const { return {{QStringLiteral("message"), m_error.left(1024)}, {QStringLiteral("code"), m_error.isEmpty() ? QString{} : QStringLiteral("FixtureFailure")}, {QStringLiteral("field"), QString{}}, {QStringLiteral("recovery"), m_recovery}}; }
    QVariantMap result() const { return {{QStringLiteral("resultId"), m_id + QStringLiteral(":result")}, {QStringLiteral("warnings"), m_warnings.isEmpty() ? QStringList{} : QStringList{m_warnings.left(1024)}}, {QStringLiteral("warningsTruncated"), m_warnings.size() > 1024}, {QStringLiteral("recoveryPaths"), m_recovery_paths}}; }
    QAbstractItemModel* errorDetails() { return &m_errors; }
    QAbstractItemModel* warningDetails() { return &m_warning_lines; }
    void complete(MockWalletQmlModel* wallet, const QString& warnings, const QString& error, bool created_artifact) {
        if (!busy()) return;
        m_wallet = created_artifact ? nullptr : wallet; m_warnings = warnings; m_error = error; m_errors.setText(error); m_warning_lines.setText(warnings);
        m_recovery_paths = created_artifact ? QStringList{QStringLiteral("wallets/") + m_name} : QStringList{};
        m_recovery = created_artifact ? QStringLiteral("Keep the created wallet file; do not repeat creation.") : QString{};
        m_phase = created_artifact ? Partial : error.isEmpty() && wallet ? Succeeded : Failed;
        Q_EMIT changed(); Q_EMIT finished();
    }
Q_SIGNALS:
    void changed();
    void promptsChanged();
    void finished();
public:
    QString m_key_scheme;
private:
    OperationKind m_operation_kind{Create};
    MockWalletOperationPrompt* m_prompt{nullptr};
    MockWalletOperationPrompt* m_confirmation_prompt{nullptr};
    MockWalletOperationPrompt* m_passphrase_prompt{nullptr};
    QList<MockWalletOperationPrompt*> m_prompts;
    QString m_id, m_name;
    quint64 m_revision;
    Kind m_kind;
    bool m_accepted, m_encrypted;
    Phase m_phase;
    QPointer<MockWalletQmlModel> m_wallet;
    QString m_error, m_warnings, m_recovery;
    QStringList m_recovery_paths;
    MockWalletErrorLines m_errors{this}, m_warning_lines{this};
};
class MockWalletCreationModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(MockWalletCreationText* name READ name CONSTANT)
    Q_PROPERTY(MockWalletCreationText* extendedPublicKey READ extendedPublicKey CONSTANT)
    Q_PROPERTY(QVariantMap error READ error NOTIFY changed)
    Q_PROPERTY(Kind kind READ kind NOTIFY changed)
    Q_PROPERTY(bool encryptRequested READ encryptRequested NOTIFY changed)
    Q_PROPERTY(bool pendingValidation READ pendingValidation NOTIFY changed)
    Q_PROPERTY(bool canCreate READ canCreate NOTIFY changed)
    Q_PROPERTY(MockWalletOperationModel* operation READ operation NOTIFY changed)
public:
    enum Kind { Software, WatchOnly, ExternalSigner }; Q_ENUM(Kind)
    using Submit = std::function<MockWalletOperationModel*(const QString&, const QString&, Kind, bool, quint64)>;
    MockWalletCreationModel(Submit submit, std::function<bool()> available, QObject* parent) : QObject(parent), m_submit(std::move(submit)), m_available(std::move(available)) {
        connect(&m_name, &MockWalletCreationText::edited, this, &MockWalletCreationModel::refresh);
        connect(&m_key, &MockWalletCreationText::edited, this, &MockWalletCreationModel::refresh);
        refresh();
    }
    MockWalletCreationText* name() { return &m_name; }
    MockWalletCreationText* extendedPublicKey() { return &m_key; }
    Kind kind() const { return m_kind; }
    QVariantMap error() const { return {{QStringLiteral("message"), m_error.value(QStringLiteral("message")).toString()}, {QStringLiteral("code"), m_error.value(QStringLiteral("code")).toString()}}; }
    bool encryptRequested() const { return m_encrypt; }
    bool pendingValidation() const { return m_pending; }
    bool canCreate() const { return !m_closed && !m_pending && m_available() && m_name.error().isEmpty() && (m_kind != WatchOnly || (!m_key.text().isEmpty() && m_key.error().isEmpty())); }
    MockWalletOperationModel* operation() const { return m_operation; }
    Q_INVOKABLE void chooseKind(Kind kind) { m_kind = kind; refresh(); }
    Q_INVOKABLE void chooseSigner(const QString&) {}
    Q_INVOKABLE void setEncryptRequested(bool value) { m_encrypt = value; ++m_revision; Q_EMIT changed(); }
    Q_INVOKABLE void discard() { m_closed = true; ++m_revision; ++m_validation_revision; Q_EMIT changed(); }
    Q_INVOKABLE MockWalletOperationModel* create(const QString&, const QString&) {
        if (m_closed) return nullptr;
        auto* submitted = m_submit(m_name.text(), m_key.text(), m_kind, m_encrypt, m_revision);
        if (submitted->phase() == MockWalletOperationModel::Rejected) m_error = submitted->error();
        else { m_operation = submitted; m_error.clear(); }
        Q_EMIT changed(); return submitted;
    }
Q_SIGNALS:
    void changed();
private:
    void refresh() {
        const auto revision = ++m_validation_revision; ++m_revision; m_pending = true; Q_EMIT changed();
        QTimer::singleShot(0, this, [this, revision] {
            if (m_closed || revision != m_validation_revision) return;
            const auto name = m_name.text();
            m_name.setError(name.isEmpty() ? QStringLiteral("Enter a wallet name.") : name != name.trimmed() || name.endsWith('/') || name.startsWith('/') ? QStringLiteral("Use a canonical relative wallet name.") : QString{});
            const auto key = m_key.text().trimmed();
            m_key.setError(!key.isEmpty() && key.size() >= 100 && (key.startsWith("xpub") || key.startsWith("tpub")) ? QString{} : QStringLiteral("Invalid extended public key."));
            m_pending = false; Q_EMIT changed();
        });
    }
    QVariantMap m_error;
    Submit m_submit;
    std::function<bool()> m_available;
    MockWalletCreationText m_name{this}, m_key{this};
    QPointer<MockWalletOperationModel> m_operation;
    Kind m_kind{Software};
    bool m_encrypt{true}, m_pending{false}, m_closed{false};
    quint64 m_revision{0}, m_validation_revision{0};
};

struct SignerPresentationFacts {
    signerdiscovery::Configuration configuration{QCoreApplication::applicationFilePath(), QStringLiteral("fixture"), true, QStringLiteral("Persistent")};
    QString fingerprint{QStringLiteral("00000001")}, name{QStringLiteral("Test signer")}, error, save_error;
    int reads{0}, writes{0};
};
class MockWalletController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(SignerDiscoveryModel* signers READ signers CONSTANT)
    Q_PROPERTY(int signerEnumerationCalls READ signerEnumerationCalls NOTIFY externalSignerStatusChanged)
    Q_PROPERTY(int signerConfigurationWrites READ signerConfigurationWrites NOTIFY externalSignerStatusChanged)

    Q_PROPERTY(bool holdCreation MEMBER m_hold_creation)
    Q_PROPERTY(bool refuseNextCreation MEMBER m_refuse_creation)
    Q_PROPERTY(int creationCalls MEMBER m_creation_calls NOTIFY walletLoadInProgressChanged)
    Q_PROPERTY(QString submittedCreationName MEMBER m_creation_name NOTIFY walletLoadInProgressChanged)
    Q_PROPERTY(QString creationWarnings MEMBER m_creation_warnings)
    Q_PROPERTY(bool initialized MEMBER m_initialized NOTIFY initializedChanged)
    Q_PROPERTY(bool isWalletLoaded MEMBER m_is_wallet_loaded NOTIFY isWalletLoadedChanged)
    Q_PROPERTY(bool noWalletsFound MEMBER m_no_wallets_found NOTIFY noWalletsFoundChanged)
    Q_PROPERTY(bool walletLoadInProgress MEMBER m_wallet_load_in_progress NOTIFY walletLoadInProgressChanged)
    Q_PROPERTY(QString walletLoadError MEMBER m_wallet_load_error NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(bool walletLoadHasError READ walletLoadHasError NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(QString walletLoadErrorDisplay READ walletLoadErrorDisplay NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(QString walletLoadWarnings MEMBER m_wallet_load_warnings NOTIFY walletLoadWarningsChanged)
    Q_PROPERTY(QString walletImportErrorTitle READ walletImportErrorTitle NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(QString walletImportErrorDescription READ walletImportErrorDescription NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(QString walletImportErrorHelpText READ walletImportErrorHelpText NOTIFY walletLoadErrorChanged)
    Q_PROPERTY(QAbstractItemModel* walletImportErrorHelpModel READ walletImportErrorHelpModel CONSTANT)
    Q_PROPERTY(bool walletImportErrorPending READ walletImportErrorPending CONSTANT)
    Q_PROPERTY(bool walletImportErrorUnavailable READ walletImportErrorUnavailable CONSTANT)
    Q_PROPERTY(QString walletImportErrorProjectionError READ walletImportErrorProjectionError CONSTANT)
    Q_PROPERTY(bool walletImportErrorRetryAvailable READ walletImportErrorRetryAvailable CONSTANT)
    Q_PROPERTY(QString walletCreateError MEMBER m_wallet_create_error NOTIFY walletCreateErrorChanged)
    Q_PROPERTY(bool walletCreateHasError READ walletCreateHasError NOTIFY walletCreateErrorChanged)
    Q_PROPERTY(QString walletCreateErrorDisplay READ walletCreateErrorDisplay NOTIFY walletCreateErrorChanged)
    Q_PROPERTY(QAbstractItemModel* walletCreateErrorHelpModel READ walletCreateErrorHelpModel CONSTANT)
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
    Q_PROPERTY(QObject* closeTarget READ closeTarget NOTIFY closeTargetChanged)
    Q_PROPERTY(QAbstractItemModel* openWalletModel READ openWalletModel CONSTANT)
    Q_PROPERTY(QList<QObject*> openWallets READ openWallets NOTIFY openWalletsChanged)
    Q_PROPERTY(QObject* openedWallet MEMBER m_opened_wallet NOTIFY openWalletsChanged)
    Q_PROPERTY(QObject* lastLocationWallet MEMBER m_last_location_wallet NOTIFY openWalletLocationCallsChanged)
    Q_PROPERTY(int closePaymentRequestDetailRequests MEMBER m_close_payment_request_detail_requests NOTIFY closePaymentRequestDetailRequestsChanged)
    Q_PROPERTY(int openReceiveRequests MEMBER m_open_receive_requests NOTIFY openReceiveRequestsChanged)
    Q_PROPERTY(QString walletLocationOpenError READ walletLocationOpenError NOTIFY walletLocationOpenErrorChanged)
    Q_PROPERTY(int openWalletLocationCalls READ openWalletLocationCalls NOTIFY openWalletLocationCallsChanged)

public:
    bool m_hold_creation{false}, m_refuse_creation{false};
    int m_creation_calls{0};
    quint64 m_creation_sequence{0};
    QString m_creation_name, m_creation_warnings;
    QPointer<MockWalletOperationModel> m_pending_creation;
    explicit MockWalletController(QObject* parent = nullptr) : QObject(parent) {
        SignerDiscoveryModel::Services services;
        services.discovery.scheduler = m_signer_process.scheduler;
        services.configuration.scheduler = m_signer_configuration.scheduler;
        const auto facts = m_signer_facts;
        services.ports.configuration = [facts] { return facts->configuration; };
        services.ports.enumerate = [facts](const signerdiscovery::Configuration& config) {
            ++facts->reads;
            if (!facts->error.isEmpty()) throw std::runtime_error(facts->error.toStdString());
            std::vector<signerdiscovery::Device> devices;
            if (!facts->fingerprint.isEmpty()) devices.push_back({config.id + QLatin1Char(':') + facts->fingerprint, facts->name, facts->fingerprint, true});
            return devices;
        };
        services.ports.save = [facts](const QString& command) -> signerdiscovery::Result {
            if (const auto error = signerdiscovery::PathError(command); !error.isEmpty()) return {facts->configuration, {}, signerdiscovery::Error::InvalidPath, error};
            if (!facts->save_error.isEmpty()) return {facts->configuration, {}, signerdiscovery::Error::Persistence, facts->save_error};
            ++facts->writes; facts->configuration.command = command;
            facts->configuration.id = QString::fromLatin1(QCryptographicHash::hash(command.toUtf8(), QCryptographicHash::Sha256).toHex());
            return {facts->configuration, {}, signerdiscovery::Error::None, {}};
        };
        m_signers = new SignerDiscoveryModel(std::move(services), this);
        connect(m_signers, &SignerDiscoveryModel::changed, this, &MockWalletController::externalSignerStatusChanged);

        connect(this, &MockWalletController::walletLoadErrorChanged, this, [this] { m_error_lines.setText(m_wallet_load_error); });
        connect(this, &MockWalletController::walletCreateErrorChanged, this, [this] { m_create_error_lines.setText(m_wallet_create_error); });
        connect(this, &MockWalletController::walletLoadStateChanged, this, [this](const QString& name, int state, const QString& error) { if (state == 3) Q_EMIT walletLoadErrorNotice(name, ++m_notice_operation, error); });
        connect(this, &MockWalletController::walletMigrationRequired, this, [this](const QString& name) { m_migration_notice_path = name; });
        connect(this, &MockWalletController::walletMigrationPassphraseRequired, this, [this](const QString& name) { m_migration_notice_path = name; });
        connect(this, &MockWalletController::walletMigrationFailed, this, [this] { Q_EMIT walletMigrationErrorNotice(m_migration_notice_path, ++m_notice_operation, m_wallet_migration_error.contains(QLatin1String("passphrase"), Qt::CaseInsensitive), m_wallet_migration_error.left(1024)); });
    }
    ~MockWalletController() override { if (m_held_signer) m_signer_process.finishWork(*m_held_signer); m_signers->closeAdmission(); advanceSignerForTest(); delete m_signers; }
    SignerDiscoveryModel* signers() const { return m_signers; }
    int signerEnumerationCalls() const { return m_signer_facts->reads; }
    int signerConfigurationWrites() const { return m_signer_facts->writes; }
    signerdiscovery::Configuration signerConfigurationForTest()const{return m_signer_facts->configuration;}
    void settingsSignerSavedForTest(const QString& command){++m_signer_facts->writes;const auto old=m_signer_facts->configuration;m_signer_facts->configuration={command,QString::fromLatin1(QCryptographicHash::hash(command.toUtf8(),QCryptographicHash::Sha256).toHex()),old.editable,old.source};m_signers->refresh();}
    Q_INVOKABLE QString signerExecutableForTest() const { return QCoreApplication::applicationFilePath(); }
    Q_INVOKABLE QString alternateSignerExecutableForTest() const {
        const QFileInfo executable{QCoreApplication::applicationFilePath()};
        return executable.path() + QStringLiteral("/./") + executable.fileName();
    }
    Q_INVOKABLE void setSignerFixtureForTest(const QString& command, const QString& fingerprint, const QString& name, const QString& error, bool read_only = false, bool refresh = true) {
        m_signer_facts->configuration = {command, QString::fromLatin1(QCryptographicHash::hash(command.toUtf8(), QCryptographicHash::Sha256).toHex()), !read_only, read_only ? QStringLiteral("CommandLine") : QStringLiteral("Persistent")};
        m_signer_facts->fingerprint = fingerprint; m_signer_facts->name = name; m_signer_facts->error = error;
        if (refresh) m_signers->refresh();
    }
    Q_INVOKABLE void setSignerSaveErrorForTest(const QString& error) { m_signer_facts->save_error = error; }
    Q_INVOKABLE void advanceSignerForTest() { for (int turn = 0; turn < 2048; ++turn) { if (!m_signer_process.stepNext() && !m_signer_configuration.stepNext()) break; } }
    Q_INVOKABLE bool holdSignerProcessForTest() {
        while (m_signer_process.step(execution::Lane::Domain)) {}
        m_held_signer = m_signer_process.dispatchNext(execution::Lane::Core); return m_held_signer.has_value();
    }
    Q_INVOKABLE void releaseSignerProcessForTest() { if (m_held_signer) { m_signer_process.finishWork(*m_held_signer); m_held_signer.reset(); } advanceSignerForTest(); }
    bool walletLoadHasError() const { return !m_wallet_load_error.isEmpty(); }
    bool walletCreateHasError() const { return !m_wallet_create_error.isEmpty(); }
    QString walletLoadErrorDisplay() const { return m_wallet_load_error.left(1024); }
    QString walletCreateErrorDisplay() const { return m_wallet_create_error.left(1024); }
    QAbstractItemModel* walletCreateErrorHelpModel() { return &m_create_error_lines; }
    QAbstractItemModel* walletImportErrorHelpModel() { return &m_error_lines; }
    bool walletImportErrorPending() const { return false; }
    bool walletImportErrorUnavailable() const { return false; }
    QString walletImportErrorProjectionError() const { return {}; }
    bool walletImportErrorRetryAvailable() const { return false; }
    Q_INVOKABLE bool retryWalletImportErrorPresentation() { return false; }
    Q_INVOKABLE QAbstractItemModel* captureMigrationErrorDetails(quint64, int slot) { auto* lines = slot == 0 ? &m_migration_required_details : &m_migration_passphrase_details; lines->setText(m_wallet_migration_error); return lines; }
    Q_INVOKABLE MockWalletCreationModel* newCreation() {
        return new MockWalletCreationModel([this](const QString& name, const QString&, MockWalletCreationModel::Kind kind, bool encrypt, quint64 revision) {
            const bool accepted = (!m_pending_creation || !m_pending_creation->busy()) && !std::exchange(m_refuse_creation, false);
            auto* operation = new MockWalletOperationModel(QString::number(++m_creation_sequence), name, revision,
                static_cast<MockWalletOperationModel::Kind>(kind), kind == MockWalletCreationModel::Software && encrypt, accepted, this);
            if (!accepted) return operation;
            m_pending_creation = operation; m_creation_name = name; ++m_creation_calls;
            m_wallet_load_in_progress = true; Q_EMIT walletLoadInProgressChanged();
            if (!m_hold_creation) QTimer::singleShot(0, operation, [this, operation] { if (m_pending_creation == operation) finishCreation(); });
            return operation;
        }, [this] { return m_initialized && (!m_pending_creation || !m_pending_creation->busy()); }, this);
    }
    Q_INVOKABLE void finishCreation(const QString& error = QString(), bool created_artifact = false) {
        if (!m_pending_creation || !m_pending_creation->busy()) return;
        auto* operation = m_pending_creation.data();
        operation->complete(qobject_cast<MockWalletQmlModel*>(reportedWallet()), m_creation_warnings, error, created_artifact);
        m_wallet_load_in_progress = false; Q_EMIT walletLoadInProgressChanged();
    }
    Q_INVOKABLE MockWalletProvisioningCheck* createProvisioningCheck() { return new MockWalletProvisioningCheck(this); }
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
    QList<QObject*> m_open_wallets;
    // The wallet reported by create, import and load success; the selected
    // wallet unless a test sets another.
    QObject* m_opened_wallet{nullptr};
    QObject* m_last_location_wallet{nullptr};
    QObject* reportedWallet() const { return m_opened_wallet ? m_opened_wallet : m_selected_wallet; }
    QString m_last_selected_wallet_name;
    QString m_last_closed_wallet_name;
    int m_close_wallet_calls{0};
    int m_close_payment_request_detail_requests{0};
    int m_open_receive_requests{0};
    QString m_wallet_location_open_error;
    bool m_open_wallet_location_result{true};
    QString m_open_wallet_location_error;
    int m_open_wallet_location_calls{0};

    QObject* selectedWallet() const { return m_selected_wallet; }
    QList<QObject*> openWallets() const { return m_open_wallets; }
    QAbstractItemModel* openWalletModel() { return &m_open_model; }
    Q_INVOKABLE int rowForWallet(QObject* wallet) const { return m_open_model.rowForWallet(wallet); }
    QString lastSelectedWalletName() const { return m_last_selected_wallet_name; }
    QString lastClosedWalletName() const { return m_last_closed_wallet_name; }
    int closeWalletCalls() const { return m_close_wallet_calls; }
    QString walletLocationOpenError() const { return m_wallet_location_open_error; }
    int openWalletLocationCalls() const { return m_open_wallet_location_calls; }
    QString walletImportErrorTitle() const { return m_wallet_load_error.isEmpty() ? QString{} : QStringLiteral("Failed to load wallet"); }
    QString walletImportErrorDescription() const { return m_wallet_load_error; }
    QString walletImportErrorHelpText() const { return QString{}; }
    Q_INVOKABLE QString homePath() const { return QStringLiteral("/tmp"); }
    Q_PROPERTY(QUrl homeUrl READ homeUrl CONSTANT)
    QUrl homeUrl() const { return QUrl::fromLocalFile(QStringLiteral("/tmp")); }
    Q_PROPERTY(int externalSignerRefreshCalls MEMBER m_external_signer_refresh_calls NOTIFY externalSignerStatusChanged)
    int m_external_signer_refresh_calls{0};
    Q_INVOKABLE void refreshExternalSignerStatus() { ++m_external_signer_refresh_calls; m_signers->refresh(); Q_EMIT externalSignerStatusChanged(); }
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
        Q_EMIT walletCreateSucceeded(reportedWallet());
    }
    Q_INVOKABLE void createSingleSigWallet(const QString&, const QString&)
    {
        clearWalletCreateStatus();
        clearWalletLoadStatus();
        Q_EMIT walletCreateSucceeded(reportedWallet());
    }
    Q_INVOKABLE bool createExternalSignerWallet(const QString&)
    {
        clearWalletLoadStatus();
        if (!m_can_create_external_signer_wallet) {
            m_wallet_load_error = QStringLiteral("Connect an external signer and try again.");
            Q_EMIT walletLoadErrorChanged();
            return false;
        }
        Q_EMIT walletLoadSucceeded(reportedWallet());
        return true;
    }
    Q_PROPERTY(int restoreCalls MEMBER m_restore_calls NOTIFY provisioningChanged)
    Q_PROPERTY(QString lastRestoreSource MEMBER m_restore_source NOTIFY provisioningChanged)
    Q_PROPERTY(bool holdRestore MEMBER m_hold_restore NOTIFY provisioningChanged)
    Q_PROPERTY(bool holdMigration MEMBER m_hold_migration NOTIFY provisioningChanged)
    Q_PROPERTY(QString nextRestoreError MEMBER m_restore_error NOTIFY provisioningChanged)
    Q_PROPERTY(QString nextRestoreWarnings MEMBER m_restore_warnings NOTIFY provisioningChanged)
    Q_PROPERTY(MockWalletOperationModel* lastRestore READ lastRestore NOTIFY provisioningChanged)
    Q_PROPERTY(MockWalletOperationModel* lastMigration READ lastMigration NOTIFY provisioningChanged)
    MockWalletOperationModel* lastRestore() const { return m_restore; }
    MockWalletOperationModel* lastMigration() const { return m_migration; }
    Q_INVOKABLE MockWalletOperationModel* restore(const QUrl& source) {
        auto* operation = new MockWalletOperationModel(QString::number(++m_creation_sequence), m_last_imported_wallet_name.isEmpty() ? QFileInfo(source.toLocalFile()).completeBaseName() : m_last_imported_wallet_name,
            0, MockWalletOperationModel::Software, false, !m_wallet_load_in_progress, this);
        operation->setOperationKind(MockWalletOperationModel::Restore); operation->m_key_scheme = m_last_imported_wallet_key_scheme;
        m_restore = operation; m_restore_source = source.toLocalFile(); ++m_restore_calls;
        if (operation->phase() != MockWalletOperationModel::Rejected) {
            m_wallet_load_in_progress = true; Q_EMIT walletLoadInProgressChanged();
            if (!m_hold_restore) QTimer::singleShot(0, operation, [this, operation] { if (m_restore == operation) finishRestore(); });
        }
        Q_EMIT provisioningChanged(); return operation;
    }
    Q_INVOKABLE MockWalletOperationModel* restoreLocalFile(const QString& path) { return restore(QUrl::fromLocalFile(path)); }
    Q_INVOKABLE void importWallet(const QString& path) { restoreLocalFile(path); }
    Q_INVOKABLE void finishRestore(const QString& error = {}, bool copied = false) {
        if (!m_restore || !m_restore->busy()) return;
        const auto failure = error.isEmpty() ? std::exchange(m_restore_error, {}) : error;
        m_restore->complete(qobject_cast<MockWalletQmlModel*>(reportedWallet()), m_restore_warnings, failure, copied);
        m_wallet_load_in_progress = false; Q_EMIT walletLoadInProgressChanged();
        if (failure.isEmpty() && !copied) Q_EMIT walletImportSucceeded(reportedWallet());
    }
    Q_INVOKABLE MockWalletOperationModel* migrate(const QString& path) {
        auto* operation = new MockWalletOperationModel(QString::number(++m_creation_sequence), path, 0, MockWalletOperationModel::Software, false, true, this);
        operation->setOperationKind(MockWalletOperationModel::Migrate); m_migration = operation;
        operation->promptAccepted = [this, operation](const QString&) {
            m_wallet_load_in_progress = true; Q_EMIT walletLoadInProgressChanged();
            if (!m_hold_migration) QTimer::singleShot(0, operation, [this, operation] { if (m_migration == operation) finishMigration(); });
        };
        operation->requestPrompt(false); Q_EMIT migrationOperationOffered(operation); Q_EMIT provisioningChanged(); return operation;
    }
    Q_INVOKABLE void requestMigrationPassphrase(const QString& error = {}) { if (m_migration) { m_migration->requestPrompt(true, error); m_wallet_load_in_progress = false; Q_EMIT walletLoadInProgressChanged(); } }
    Q_INVOKABLE void finishMigration(const QString& error = {}, bool partial = false) {
        if (!m_migration || !m_migration->busy()) return;
        m_migration->complete(qobject_cast<MockWalletQmlModel*>(reportedWallet()), m_restore_warnings, error, partial);
        m_wallet_load_in_progress = false; Q_EMIT walletLoadInProgressChanged();
    }
    Q_INVOKABLE void migrateWallet(const QString& path, const QString& = QString()) { migrate(path); }
    Q_INVOKABLE void requestOpenWalletSettings(QObject* wallet)
    {
        if (wallet) Q_EMIT openWalletSettingsRequested(wallet);
    }
    Q_INVOKABLE void setOpenWallets(const QList<QObject*>& wallets)
    {
        if (m_open_wallets == wallets) return;
        m_open_wallets = wallets; m_open_model.setWallets(wallets);
        Q_EMIT openWalletsChanged();
    }
    Q_INVOKABLE bool isWalletOpen(const QString& name) const
    {
        return std::any_of(m_open_wallets.begin(), m_open_wallets.end(), [&](const QObject* wallet) {
            return wallet && wallet->property("name").toString() == name;
        });
    }
    Q_INVOKABLE void setSelectedWalletObject(QObject* wallet)
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
    QObject* closeTarget() const { return m_close_target; }
    Q_INVOKABLE QObject* walletForName(const QString& name) const
    {
        for (auto* wallet : m_open_wallets) if (wallet && wallet->property("name").toString() == name) return wallet;
        return nullptr;
    }
    Q_INVOKABLE bool selectWallet(QObject* wallet)
    {
        if (!wallet || !wallet->property("available").toBool() || !m_open_wallets.contains(wallet)) return false;
        setSelectedWalletObject(wallet);
        setSelectedWallet(wallet->property("name").toString());
        return true;
    }
    Q_INVOKABLE void requestCloseWallet(QObject* wallet)
    {
        auto* model = qobject_cast<MockWalletQmlModel*>(wallet);
        if (!model || !wallet->property("available").toBool() || !m_open_wallets.contains(wallet)) return;
        QObject::disconnect(m_close_connection);
        if (m_close_target && m_close_target != wallet) m_close_target->setProperty("closeQuestionToken", QString{});
        m_close_target = wallet;
        m_close_connection = connect(model, &MockWalletQmlModel::closeConfirmed, this, [this, model] { closeWallet(model->property("name").toString()); });
        Q_EMIT closeTargetChanged();
        model->requestClose();
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
        if (m_held_signer) { m_signer_process.finishWork(*m_held_signer); m_held_signer.reset(); }
        m_signer_facts->error.clear(); m_signer_facts->save_error.clear(); m_signers->discardChanges();
        setSignerFixtureForTest(QCoreApplication::applicationFilePath(), QStringLiteral("00000001"), QStringLiteral("Test signer"), {});
        advanceSignerForTest();

        m_pending_creation.clear(); m_hold_creation = false; m_refuse_creation = false; m_creation_calls = 0; m_creation_name.clear(); m_creation_warnings.clear();
        QObject::disconnect(m_close_connection);
        if (m_close_target) m_close_target->setProperty("closeQuestionToken", QString{});
        m_close_target = nullptr;
        Q_EMIT closeTargetChanged();
        setOpenWallets({});
        m_opened_wallet = nullptr;
        m_last_location_wallet = nullptr;
        m_last_selected_wallet_name.clear();
        m_last_closed_wallet_name.clear();
        m_close_wallet_calls = 0;
        m_close_payment_request_detail_requests = 0;
        m_open_receive_requests = 0;
        m_open_wallet_location_calls = 0;
        m_open_wallet_location_result = true;
        m_open_wallet_location_error.clear();
        clearWalletLocationOpenError();
        clearWalletLoadStatus();
        clearWalletCreateStatus();
        clearWalletMigrationStatus();
        m_restore = nullptr; m_migration = nullptr; m_restore_source.clear(); m_restore_error.clear(); m_restore_warnings.clear(); m_restore_calls = 0; m_hold_restore = false; m_hold_migration = false;
        m_last_imported_wallet_name.clear();
        m_last_imported_wallet_key_scheme.clear();
        m_external_signer_refresh_calls = 0;
        m_can_create_external_signer_wallet = false;
        m_external_signer_name.clear();
        m_external_signer_error.clear();
        m_suggested_external_signer_wallet_name = QStringLiteral("external_signer");
        Q_EMIT lastSelectedWalletNameChanged();
        Q_EMIT lastClosedWalletNameChanged();
        Q_EMIT closeWalletCallsChanged();
        Q_EMIT closePaymentRequestDetailRequestsChanged();
        Q_EMIT openReceiveRequestsChanged();
        Q_EMIT openWalletLocationCallsChanged();
        Q_EMIT lastImportedWalletInfoChanged();
        Q_EMIT externalSignerStatusChanged();
    }
    Q_INVOKABLE void requestClosePaymentRequestDetail(QObject* wallet)
    {
        ++m_close_payment_request_detail_requests;
        Q_EMIT closePaymentRequestDetailRequestsChanged();
        if (wallet) Q_EMIT closePaymentRequestDetailRequested(wallet);
    }
    Q_INVOKABLE void requestOpenReceive(QObject* wallet)
    {
        ++m_open_receive_requests;
        Q_EMIT openReceiveRequestsChanged();
        if (wallet) Q_EMIT openReceiveRequested(wallet);
    }
    Q_INVOKABLE void setOpenWalletLocationResult(const bool result, const QString& error = QString())
    {
        m_open_wallet_location_result = result;
        m_open_wallet_location_error = error;
    }
    Q_INVOKABLE bool openWalletLocation(QObject* wallet)
    {
        m_last_location_wallet = wallet;
        ++m_open_wallet_location_calls;
        Q_EMIT openWalletLocationCallsChanged();
        if (m_open_wallet_location_result) {
            clearWalletLocationOpenError();
            return true;
        }
        m_wallet_location_open_error = m_open_wallet_location_error.isEmpty()
            ? QStringLiteral("Could not open wallet file location.")
            : m_open_wallet_location_error;
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
    void closeTargetChanged();
    void initializedChanged();
    void isWalletLoadedChanged();
    void noWalletsFoundChanged();
    void walletLoadInProgressChanged();
    void walletLoadErrorChanged();
    void walletLoadWarningsChanged();
    void walletLoadWarningsOffered(MockWalletOperationModel* operation);
    void walletCreateErrorChanged();
    void walletMigrationInProgressChanged();
    void walletMigrationErrorChanged();
    void lastImportedWalletInfoChanged();
    void externalSignerStatusChanged();
    void lastSelectedWalletNameChanged();
    void lastClosedWalletNameChanged();
    void closeWalletCallsChanged();
    void selectedWalletChanged();
    void openWalletsChanged();
    void closePaymentRequestDetailRequestsChanged();
    void closePaymentRequestDetailRequested(QObject* wallet);
    void openReceiveRequestsChanged();
    void openReceiveRequested(QObject* wallet);
    void openWalletSettingsRequested(QObject* wallet);
    void walletCreateSucceeded(QObject* wallet);
    void walletLoadSucceeded(QObject* wallet);
    void walletImportSucceeded(QObject* wallet);
    void walletMigrationSucceeded();
    void walletLoadStateChanged(const QString& name, int state, const QString& error);
    void walletLoadErrorNotice(const QString& name, quint64 operation, const QString& description);
    void walletMigrationRequired(const QString& wallet_name);
    void walletMigrationPassphraseRequired(const QString& wallet_name);
    void walletMigrationFailed();
    void walletMigrationErrorNotice(const QString& name, quint64 operation, bool needsPassphrase, const QString& description);
    void migrationOperationOffered(MockWalletOperationModel* operation);
    void provisioningChanged();
    void walletLocationOpenErrorChanged();
    void openWalletLocationCallsChanged();
private:
    const std::shared_ptr<SignerPresentationFacts> m_signer_facts{std::make_shared<SignerPresentationFacts>()};
    ManualRoutedExecution m_signer_process, m_signer_configuration;
    SignerDiscoveryModel* m_signers{nullptr};
    std::optional<execution::TaskId> m_held_signer;

    QPointer<MockWalletOperationModel> m_restore, m_migration;
    QString m_restore_source, m_restore_error, m_restore_warnings;
    int m_restore_calls{0};
    bool m_hold_restore{false}, m_hold_migration{false};
    MockWalletErrorLines m_error_lines{this}, m_create_error_lines{this};
    MockWalletErrorLines m_migration_required_details{this}, m_migration_passphrase_details{this};
    QString m_migration_notice_path;
    quint64 m_notice_operation{0};
    MockOpenWalletListModel m_open_model;
    QObject* m_close_target{nullptr};
    QMetaObject::Connection m_close_connection;
};

class MockCoreSettingsModel;
class MockCoreProxyDraft;

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
    enum StorageChoice {FullStorage,ReducedStorage,CustomStorage};Q_ENUM(StorageChoice)
    Q_PROPERTY(StorageChoice storageChoice READ storageChoice NOTIFY changed)
    Q_PROPERTY(bool showCustomStorage READ showCustomStorage NOTIFY changed)
    Q_PROPERTY(QString customStorageDescription READ customStorageDescription NOTIFY changed)
    Q_PROPERTY(int customStorageSizeGB READ customStorageSizeGB NOTIFY changed)
    int64_t m_target_mib{1907},m_custom_target{-1};
    QString m_raw_prune,m_raw_prune_error;
    QString pruneTargetText()const{return m_raw_prune.isNull()?QString::number(m_prune?m_target_mib:0):m_raw_prune;}
    QString pruneTargetError()const{return m_raw_prune_error;}
    void stagePruneLiteral(const QString& text){m_raw_prune=text.isNull()?QStringLiteral(""):text.left(65);const auto value=settingsparse::Integer(text,64);m_raw_prune_error=!value||*value<0||(*value>1&&*value<550)?QStringLiteral("Invalid prune target"):QString{};if(m_raw_prune_error.isEmpty())stagePruneMiB(*value);else Q_EMIT changed();}
    StorageChoice storageChoice()const{return !m_prune?FullStorage:m_target_mib==1907?ReducedStorage:CustomStorage;}
    bool showCustomStorage()const{return customTarget()>0;}
    int64_t customTarget()const{return m_target_mib>0&&m_target_mib!=1907?m_target_mib:m_custom_target;}
    int customStorageSizeGB()const{return settingsparse::StorageGB(customTarget());}
    QString customStorageDescription()const{return customTarget()==1?QStringLiteral("Manual pruning. No automatic block storage limit is enforced."):QStringLiteral("Storing recent blocks up to %1 MiB.").arg(customTarget());}
    Q_INVOKABLE void chooseStorage(StorageChoice choice){m_raw_prune={};m_raw_prune_error={};const auto memory=customTarget();if(memory>0)m_custom_target=memory;if(choice==FullStorage)setPrune(false);else if(choice==ReducedStorage){setPrune(true);m_target_mib=1907;setPruneSizeGB(2);}else if(memory>0)stagePruneMiB(memory);Q_EMIT changed();}
    void stagePruneMiB(int64_t value){m_target_mib=value;m_prune=value>0;if(value>1)m_prune_size_gb=settingsparse::StorageGB(value);Q_EMIT changed();}
    Q_INVOKABLE void discardPruneInput(){m_raw_prune={};m_raw_prune_error={};Q_EMIT changed();}
    Q_INVOKABLE void resetPruneDraftForTest(){m_raw_prune={};m_raw_prune_error={};m_custom_target=-1;m_target_mib=1907;m_prune_size_gb=2;m_prune=true;Q_EMIT changed();}
    enum OperationState {Idle,Pending,Applied,Rejected,Stale,Failed,Closed};
    Q_ENUM(OperationState)
    explicit MockCoreSettingsModel(QObject* parent = nullptr) : QObject{parent} {}
    Q_PROPERTY(bool holdProxyEffectsForTest MEMBER m_hold_proxy_effects)
    Q_PROPERTY(bool refuseProxyAdmissionForTest MEMBER m_refuse_proxy_admission)
    Q_PROPERTY(int proxyApplyStateForTest MEMBER m_proxy_apply_state)
    Q_PROPERTY(int proxyApplyCallsForTest READ proxyApplyCallsForTest)
    int proxyApplyCallsForTest() const { return m_proxy_apply_calls; }
    Q_INVOKABLE bool deliverProxyEffectForTest() {
        if (m_proxy_effects.empty()) return false;
        auto turn = std::move(m_proxy_effects.front()); m_proxy_effects.pop_front(); turn(); return true;
    }
    void queueProxyEffect(std::function<void()> effect) {
        ++m_proxy_apply_calls;
        if (m_hold_proxy_effects) m_proxy_effects.push_back(std::move(effect));
        else QTimer::singleShot(0, this, std::move(effect));
    }
    bool m_hold_proxy_effects{false}, m_refuse_proxy_admission{false};
    int m_proxy_apply_state{Applied}, m_proxy_apply_calls{0};
    std::deque<std::function<void()>> m_proxy_effects;
    Q_INVOKABLE MockCoreProxyDraft* createProxyDraft();

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
    void setPrune(bool value) { m_raw_prune={};m_raw_prune_error={};setBool(QStringLiteral("prune"), m_prune, value); }
    void setPruneSizeGB(int value)
    {
        if (!canEdit(QStringLiteral("prune")) || value == m_prune_size_gb || value < 1) return;
        m_prune_size_gb = value;m_target_mib=static_cast<int64_t>(value*GB_BYTES/1024/1024);m_raw_prune={};m_raw_prune_error={};
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

class MockCoreProxyDraft : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool proxyEnabled READ proxyEnabled WRITE setProxyEnabled NOTIFY changed)
    Q_PROPERTY(QString proxyAddress READ proxyAddress WRITE setProxyAddress NOTIFY changed)
    Q_PROPERTY(bool torEnabled READ torEnabled WRITE setTorEnabled NOTIFY changed)
    Q_PROPERTY(QString torAddress READ torAddress WRITE setTorAddress NOTIFY changed)
    Q_PROPERTY(QString proxyError READ proxyError NOTIFY changed)
    Q_PROPERTY(QString torError READ torError NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool canApply READ canApply NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(quint64 operationId READ operationId NOTIFY changed)
    Q_PROPERTY(int operationState READ operationState NOTIFY changed)
public:
    explicit MockCoreProxyDraft(MockCoreSettingsModel& source):m_source(&source){connect(&source,&MockCoreSettingsModel::changed,this,&MockCoreProxyDraft::evaluate);connect(&source,&MockCoreSettingsModel::statusesChanged,this,&MockCoreProxyDraft::evaluate);reset();}
    bool proxyEnabled() const{return m_proxy;}QString proxyAddress() const{return m_address;}
    bool torEnabled() const{return m_tor;}QString torAddress() const{return m_onion;}
    QString proxyError() const{return m_pending?QString{}:m_proxy_error;}QString torError() const{return m_pending?QString{}:m_tor_error;}
    bool pending() const{return m_pending;}bool ready() const{return m_ready;}bool dirty() const{return m_dirty;}
    bool canApply() const{return m_ready&&!m_pending&&m_valid&&m_state!=MockCoreSettingsModel::Pending;}
    QString error() const{return m_error;}quint64 operationId() const{return m_operation;}
    int operationState() const{return m_state;}
    void setProxyEnabled(bool value){if(value==m_proxy)return;m_proxy=value;evaluate();}
    void setProxyAddress(const QString& value){if(value==m_address)return;m_address=value;evaluate();}
    void setTorEnabled(bool value){if(value==m_tor)return;m_tor=value;evaluate();}
    void setTorAddress(const QString& value){if(value==m_onion)return;m_onion=value;evaluate();}
    Q_INVOKABLE void reset(){
        if(!m_source)return;
        const auto revision=++m_revision;m_pending=true;m_ready=false;Q_EMIT changed();
        QTimer::singleShot(0,this,[this,revision]{if(!m_source||revision!=m_revision)return;
            m_ready=true;m_proxy=m_source->proxyEnabled();m_address=m_source->proxyAddress().isEmpty()?m_source->defaultAddress():m_source->proxyAddress();
            m_tor=m_source->torEnabled();m_onion=m_source->torAddress().isEmpty()?m_source->defaultAddress():m_source->torAddress();evaluate();});
    }
    Q_INVOKABLE quint64 requestApply(){
        if(!m_source||!canApply()||m_source->m_refuse_proxy_admission)return 0;
        const auto ticket=++m_operation;m_state=MockCoreSettingsModel::Pending;Q_EMIT changed();
        const auto source=m_source;const QPointer<MockCoreProxyDraft> observer{this};const bool proxy=m_proxy,tor=m_tor;const auto address=m_address,onion=m_onion;
        const int requested_state=source->m_proxy_apply_state;
        source->queueProxyEffect([source,observer,proxy,tor,address,onion,requested_state]{if(!source)return;
            const bool valid=(!proxy||source->validateAddress(address).isEmpty())&&(!tor||source->validateAddress(onion).isEmpty());
            if(valid && requested_state==MockCoreSettingsModel::Applied){if(source->canEdit("proxy")){if(source->validateAddress(address).isEmpty())source->setProxyAddress(address);source->setProxyEnabled(proxy);}
                if(source->canEdit("onion")){if(source->validateAddress(onion).isEmpty())source->setTorAddress(onion);source->setTorEnabled(tor);}}
            if(observer){observer->m_state=valid?requested_state:MockCoreSettingsModel::Failed;observer->m_error=observer->m_state==MockCoreSettingsModel::Applied?QString{}:valid?QStringLiteral("The original proxy draft could not be applied."):QStringLiteral("Invalid proxy location");Q_EMIT observer->changed();}
        });return ticket;
    }
Q_SIGNALS:void changed();
private:
    void evaluate(){
        if(!m_source)return;
        const auto revision=++m_revision;m_pending=true;Q_EMIT changed();
        QTimer::singleShot(0,this,[this,revision]{if(!m_source||revision!=m_revision)return;
            m_proxy_error=m_source->validateAddress(m_address);m_tor_error=m_source->validateAddress(m_onion);
            m_valid=(!m_proxy||m_proxy_error.isEmpty())&&(!m_tor||m_tor_error.isEmpty());
            m_dirty=(m_source->canEdit("proxy")&&(m_proxy!=m_source->proxyEnabled()||m_address!=m_source->proxyAddress()))
                ||(m_source->canEdit("onion")&&(m_tor!=m_source->torEnabled()||m_onion!=m_source->torAddress()));
            m_pending=false;Q_EMIT changed();});
    }
    QPointer<MockCoreSettingsModel> m_source;quint64 m_revision{0},m_operation{0};int m_state{MockCoreSettingsModel::Idle};
    QString m_address,m_onion,m_proxy_error,m_tor_error,m_error;
    bool m_proxy{false},m_tor{false},m_pending{true},m_valid{false},m_dirty{false},m_ready{false};
};
MockCoreProxyDraft* MockCoreSettingsModel::createProxyDraft(){auto* result=new MockCoreProxyDraft(*this);QQmlEngine::setObjectOwnership(result,QQmlEngine::JavaScriptOwnership);return result;}

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

class MockOptionsTransactionLinks : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString txid READ txid WRITE setTxid NOTIFY txidChanged)
    Q_PROPERTY(QAbstractItemModel* links READ links CONSTANT)
    Q_PROPERTY(bool pending READ pending NOTIFY resultChanged)
    Q_PROPERTY(QString error READ error NOTIFY resultChanged)
public:
    MockOptionsTransactionLinks(QObject* owner, std::function<QVariantList(const QString&)> read)
        : QAbstractListModel(owner), m_read(std::move(read)) {}
    QString txid() const { return m_txid; }
    QAbstractItemModel* links() { return this; }
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : m_links.size(); }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "host"}, {Qt::UserRole + 1, "url"}}; }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_links.size()) return {};
        const auto row = m_links[index.row()].toMap();
        return role == Qt::UserRole ? row.value("host") : role == Qt::UserRole + 1 ? row.value("url") : QVariant{};
    }
    bool pending() const { return m_pending; }
    QString error() const { return {}; }
    void setTxid(const QString& value) { if (value == m_txid) return; m_txid = value; Q_EMIT txidChanged(); refresh(); }
    void refresh() {
        const auto revision = ++m_revision;
        m_pending = true; Q_EMIT resultChanged();
        QTimer::singleShot(0, this, [this, revision] {
            if (revision != m_revision) return;
            beginResetModel(); m_links = m_read(m_txid); endResetModel();
            m_pending = false; Q_EMIT resultChanged();
        });
    }
Q_SIGNALS:
    void txidChanged();
    void resultChanged();
private:
    std::function<QVariantList(const QString&)> m_read;
    QString m_txid;
    QVariantList m_links;
    quint64 m_revision{0};
    bool m_pending{false};
};

namespace {
struct ControlledSettings {
    ManualRoutedExecution transport;
    std::deque<std::function<void()>> gui;
};
struct MockSettingsStore {
    std::mutex mutex;
    effectivesettings::Snapshot snapshot;
    std::array<effectivesettings::Value,settingsedit::FIELDS> configured;
    QString rejected;
    int writes{0};
    MockSettingsStore(){
        using namespace effectivesettings;snapshot.ready=true;snapshot.revision=QStringLiteral("settings-0");
        for(size_t i=0;i<settingsedit::FIELDS;++i){const auto key=static_cast<Key>(i);Value value=Type(key)==Kind::Boolean?Value{key==Key::Listen}:Type(key)==Kind::Text?Value{QString{}}:Value{int64_t{key==Key::Prune?550:key==Key::Threads?0:key==Key::Dbcache?450:300}};configured[i]=value;Field field{key,Type(key),value,value,value};if(key==Key::Signer||key==Key::Natpmp)field.applicability=Applicability::Live;snapshot.fields.push_back(std::move(field));}
    }
};
QList<QPointer<SettingsEditorModel>>& MockSettingsOwners(){static QList<QPointer<SettingsEditorModel>> result;return result;}
}
class MockOptionsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool settingsReady MEMBER m_settings_ready NOTIFY settingsReadyChanged)
    Q_PROPERTY(bool settingsPending MEMBER m_settings_pending NOTIFY settingsPendingChanged)
    Q_PROPERTY(QString settingsError MEMBER m_settings_error NOTIFY settingsErrorChanged)
    Q_PROPERTY(bool validationPending MEMBER m_validation_pending NOTIFY validationPendingChanged)
    Q_PROPERTY(bool signerPathValidationPending MEMBER m_signer_path_validation_pending NOTIFY signerPathValidationChanged)
    Q_PROPERTY(QString signerPathError MEMBER m_signer_path_error NOTIFY signerPathValidationChanged)
    Q_PROPERTY(bool listen READ listen WRITE setListen NOTIFY listenChanged)
    Q_PROPERTY(bool natpmp READ natpmp WRITE setNatpmp NOTIFY natpmpChanged)
    Q_PROPERTY(bool server READ server WRITE setServer NOTIFY serverChanged)
    Q_PROPERTY(int dbcacheSizeMiB MEMBER m_dbcache_size_mib NOTIFY dbcacheSizeMiBChanged)
    Q_PROPERTY(int minDbcacheSizeMiB MEMBER m_min_dbcache_size_mib CONSTANT)
    Q_PROPERTY(int maxDbcacheSizeMiB MEMBER m_max_dbcache_size_mib CONSTANT)
    Q_PROPERTY(int scriptThreads MEMBER m_script_threads NOTIFY scriptThreadsChanged)
    Q_PROPERTY(int minScriptThreads MEMBER m_min_script_threads CONSTANT)
    Q_PROPERTY(int maxScriptThreads MEMBER m_max_script_threads CONSTANT)
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
    Q_PROPERTY(int reducedStorageRequiredGB READ reducedStorageRequiredGB NOTIFY storageStatusChanged)
    Q_PROPERTY(int selectedStorageRequiredGB READ selectedStorageRequiredGB NOTIFY storageStatusChanged)
    Q_PROPERTY(bool storageEnoughForSelected READ storageEnoughForSelected NOTIFY storageStatusChanged)
    Q_PROPERTY(bool storageEnoughForFull READ storageEnoughForFull NOTIFY storageStatusChanged)
    Q_PROPERTY(bool storageEnoughForReduced READ storageEnoughForReduced NOTIFY storageStatusChanged)
    Q_PROPERTY(bool storageEnoughForCustom READ storageEnoughForCustom NOTIFY storageStatusChanged)
    Q_PROPERTY(QString thirdPartyTransactionUrls MEMBER m_third_party_transaction_urls NOTIFY thirdPartyTransactionUrlsChanged)
    Q_PROPERTY(QString moneyFontChoice MEMBER m_money_font_choice NOTIFY moneyFontChoiceChanged)
    Q_PROPERTY(bool hideAmounts MEMBER m_hide_amounts NOTIFY hideAmountsChanged)
    Q_PROPERTY(QString externalSignerPath MEMBER m_external_signer_path NOTIFY externalSignerPathChanged)
    Q_PROPERTY(QFont moneyFont READ moneyFont NOTIFY moneyFontChanged)

public:
    Q_INVOKABLE void refreshDisplaySettings() {}

    bool m_settings_ready{true};
    bool m_settings_pending{false};
    bool m_validation_pending{false};
    QString m_settings_error;
    bool m_signer_path_validation_pending{false};
    QString m_signer_path_error;
    Q_PROPERTY(bool holdSignerPathValidationForTest MEMBER m_hold_signer_validation)
    bool m_hold_signer_validation{false};
    Q_INVOKABLE void requestExternalSignerPathValidation(const QString&)
    {
        m_signer_path_validation_pending = m_hold_signer_validation;
        m_signer_path_error.clear();
        Q_EMIT signerPathValidationChanged();
    }
    Q_INVOKABLE void finishSignerPathValidationForTest(const QString& error)
    {
        m_signer_path_validation_pending = false;
        m_signer_path_error = error;
        Q_EMIT signerPathValidationChanged();
    }
    bool m_listen{true};
    bool m_natpmp{false};
    bool m_server{false};
    int m_dbcache_size_mib{450};
    int m_min_dbcache_size_mib{4};
    int m_max_dbcache_size_mib{std::numeric_limits<int>::max()};
    int m_script_threads{0};
    int m_min_script_threads{-8};
    int m_max_script_threads{15};
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
    bool m_hide_amounts{false};
    QString m_external_signer_path;
    QVariantMap m_core_setting_status_overrides;
    MockCoreSettingsModel m_core_settings;

    Q_PROPERTY(QString pruneTargetText READ pruneTargetText NOTIFY pruneTargetInputChanged)
    Q_PROPERTY(QString pruneTargetError READ pruneTargetError NOTIFY pruneTargetInputChanged)
    QString pruneTargetText()const{return m_core_settings.pruneTargetText();}
    QString pruneTargetError()const{return m_core_settings.pruneTargetError();}
    Q_INVOKABLE void editPruneTarget(const QString& text){m_core_settings.stagePruneLiteral(text);}
    std::shared_ptr<MockSettingsStore> m_settings_store;
    std::shared_ptr<ControlledSettings> m_controlled_settings;
    std::vector<QPointer<SettingsEditorModel>> m_controlled_settings_owners;
    MockWalletController* m_settings_signer{nullptr};
    void setSettingsSignerForTest(MockWalletController* signer){m_settings_signer=signer;}
    Q_PROPERTY(bool setupCommitPending READ setupCommitPending NOTIFY setupCommitChanged)
    Q_PROPERTY(bool setupCommitted READ setupCommitted NOTIFY setupCommitChanged)
    Q_PROPERTY(QString setupCommitError READ setupCommitError NOTIFY setupCommitChanged)
    Q_PROPERTY(bool setupPartial READ setupPartial NOTIFY setupCommitChanged)
    Q_PROPERTY(bool setupUncertain READ setupUncertain NOTIFY setupCommitChanged)
    bool m_setup_partial{false},m_setup_uncertain{false};
    int m_setup_queries{0},m_setup_completions{0},m_setup_rechecks{0};
    bool setupPartial()const{return m_setup_partial;}
    bool setupUncertain()const{return m_setup_uncertain;}
    Q_INVOKABLE void querySetupOutcome(){if(!m_setup_uncertain||m_setup_pending)return;++m_setup_queries;m_setup_pending=true;Q_EMIT setupCommitChanged();}
    Q_INVOKABLE void completeSavedSetup(){if(!m_setup_partial||m_setup_uncertain||m_setup_pending)return;++m_setup_completions;m_setup_pending=true;Q_EMIT setupCommitChanged();}
    Q_INVOKABLE void retrySetupChecks(){++m_setup_rechecks;}
    Q_INVOKABLE int setupQueriesForTest()const{return m_setup_queries;}
    Q_INVOKABLE int setupCompletionsForTest()const{return m_setup_completions;}
    Q_INVOKABLE int setupRechecksForTest()const{return m_setup_rechecks;}
    Q_INVOKABLE void projectSetupForTest(const QString& phase){
        m_setup_pending=phase==QStringLiteral("pending");m_setup_committed=phase==QStringLiteral("completed");
        m_setup_partial=phase==QStringLiteral("partial");m_setup_uncertain=phase==QStringLiteral("uncertain");
        m_setup_error=phase==QStringLiteral("failed")?QStringLiteral("Setup was not written."):m_setup_partial?QStringLiteral("Profile written; legacy cleanup incomplete."):m_setup_uncertain?QStringLiteral("Original outcome unknown."):QString{};
        Q_EMIT setupCommitChanged();if(m_setup_committed)Q_EMIT settingsCommitted();
    }
    Q_PROPERTY(QString unpruneConsentToken READ unpruneConsentToken CONSTANT)
    bool m_setup_pending{false},m_setup_committed{false};
    QString m_setup_error;
    bool setupCommitPending()const{return m_setup_pending;}
    bool setupCommitted()const{return m_setup_committed;}
    QString setupCommitError()const{return m_setup_error;}
    QString unpruneConsentToken()const{return QStringLiteral("mock-consent");}
    Q_INVOKABLE void confirmUnpruning(const QString&){}
    Q_INVOKABLE void commitSettings(){if(!canFinish()||m_setup_pending||m_setup_committed)return;m_setup_pending=true;Q_EMIT setupCommitChanged();QTimer::singleShot(0,this,[this]{m_setup_pending=false;m_setup_committed=true;Q_EMIT setupCommitChanged();Q_EMIT settingsCommitted();});}
    Q_INVOKABLE void resetSettingsEditorsForTest(){for(const auto& editor:MockSettingsOwners())if(editor)editor->close();m_settings_store.reset();m_setup_pending=m_setup_committed=m_setup_partial=m_setup_uncertain=false;m_setup_queries=m_setup_completions=m_setup_rechecks=0;m_setup_error={};Q_EMIT setupCommitChanged();}
    Q_INVOKABLE bool beginControlledSettingsForTest() {
        if (m_controlled_settings) return false;
        m_controlled_settings = std::make_shared<ControlledSettings>();
        return true;
    }
    Q_INVOKABLE bool stepControlledSettingsDomainForTest() {
        return m_controlled_settings && m_controlled_settings->transport.step(execution::Lane::Domain);
    }
    Q_INVOKABLE bool publishControlledSettingsForTest() {
        if (!m_controlled_settings) return false;
        auto& gui = m_controlled_settings->gui;
        for (int turn = 0; turn < 4096 && !gui.empty(); ++turn) {
            auto callback = std::move(gui.front()); gui.pop_front(); callback();
        }
        return gui.empty();
    }
    Q_INVOKABLE bool settleControlledSettingsForTest() {
        if (!m_controlled_settings) return false;
        auto& state = *m_controlled_settings;
        for (int turn = 0; turn < 4096; ++turn) {
            if (!state.transport.queuedIdle()) { if (!state.transport.stepNext()) return false; continue; }
            if (!state.gui.empty()) { auto callback = std::move(state.gui.front()); state.gui.pop_front(); callback(); continue; }
            return state.transport.transportIdle();
        }
        return false;
    }
    Q_INVOKABLE bool endControlledSettingsForTest() {
        if (!m_controlled_settings) return true;
        const auto owners = m_controlled_settings_owners;
        for (const auto& editor : owners) if (editor) editor->close();
        const bool settled = settleControlledSettingsForTest();
        const bool drained = m_controlled_settings->transport.quiescent() && m_controlled_settings->gui.empty();
        m_controlled_settings_owners.clear(); m_controlled_settings.reset();
        return settled && drained;
    }
    Q_INVOKABLE int settingsWritesForTest()const{if(!m_settings_store)return 0;std::lock_guard lock{m_settings_store->mutex};return m_settings_store->writes;}
    Q_INVOKABLE void rejectSettingsForTest(const QString& error){if(!m_settings_store)m_settings_store=std::make_shared<MockSettingsStore>();std::lock_guard lock{m_settings_store->mutex};m_settings_store->rejected=error;}
    Q_INVOKABLE SettingsEditorModel* createSettingsEditor(){
        using namespace effectivesettings;if(!m_settings_store)m_settings_store=std::make_shared<MockSettingsStore>();const auto store=m_settings_store;
        if(m_settings_signer){const auto config=m_settings_signer->signerConfigurationForTest();std::lock_guard lock{store->mutex};auto& field=store->snapshot.fields[size_t(Key::Signer)];field.committed=field.effective=field.next_start=config.command;field.editable=config.editable;field.provenance=config.editable?Provenance::Persistent:Provenance::CommandLine;}
        SettingsEditorModel::Ports ports;ports.execution=MockFeeServices();
        if (const auto controlled = m_controlled_settings) ports.execution = {controlled->transport.scheduler, [controlled](auto turn) { controlled->gui.push_back(std::move(turn)); }};
        const auto signer = QPointer<MockWalletController>{m_settings_signer};
        const auto notify = ports.execution.post;
        ports.read=[store]{std::lock_guard lock{store->mutex};return store->snapshot;};
        ports.review=[store](const settingsedit::Save& save){std::lock_guard lock{store->mutex};const bool current=save.original.revision==store->snapshot.revision;QString error=store->rejected;for(const auto& edit:save.batch.edits)if(edit.key==Key::Signer&&edit.intent==Intent::ExplicitOverride)if(const auto path_error=signerdiscovery::PathError(std::get<QString>(edit.value));!path_error.isEmpty())error=path_error;return Check{current&&error.isEmpty(),{},{},current?error:QStringLiteral("Configuration changed")};};
        ports.write=[store, signer, notify](const settingsedit::Save& save) {
            Result result; std::optional<QString> applied_signer;
            {
                std::lock_guard lock{store->mutex};
                if (save.original.revision != store->snapshot.revision) { result.outcome=Outcome::Conflict; result.error=QStringLiteral("Configuration changed"); return result; }
                ++store->writes;
                for (const auto& edit : save.batch.edits) {
                    auto& field=store->snapshot.fields[size_t(edit.key)]; field.has_override=edit.intent==Intent::ExplicitOverride;
                    field.committed=field.has_override?edit.value:store->configured[size_t(edit.key)]; field.next_start=field.committed;
                    if (field.applicability == Applicability::Live) field.effective=field.committed;
                    field.provenance=field.has_override?Provenance::Persistent:Provenance::Default;
                    result.persisted_values[edit.key]=field.has_override?std::make_optional(field.committed):std::nullopt;
                    if (edit.key == Key::Signer) applied_signer=std::get<QString>(field.committed);
                }
                store->snapshot.revision=QStringLiteral("settings-")+QString::number(store->writes); result.snapshot=store->snapshot;
                result.outcome=Outcome::Applied; result.persisted=result.write_completed=result.runtime_confirmed=true;
            }
            // Mirror the actual original write/forceSetting notification. A
            // later GUI draft or reconciliation cannot retarget this effect.
            if (applied_signer) notify([signer, command=std::move(*applied_signer)] { if (signer) signer->settingsSignerSavedForTest(command); });
            return result;
        };
        auto* editor=new SettingsEditorModel(std::move(ports),this);
        if (m_controlled_settings) m_controlled_settings_owners.emplace_back(editor);
        MockSettingsOwners().removeIf([](const auto& item){return !item;});MockSettingsOwners().push_back(editor);connect(editor,&SettingsEditorModel::closed,editor,[editor]{editor->close();editor->deleteLater();});return editor;
    }
Q_SIGNALS:
    void pruneTargetInputChanged();
    void setupCommitChanged();
    void settingsCommitted();
public:
    MockOptionsModel()
    {
        connect(&m_core_settings, &MockCoreSettingsModel::changed, this, &MockOptionsModel::syncCoreSettingsFromDocument);
        connect(&m_core_settings,&MockCoreSettingsModel::changed,this,&MockOptionsModel::pruneTargetInputChanged);
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
    bool canFinish() const { return m_preview_error.isEmpty() && !m_storage_check_pending && m_storage_error_text.isEmpty() && pruneTargetError().isEmpty() && storageEnoughForSelected(); }
    QString storageStatus() const
    {
        if (m_storage_check_pending) return QStringLiteral("checking");
        if (!m_storage_error_text.isEmpty()) return QStringLiteral("error");
        if (!m_storage_warning_text.isEmpty()) return QStringLiteral("warning");
        return QStringLiteral("ok");
    }
    QString storageAvailableText() const
    {
        return m_storage_check_pending ? QStringLiteral("Checking available storage…") : QStringLiteral("%1GB available").arg(m_storage_available_gb);
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
    int fullStorageRequiredGB() const { return storageChoices().full_required_gb; }
    bool prune() const { return m_core_settings.prune(); }
    void setPrune(bool value) { m_core_settings.setPrune(value); }
    int pruneSizeGB() const { return m_core_settings.pruneSizeGB(); }
    void setPruneSizeGB(int value) { m_core_settings.setPruneSizeGB(value); }
    int prunedStorageRequiredGB() const { return storageChoices().pruned_required_gb; }
    int reducedStorageRequiredGB()const{return storageChoices().reduced_required_gb;}
    int selectedStorageRequiredGB() const { return storageChoices().selected_required_gb; }
    bool storageEnoughForSelected() const { return storageChoices().enough_for_selected; }
    bool storageEnoughForFull() const { return storageChoices().enough_for_full; }
    QmlOnboardingStorage::Info storageChoices()const{QmlOnboardingStorage::State state;state.result_valid=!m_storage_check_pending&&m_storage_error_text.isEmpty();state.available_bytes=static_cast<uint64_t>(std::max(0,m_storage_available_gb))*GB_BYTES;state.assumed_blockchain_size_gb=m_assumed_blockchain_size;state.assumed_chainstate_size_gb=m_assumed_chainstate_size;state.prune=prune();state.prune_size_gb=pruneSizeGB();state.existing_profile=m_existing_profile;state.manual_pruning=m_core_settings.m_target_mib==1;state.custom_manual_pruning=m_core_settings.customTarget()==1;state.custom_prune_size_gb=m_core_settings.customStorageSizeGB();return QmlOnboardingStorage::Evaluate(state);}
    bool storageEnoughForReduced()const{return storageChoices().enough_for_reduced;}
    bool storageEnoughForCustom()const{return storageChoices().enough_for_custom;}
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
    Q_INVOKABLE QString externalSignerPathValidationError(const QString&) const { return {}; }
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
    Q_INVOKABLE MockOptionsTransactionLinks* createTransactionLinks() {
        auto* view = new MockOptionsTransactionLinks(this, [this](const QString& txid) { return preparedTransactionLinks(txid); });
        connect(this, &MockOptionsModel::thirdPartyTransactionUrlsChanged, view, &MockOptionsTransactionLinks::refresh);
        return view;
    }
    QVariantList preparedTransactionLinks(const QString& txid) const {
        QVariantList result;
        if (m_third_party_transaction_urls.isEmpty()) return result;
        // Build the link from the configured template, like the real model does,
        // so a test sees the setting change rather than a fixed host.
        QString url = m_third_party_transaction_urls.section(QLatin1Char('|'), 0, 0).trimmed();
        url.replace(QStringLiteral("%s"), txid);
        QVariantMap link;
        link.insert("host", QUrl(url).host());
        link.insert("url", url);
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
    void settingsReadyChanged();
    void settingsPendingChanged();
    void settingsErrorChanged();
    void validationPendingChanged();
    void signerPathValidationChanged();
    void dataDirSelectionFinished(bool success, const QString& error);
    void listenChanged();
    void natpmpChanged();
    void serverChanged();
    void dbcacheSizeMiBChanged();
    void scriptThreadsChanged();
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
    void hideAmountsChanged();
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
    Q_PROPERTY(QString networkName MEMBER m_network_name CONSTANT)

public:
    int m_assumed_chainstate_size{12};
    int m_assumed_blockchain_size{610};
    QString m_network_name{QStringLiteral("REGTEST")};
};

class MockBlockClockModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qreal currentTimeFraction MEMBER m_current_time_fraction NOTIFY currentTimeFractionChanged)
    Q_PROPERTY(QList<qreal> blockTimeFractions MEMBER m_block_time_fractions NOTIFY blockTimeFractionsChanged)
    Q_PROPERTY(QVariant blockSnapshot READ blockSnapshot NOTIFY blockTimeFractionsChanged)
    Q_PROPERTY(QString historyError READ historyError CONSTANT)
    Q_PROPERTY(bool historyPending READ historyPending CONSTANT)

public:
    QVariant blockSnapshot() const { auto value = std::make_shared<blockclock::Projection>(); value->fractions = m_block_time_fractions; m_snapshot = std::move(value); return QVariant::fromValue(blockclock::WeakSnapshot{m_snapshot}); }
    QString historyError() const { return {}; }
    bool historyPending() const { return false; }
    Q_INVOKABLE void refreshHistory() {}
    mutable blockclock::Snapshot m_snapshot;
    qreal m_current_time_fraction{0.25};
    QList<qreal> m_block_time_fractions{0.1, 0.2};

Q_SIGNALS:
    void currentTimeFractionChanged();
    void blockTimeFractionsChanged();
};

class MockNodeLifecycle : public QObject {
    Q_OBJECT
public:
    enum Phase { Constructed,Initializing,Ready,StartupFailed,Stopping,Draining,Stopped,Fatal };
    Q_ENUM(Phase)
};
class MockRuntimePrompt : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString title MEMBER title CONSTANT)
    Q_PROPERTY(QString message MEMBER message CONSTANT)
    Q_PROPERTY(QString icon MEMBER icon CONSTANT)
    Q_PROPERTY(unsigned int buttons MEMBER buttons CONSTANT)
    Q_PROPERTY(bool question MEMBER question CONSTANT)
    Q_PROPERTY(QAbstractItemModel* diagnostics READ diagnostics CONSTANT)
public:
    using QObject::QObject;
    QString title,message,icon;unsigned int buttons{0};bool question{false};
    std::function<void()> complete;
    std::function<void(unsigned int)> answered;
    QAbstractItemModel* diagnostics()const{return nullptr;}
    Q_INVOKABLE bool presented(){return true;}
    Q_INVOKABLE bool answer(unsigned int button){
        if(!button||(button&(button-1))||(button&buttons)!=button)return false;
        if(answered)answered(button);else if(complete)complete();
        return true;
    }
    Q_INVOKABLE bool unavailable(){if(complete)complete();
        return true;}
};

class MockNodeModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(MockRuntimePrompt* runtimePrompt READ runtimePrompt NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QVariantList runtimePrompts READ runtimePrompts NOTIFY runtimeDialogChanged)
    Q_PROPERTY(int lifecyclePhase MEMBER m_lifecycle_phase NOTIFY lifecyclePhaseChanged)
    Q_PROPERTY(bool networkActionReady MEMBER m_network_action_ready NOTIFY networkActionReadyChanged)
    Q_PROPERTY(bool networkActionPending MEMBER m_network_action_pending NOTIFY networkActionPendingChanged)
    Q_PROPERTY(QString networkActionError MEMBER m_network_action_error NOTIFY networkActionErrorChanged)
    Q_PROPERTY(bool pause MEMBER m_pause NOTIFY pauseChanged)
    Q_PROPERTY(int numPeers MEMBER m_num_peers NOTIFY numPeersChanged)
    Q_PROPERTY(int numInboundPeers MEMBER m_num_inbound_peers NOTIFY numInboundPeersChanged)
    Q_PROPERTY(int numOutboundPeers MEMBER m_num_outbound_peers NOTIFY numOutboundPeersChanged)
    Q_PROPERTY(int maxNumOutboundPeers MEMBER m_max_num_outbound_peers NOTIFY maxNumOutboundPeersChanged)
    Q_PROPERTY(double verificationProgress MEMBER m_verification_progress NOTIFY verificationProgressChanged)
    Q_PROPERTY(bool initialSyncComplete MEMBER m_initial_sync_complete NOTIFY initialSyncCompleteChanged)
    Q_PROPERTY(int remainingSyncTime MEMBER m_remaining_sync_time NOTIFY remainingSyncTimeChanged)
    Q_PROPERTY(bool headerSyncActive MEMBER m_header_sync_active NOTIFY headerSyncChanged)
    Q_PROPERTY(bool blockSyncActive MEMBER m_block_sync_active NOTIFY blockSyncActiveChanged)
    Q_PROPERTY(bool headerPresync MEMBER m_header_presync NOTIFY headerSyncChanged)
    Q_PROPERTY(double headerSyncProgress MEMBER m_header_sync_progress NOTIFY headerSyncChanged)
    Q_PROPERTY(bool faulted MEMBER m_faulted NOTIFY faultedChanged)
    Q_PROPERTY(QAbstractItemModel* startupDiagnostics READ startupDiagnostics CONSTANT)
    Q_PROPERTY(QAbstractItemModel* runtimeDialogDiagnostics READ runtimeDialogDiagnostics NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString startupError MEMBER m_startup_error NOTIFY startupErrorChanged)
    Q_PROPERTY(bool fatalException MEMBER m_fatal_exception NOTIFY fatalExceptionChanged)
    Q_PROPERTY(bool shutdownRequested MEMBER m_shutdown_requested NOTIFY shutdownRequestedChanged)
    Q_PROPERTY(bool pruneMode MEMBER m_prune_mode NOTIFY pruneModeChanged)
    Q_PROPERTY(QString warnings MEMBER m_warnings NOTIFY warningsChanged)
    Q_PROPERTY(QStringList warningList MEMBER m_warning_list NOTIFY warningsChanged)
    Q_PROPERTY(bool hasWarnings READ hasWarnings NOTIFY warningsChanged)
    Q_PROPERTY(bool runtimeDialogVisible MEMBER m_runtime_dialog_visible NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogTitle MEMBER m_runtime_dialog_title NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogMessage MEMBER m_runtime_dialog_message NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogIcon MEMBER m_runtime_dialog_icon NOTIFY runtimeDialogChanged)
    Q_PROPERTY(unsigned int runtimeDialogButtons MEMBER m_runtime_dialog_buttons NOTIFY runtimeDialogChanged)
    Q_PROPERTY(bool runtimeDialogQuestion MEMBER m_runtime_dialog_question NOTIFY runtimeDialogChanged)
    Q_PROPERTY(unsigned int lastRuntimeDialogAnswer MEMBER m_last_runtime_dialog_answer NOTIFY runtimeDialogChanged)
    Q_PROPERTY(int blockTipHeight MEMBER m_block_tip_height NOTIFY blockTipHeightChanged)
    Q_PROPERTY(int mempoolTransactionCount MEMBER m_mempool_transaction_count NOTIFY mempoolInfoChanged)
    Q_PROPERTY(double mempoolUsageMB MEMBER m_mempool_usage_mb NOTIFY mempoolInfoChanged)
    Q_PROPERTY(double mempoolMaxUsageMB MEMBER m_mempool_max_usage_mb NOTIFY mempoolInfoChanged)
    Q_PROPERTY(bool mempoolInfoPollingActive READ mempoolInfoPollingActive WRITE setMempoolInfoPollingActive NOTIFY mempoolInfoPollingActiveChanged)
    Q_PROPERTY(bool mempoolInformationAvailable MEMBER m_mempool_information_available NOTIFY mempoolInformationAvailableChanged)
    Q_PROPERTY(bool disconnectPeerResult MEMBER m_disconnect_peer_result NOTIFY peerActionStateChanged)
    Q_PROPERTY(bool banPeerResult MEMBER m_ban_peer_result NOTIFY peerActionStateChanged)
    Q_PROPERTY(QVariantList informationRows READ nodeInformationRows NOTIFY peerActionStateChanged)
    Q_PROPERTY(bool autoCompletePeerAction MEMBER m_auto_complete_peer_action)
    Q_PROPERTY(bool autoCompletePeerQuery MEMBER m_auto_complete_peer_query)
    Q_PROPERTY(int peerQueryCalls READ peerQueryCalls NOTIFY peerCommandStateChanged)
    Q_PROPERTY(int lastBanDuration MEMBER m_last_ban_duration NOTIFY peerActionCallsChanged)
    Q_PROPERTY(int disconnectPeerCalls READ disconnectPeerCalls NOTIFY peerActionCallsChanged)
    Q_PROPERTY(int banPeerCalls READ banPeerCalls NOTIFY peerActionCallsChanged)
    Q_PROPERTY(int lastDisconnectedNodeId MEMBER m_last_disconnected_node_id NOTIFY peerActionCallsChanged)
    Q_PROPERTY(QString lastBannedAddress MEMBER m_last_banned_address NOTIFY peerActionCallsChanged)

public:
    enum class PeerActionOutcome { Succeeded, Rejected, PartiallyApplied, Uncertain };
    Q_ENUM(PeerActionOutcome)
    QAbstractItemModel* startupDiagnostics() const { return nullptr; }
    QAbstractItemModel* runtimeDialogDiagnostics() const { return nullptr; }

    int m_lifecycle_phase{MockNodeLifecycle::Constructed};
    MockRuntimePrompt* m_runtime_prompt{nullptr};
    MockRuntimePrompt* runtimePrompt()const{return m_runtime_prompt;}
    QVariantList runtimePrompts()const{return m_runtime_prompt?QVariantList{QVariant::fromValue(m_runtime_prompt)}:QVariantList{};}
    bool m_network_action_ready{true};
    bool m_network_action_pending{false};
    QString m_network_action_error;
    int m_last_disconnected_node_id{-1};
    QString m_last_banned_address;
    bool m_pause{false};
    int m_num_peers{0};
    int m_num_inbound_peers{0};
    int m_num_outbound_peers{0};
    int m_max_num_outbound_peers{8};
    double m_verification_progress{0.0};
    bool m_initial_sync_complete{false};
    int m_remaining_sync_time{0};
    bool m_header_sync_active{false};
    bool m_block_sync_active{false};
    bool m_header_presync{false};
    double m_header_sync_progress{0.0};
    bool m_faulted{false};
    QString m_startup_error;
    bool m_fatal_exception{false};
    bool m_shutdown_requested{false};
    bool m_prune_mode{false};
    QString m_warnings;
    QStringList m_warning_list;
    bool m_runtime_dialog_visible{false};
    QString m_runtime_dialog_title;
    QString m_runtime_dialog_message;
    QString m_runtime_dialog_icon{QStringLiteral("image://images/info-filled")};
    unsigned int m_runtime_dialog_buttons{0};
    bool m_runtime_dialog_question{false};
    unsigned int m_last_runtime_dialog_answer{0};
    int m_block_tip_height{0};
    int m_mempool_transaction_count{0};
    double m_mempool_usage_mb{0.0};
    double m_mempool_max_usage_mb{300.0};
    bool m_mempool_info_polling_active{false};
    bool m_mempool_information_available{true};
    bool m_disconnect_peer_result{true};
    bool m_ban_peer_result{true};
    bool m_auto_complete_peer_action{true}, m_auto_complete_peer_query{true};
    bool m_typed_peer_pending{false}, m_peer_commands_closed{false};
    int m_peer_query_calls{0}, m_last_ban_duration{0};
    QHash<QString, std::shared_ptr<MockPeerOperationState>> m_typed_peer_operations;
    std::function<void()> m_refresh_peer_inventories;
    int peerQueryCalls() const { return m_peer_query_calls; }
    bool canStartPeerCommand() const { return !m_peer_commands_closed && m_network_action_ready && !m_typed_peer_pending; }
    Q_INVOKABLE void closePeerCommandAdmission() { m_peer_commands_closed = true; Q_EMIT peerCommandStateChanged(); }
    MockPeerCommandOperation* preparePeerCommand(MockPeerDetailsModel* details, int duration);
    bool confirmPeerCommand(std::shared_ptr<MockPeerOperationState> original);
    Q_INVOKABLE void completePeerOperation(const QString& id) {
        const auto original = m_typed_peer_operations.value(id);
        if (original && original->ticket) completePeerAction(original->ticket);
    }
    Q_INVOKABLE void completePeerOperationWithOutcome(const QString& id, int outcome, const QString& error) {
        const auto original = m_typed_peer_operations.value(id);
        if (original && original->ticket) completePeerActionWithOutcome(original->ticket, outcome, error);
    }
    Q_INVOKABLE void completePeerQuery(const QString& id, bool connected, bool banned, const QString& error) {
        const auto original = m_typed_peer_operations.value(id);
        if (!original || !original->query_pending) return;
        original->query_pending = false; original->pending = false;
        original->result.queryError = error;
        original->result.observationAvailable = error.isEmpty(); original->result.targetConnected = connected;
        original->result.banObservationAvailable = error.isEmpty(); original->result.targetBanned = banned;
        m_typed_peer_pending = false;
        if (original->notify) original->notify();
        Q_EMIT peerCommandStateChanged();
    }
    Q_INVOKABLE void repeatPeerOperationNotification(const QString& id) {
        const auto original = m_typed_peer_operations.value(id);
        if (!original || !original->ticket || !original->result.present) return;
        Q_EMIT peerActionFinished(original->ticket, static_cast<int>(original->result.outcome), original->result.errorText);
    }
    bool reconcilePeerCommand(std::shared_ptr<MockPeerOperationState> original) {
        if (original->pending || !original->result.present || original->result.outcomeKnown) return false;
        if (!canStartPeerCommand()) {
            original->result.queryError = QStringLiteral("The node is busy or closing. The original peer state was not refreshed.");
            if (original->notify) original->notify();
            return false;
        }
        original->query_pending = original->pending = m_typed_peer_pending = true; ++m_peer_query_calls;
        if (original->notify) original->notify();
        Q_EMIT peerCommandStateChanged();
        if (m_auto_complete_peer_query) QTimer::singleShot(0, this, [this, id = original->id] { completePeerQuery(id, true, false, {}); });
        return true;
    }
    quint64 m_peer_ticket{0};
    struct PeerOutcome { PeerActionOutcome outcome; QString error; };
    QMap<quint64, PeerOutcome> m_peer_outcomes;
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
        m_last_runtime_dialog_answer = button;
        m_runtime_dialog_visible = false;
        m_runtime_prompt = nullptr;
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
        auto prompt=new MockRuntimePrompt(this);prompt->title=title;prompt->message=message;
        prompt->icon=m_runtime_dialog_icon;prompt->buttons=buttons;prompt->question=question;
        prompt->complete=[this,prompt]{if(m_runtime_prompt!=prompt)return;m_runtime_prompt=nullptr;m_runtime_dialog_visible=false;Q_EMIT runtimeDialogChanged();};
        prompt->answered=[this,prompt](unsigned int button){if(m_runtime_prompt!=prompt)return;m_last_runtime_dialog_answer=button;m_runtime_prompt=nullptr;m_runtime_dialog_visible=false;Q_EMIT runtimeDialogChanged();};
        m_runtime_prompt=prompt;
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
        m_auto_complete_peer_action = true;
        m_peer_outcomes.clear();
        m_typed_peer_operations.clear(); m_typed_peer_pending = false; m_peer_commands_closed = false;
        m_auto_complete_peer_query = true; m_peer_query_calls = 0; m_last_ban_duration = 0;
        m_network_action_ready = true;
        Q_EMIT peerCommandStateChanged();
        Q_EMIT networkActionReadyChanged();
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
        m_last_disconnected_node_id = node_id;
        ++m_disconnect_peer_calls;
        Q_EMIT peerActionCallsChanged();
        return m_disconnect_peer_result;
    }
    Q_INVOKABLE quint64 requestDisconnectPeer(qint64 node_id)
    {
        const bool success = disconnectPeer(node_id);
        return queuePeerOutcome(success, success ? QString{} : QStringLiteral("The peer could not be disconnected. It may already be disconnected."));
    }
    Q_INVOKABLE quint64 requestBanPeer(const QString& address, qint64 duration)
    {
        const bool success = banPeer(address, duration);
        return queuePeerOutcome(success, success ? QString{} : QStringLiteral("The peer could not be banned."));
    }
    quint64 queuePeerOutcome(bool success, QString error)
    {
        const auto ticket = ++m_peer_ticket;
        m_peer_outcomes.insert(ticket, {success ? PeerActionOutcome::Succeeded : PeerActionOutcome::Rejected, std::move(error)});
        if (m_auto_complete_peer_action) QTimer::singleShot(0, this, [this, ticket] { completePeerAction(ticket); });
        return ticket;
    }
    Q_INVOKABLE void completePeerAction(quint64 ticket)
    {
        const auto found = m_peer_outcomes.find(ticket);
        if (found == m_peer_outcomes.end()) return;
        auto result = std::move(found.value());
        m_peer_outcomes.erase(found);
        // Only a terminal receipt belonging to an admitted typed operation
        // drives composition refresh. Arbitrary legacy signals have no owner.
        std::shared_ptr<MockPeerOperationState> original;
        for (const auto& candidate : std::as_const(m_typed_peer_operations)) {
            if (candidate->ticket == ticket && !candidate->result.present) { original = candidate; break; }
        }
        if (original) {
            auto& value = original->result;
            value.operationId = original->id; value.connectionId = original->connection;
            value.nodeIdText = original->node_id; value.address = original->address;
            value.outcome = static_cast<MockPeerCommandOperation::Outcome>(result.outcome);
            value.present = true; value.outcomeKnown = result.outcome != PeerActionOutcome::Uncertain;
            value.errorText = result.error; value.mutationAttempted = true;
            value.banApplied = original->duration > 0 && (result.outcome == PeerActionOutcome::Succeeded || result.outcome == PeerActionOutcome::PartiallyApplied);
            value.disconnectConfirmed = result.outcome == PeerActionOutcome::Succeeded;
            original->pending = false; m_typed_peer_pending = false;
            if (!m_peer_commands_closed && m_refresh_peer_inventories && result.outcome != PeerActionOutcome::Rejected) m_refresh_peer_inventories();
            if (original->notify) original->notify();
            Q_EMIT peerCommandStateChanged();
        }
        Q_EMIT peerActionFinished(ticket, static_cast<int>(result.outcome), result.error);
    }
    Q_INVOKABLE void completePeerActionWithOutcome(quint64 ticket, int outcome, const QString& error)
    {
        const auto found = m_peer_outcomes.find(ticket);
        if (found == m_peer_outcomes.end() || outcome < 0 || outcome > static_cast<int>(PeerActionOutcome::Uncertain)) return;
        found->outcome = static_cast<PeerActionOutcome>(outcome); found->error = error;
        completePeerAction(ticket);
    }
    Q_INVOKABLE bool banPeer(const QString& raw_address, qint64 ban_duration)
    {
        m_last_banned_address = raw_address;
        m_last_ban_duration = static_cast<int>(ban_duration);
        ++m_ban_peer_calls;
        Q_EMIT peerActionCallsChanged();
        return m_ban_peer_result;
    }

Q_SIGNALS:
    void peerCommandStateChanged();
    void lifecyclePhaseChanged();
    void peerActionFinished(quint64 ticket, int outcome, const QString& error);
    void networkActionReadyChanged();
    void networkActionPendingChanged();
    void networkActionErrorChanged();
    void requestedShutdown();
    void pauseChanged();
    void numPeersChanged();
    void numInboundPeersChanged();
    void numOutboundPeersChanged();
    void maxNumOutboundPeersChanged();
    void verificationProgressChanged();
    void initialSyncCompleteChanged();
    void remainingSyncTimeChanged();
    void headerSyncChanged();
    void blockSyncActiveChanged();
    void faultedChanged();
    void startupErrorChanged();
    void fatalExceptionChanged();
    void shutdownRequestedChanged();
    void pruneModeChanged();
    void warningsChanged();
    void runtimeDialogChanged();
    void blockTipHeightChanged();
    void mempoolInfoChanged();
    void mempoolInfoPollingActiveChanged(bool active);
    void mempoolInformationAvailableChanged();
    void peerActionStateChanged();
    void peerActionCallsChanged();
};

QString MockPeerCommandOperation::operationId() const { return m_original->id; }
QString MockPeerCommandOperation::connectionId() const { return m_original->connection; }
QString MockPeerCommandOperation::nodeIdText() const { return m_original->node_id; }
QString MockPeerCommandOperation::address() const { return m_original->address; }
int MockPeerCommandOperation::banDuration() const { return m_original->duration; }
bool MockPeerCommandOperation::confirmationRequired() const { return !m_retired && !m_original->confirmed; }
bool MockPeerCommandOperation::pending() const { return m_original->pending; }
bool MockPeerCommandOperation::queryPending() const { return m_original->query_pending; }
bool MockPeerCommandOperation::resultPresent() const { return m_original->result.present; }
bool MockPeerCommandOperation::outcomeKnown() const { return m_original->result.outcomeKnown; }
QString MockPeerCommandOperation::errorText() const { return m_original->result.errorText; }
QString MockPeerCommandOperation::queryError() const { return m_original->result.queryError; }
MockPeerCommandResult MockPeerCommandOperation::result() const { return m_original->result; }
void MockPeerCommandOperation::refresh() { if (!m_retired && m_node && !m_node->m_peer_commands_closed) Q_EMIT changed(); }
void MockPeerCommandOperation::retire() { if (m_retired) return; m_retired = true; cancelOriginal(); }
MockPeerCommandOperation::MockPeerCommandOperation(MockNodeModel* node, std::shared_ptr<MockPeerOperationState> original)
    : m_node(node), m_original(std::move(original))
{
    const QPointer<MockPeerCommandOperation> observer{this};
    m_original->notify = [observer] { if (observer) observer->refresh(); };
    QQmlEngine::setObjectOwnership(this, QQmlEngine::JavaScriptOwnership);
}
bool MockPeerCommandOperation::confirm()
{
    if (m_retired || m_original->confirmed) return false;
    return m_node && m_node->confirmPeerCommand(m_original);
}
bool MockPeerCommandOperation::cancelOriginal()
{
    if (m_original->confirmed) return false;
    m_original->confirmed = true;
    auto& result = m_original->result;
    result.operationId = operationId(); result.connectionId = connectionId(); result.nodeIdText = nodeIdText(); result.address = address();
    result.present = result.outcomeKnown = true; result.outcome = MockPeerCommandOperation::Outcome::Cancelled;
    return true;
}
bool MockPeerCommandOperation::cancel()
{
    if (m_retired || !cancelOriginal()) return false;
    refresh(); return true;
}
bool MockPeerCommandOperation::reconcile() { return !m_retired && m_node && m_node->reconcilePeerCommand(m_original); }
MockPeerCommandOperation* MockNodeModel::preparePeerCommand(MockPeerDetailsModel* details, int duration)
{
    if (!details || !details->connected()) return nullptr;
    auto original = std::make_shared<MockPeerOperationState>();
    original->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    original->connection = details->connectionId(); original->node_id = details->nodeIdText();
    original->address = details->m_address; original->raw_address = details->m_raw_address;
    original->duration = duration; original->target = details;
    m_typed_peer_operations.insert(original->id, original);
    return new MockPeerCommandOperation(this, std::move(original));
}
bool MockNodeModel::confirmPeerCommand(std::shared_ptr<MockPeerOperationState> original)
{
    if (original->confirmed) return false;
    original->confirmed = true;
    if (!canStartPeerCommand() || !original->target || !original->target->connected()
        || original->target->connectionId() != original->connection) {
        auto& result = original->result;
        result.operationId = original->id; result.connectionId = original->connection;
        result.nodeIdText = original->node_id; result.address = original->address;
        result.present = result.outcomeKnown = true; result.outcome = MockPeerCommandOperation::Outcome::Rejected;
        result.errorText = QStringLiteral("The original peer command was not admitted. The node is busy, closing, or the connection has ended.");
        if (original->notify) original->notify();
        return false;
    }
    original->pending = m_typed_peer_pending = true;
    // Reuse the fixture's existing single mutation/effect and outcome ledger.
    original->ticket = original->duration > 0 ? requestBanPeer(original->raw_address, original->duration)
        : requestDisconnectPeer(original->node_id.toLongLong());
    if (original->notify) original->notify();
    Q_EMIT peerCommandStateChanged();
    return true;
}
bool MockPeerDetailsModel::canDisconnect() const { return connected() && m_commands && m_commands->canStartPeerCommand(); }
void MockPeerDetailsModel::setCommands(MockNodeModel* node)
{
    m_commands = node;
    connect(node, &MockNodeModel::peerCommandStateChanged, this, &MockPeerDetailsModel::dataChanged);
    connect(node, &MockNodeModel::networkActionReadyChanged, this, &MockPeerDetailsModel::dataChanged);
}
MockPeerCommandOperation* MockPeerDetailsModel::prepareDisconnect() { return m_commands ? m_commands->preparePeerCommand(this, 0) : nullptr; }
MockPeerCommandOperation* MockPeerDetailsModel::prepareBan(MockPeerCommandOperation::BanDuration duration) { return m_commands ? m_commands->preparePeerCommand(this, static_cast<int>(duration)) : nullptr; }

class MockPeerTableModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool autoRefreshActive MEMBER m_auto_refresh_active NOTIFY autoRefreshActiveChanged)
    Q_PROPERTY(int refreshCalls READ refreshCalls NOTIFY refreshCallsChanged)

public:
    Q_INVOKABLE void startAutoRefresh()
    {
        if (m_auto_refresh_active) return;
        m_auto_refresh_active = true;
        Q_EMIT autoRefreshActiveChanged();
    }
    Q_INVOKABLE void stopAutoRefresh()
    {
        if (!m_auto_refresh_active) return;
        m_auto_refresh_active = false;
        Q_EMIT autoRefreshActiveChanged();
    }
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
    void autoRefreshActiveChanged();

private:
    int m_refresh_calls{0};
    bool m_auto_refresh_active{false};
};

class MockNetworkTrafficTower : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(quint64 totalBytesReceived MEMBER m_total_bytes_received NOTIFY totalBytesReceivedChanged)
    Q_PROPERTY(quint64 totalBytesSent MEMBER m_total_bytes_sent NOTIFY totalBytesSentChanged)
    Q_PROPERTY(double maxReceivedRateBps MEMBER m_max_received_rate_bps NOTIFY maxReceivedRateBpsChanged)
    Q_PROPERTY(double maxSentRateBps MEMBER m_max_sent_rate_bps NOTIFY maxSentRateBpsChanged)
    Q_PROPERTY(QQueue<float> receivedRateList MEMBER m_received_rate_list NOTIFY receivedRateListChanged)
    Q_PROPERTY(QQueue<float> sentRateList MEMBER m_sent_rate_list NOTIFY sentRateListChanged)
    Q_PROPERTY(int lastFilterWindowSize MEMBER m_last_filter_window_size NOTIFY lastFilterWindowSizeChanged)

public:
    MockNetworkTrafficTower()
    {
        m_received_rate_list << 10.0f << 20.0f << 30.0f;
        m_sent_rate_list << 15.0f << 25.0f << 35.0f;
    }

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
    QQueue<float> m_received_rate_list;
    QQueue<float> m_sent_rate_list;
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
    Q_PROPERTY(bool sortAscending MEMBER m_sort_ascending NOTIFY sortAscendingChanged)
    Q_PROPERTY(QString searchText MEMBER m_search_text NOTIFY searchTextChanged)
    Q_PROPERTY(QStringList directionFilters MEMBER m_direction_filters NOTIFY directionFiltersChanged)
    Q_PROPERTY(QStringList connectionTypeFilters MEMBER m_connection_type_filters NOTIFY connectionTypeFiltersChanged)
    Q_PROPERTY(QStringList networkFilters MEMBER m_network_filters NOTIFY networkFiltersChanged)
    Q_PROPERTY(QStringList transportFilters MEMBER m_transport_filters NOTIFY transportFiltersChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        NodeIdRole = Qt::UserRole,
        AddressRole,
        SubversionRole,
        DirectionRole,
        ConnectionTypeRole,
        NetworkRole,
        TransportRole,
        SentRole,
        ReceivedRole,
        ConnectionIdRole,
        NodeIdTextRole,
    };

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
        return m_peer_count;
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_peer_count) return {};
        switch (role) {
        case NodeIdRole: { const auto* details = peerDetailsAt(index.row()); return details ? QVariant::fromValue(details->m_node_id) : QVariant{}; }
        case NodeIdTextRole: { const auto* details = peerDetailsAt(index.row()); return details ? details->nodeIdText() : QString{}; }
        case ConnectionIdRole: { const auto* details = peerDetailsAt(index.row()); return details ? details->connectionId() : QString{}; }
        case AddressRole: { const auto* details = peerDetailsAt(index.row()); return details ? details->m_address : QString{}; }
        case SubversionRole: return QStringLiteral("/Satoshi:test/");
        case DirectionRole: return QStringLiteral("Outbound");
        case ConnectionTypeRole: return QStringLiteral("Full relay");
        case NetworkRole: return QStringLiteral("IPv4");
        case TransportRole: return QStringLiteral("v2");
        case SentRole: return QStringLiteral("1 kB");
        case ReceivedRole: return QStringLiteral("2 kB");
        default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {NodeIdRole, "nodeId"},
            {AddressRole, "address"},
            {SubversionRole, "subversion"},
            {DirectionRole, "direction"},
            {ConnectionTypeRole, "connectionType"},
            {NetworkRole, "network"},
            {TransportRole, "transport"},
            {SentRole, "sent"},
            {ReceivedRole, "received"},
            {ConnectionIdRole, "connectionId"},
            {NodeIdTextRole, "nodeIdText"},
        };
    }

    Q_INVOKABLE void setPeerCountForTest(int count)
    {
        if (m_peer_count == count) return;
        beginResetModel();
        m_peer_count = std::clamp(count, 0, 2);
        endResetModel();
        Q_EMIT countChanged();
    }

    Q_INVOKABLE MockPeerDetailsModel* peerDetailsAt(int row) const
    {
        return row >= 0 && row < m_peer_count ? row == 1 ? m_other_details : m_peer_details : nullptr;
    }
    void setPeerDetailsForTest(MockPeerDetailsModel* first, MockPeerDetailsModel* second) { m_peer_details = m_default_details = first; m_other_details = second; }
    Q_INVOKABLE void resetPeerInventoryForTest() {
        beginResetModel(); m_peer_count = 0; m_peer_details = m_default_details; endResetModel(); Q_EMIT countChanged();
    }
    Q_INVOKABLE void replaceFirstConnectionForTest() {
        auto* replacement = new MockPeerDetailsModel(this);
        replacement->m_node_id = m_peer_details->m_node_id;
        replacement->m_address = m_peer_details->m_address;
        replacement->m_raw_address = m_peer_details->m_raw_address;
        replacement->setCommands(m_peer_details->m_commands);
        beginResetModel(); m_peer_details->triggerDisconnected(); m_peer_details = replacement; endResetModel(); Q_EMIT countChanged();
    }

    Q_INVOKABLE int indexOfNodeId(qint64 node_id) const
    {
        for (int row = 0; row < m_peer_count; ++row) if (auto* details = peerDetailsAt(row); details && details->m_node_id == node_id) return row;
        return -1;
    }

Q_SIGNALS:
    void sortByChanged(const QString& roleName);
    void sortAscendingChanged();
    void searchTextChanged();
    void directionFiltersChanged();
    void connectionTypeFiltersChanged();
    void networkFiltersChanged();
    void transportFiltersChanged();
    void countChanged();
    void dataChanged(int startIndex, int endIndex);

private:
    int m_peer_count{0};
    MockPeerDetailsModel* m_peer_details{nullptr};
    MockPeerDetailsModel* m_other_details{nullptr};
    MockPeerDetailsModel* m_default_details{nullptr};
    QString m_sort_by{QStringLiteral("nodeId")};
    bool m_sort_ascending{true};
    QString m_search_text;
    QStringList m_direction_filters;
    QStringList m_connection_type_filters;
    QStringList m_network_filters;
    QStringList m_transport_filters;
};

class MockPeerSelectionModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(MockPeerListModelProxy* source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(bool autoSelectFirst READ autoSelectFirst WRITE setAutoSelectFirst NOTIFY autoSelectFirstChanged)
    Q_PROPERTY(MockPeerDetailsModel* selectedDetails READ selectedDetails NOTIFY changed)
    Q_PROPERTY(QString connectionId READ connectionId NOTIFY changed)
    Q_PROPERTY(QString nodeIdText READ nodeIdText NOTIFY changed)
    Q_PROPERTY(qint64 nodeId READ nodeId NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(Phase phase READ phase NOTIFY changed)
public:
    enum Phase { NoSelection, Connected, Disconnected, Stopped };
    Q_ENUM(Phase)
    explicit MockPeerSelectionModel(QObject* parent = nullptr) : QObject(parent) {}
    MockPeerListModelProxy* source() const { return m_source; }
    void setSource(MockPeerListModelProxy* value) {
        if (m_source == value) return;
        if (m_source) disconnect(m_source, nullptr, this, nullptr);
        clear(); m_source = value;
        if (value) {
            connect(value, &MockPeerListModelProxy::countChanged, this, &MockPeerSelectionModel::reconcile);
            connect(value, &QObject::destroyed, this, [this] { clear(); m_source = nullptr; m_phase = Stopped; Q_EMIT changed(); });
        }
        Q_EMIT sourceChanged(); reconcile();
    }
    bool autoSelectFirst() const { return m_auto; }
    void setAutoSelectFirst(bool value) { if (m_auto == value) return; m_auto = value; Q_EMIT autoSelectFirstChanged(); reconcile(); }
    MockPeerDetailsModel* selectedDetails() const { return m_selected; }
    QString connectionId() const { return m_connection; }
    QString nodeIdText() const { return m_node_id < 0 ? QString{} : QString::number(m_node_id); }
    qint64 nodeId() const { return m_node_id; }
    bool connected() const { return m_phase == Connected && m_selected && m_selected->connected(); }
    Phase phase() const { return m_phase; }
    Q_INVOKABLE bool select(MockPeerDetailsModel* details) {
        if (!m_source || !details || !details->connected() || m_phase == Stopped) return false;
        const int row = m_source->indexOfNodeId(details->m_node_id);
        if (row < 0 || m_source->peerDetailsAt(row) != details) return false;
        clear(); m_selected = details; m_connection = details->connectionId(); m_node_id = details->m_node_id; m_phase = Connected;
        connect(details, &MockPeerDetailsModel::dataChanged, this, [this] { if (m_selected && !m_selected->connected()) { m_phase = Disconnected; Q_EMIT changed(); } });
        Q_EMIT changed(); return true;
    }
    Q_INVOKABLE void clear() {
        if (!m_selected && m_phase == NoSelection) return;
        if (m_selected) disconnect(m_selected, nullptr, this, nullptr);
        m_selected = nullptr; m_connection.clear(); m_node_id = -1; m_phase = NoSelection; Q_EMIT changed();
    }
Q_SIGNALS:
    void sourceChanged();
    void autoSelectFirstChanged();
    void changed();
private:
    void reconcile() {
        if (!m_source || m_phase == Stopped) return;
        const int row = m_source->indexOfNodeId(m_node_id);
        if (connected() && row >= 0 && m_source->peerDetailsAt(row) == m_selected) return;
        clear(); if (m_auto && m_source->rowCount() > 0) select(m_source->peerDetailsAt(0));
    }
    QPointer<MockPeerListModelProxy> m_source;
    QPointer<MockPeerDetailsModel> m_selected;
    QString m_connection;
    qint64 m_node_id{-1};
    bool m_auto{false}; Phase m_phase{NoSelection};
};

class MockBanListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool unbanResult MEMBER m_unban_result NOTIFY actionStateChanged)
    Q_PROPERTY(bool resetOnUnban MEMBER m_reset_on_unban NOTIFY actionStateChanged)
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

Q_SIGNALS:
    void unbanFinished(bool success);

public:

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
        if (m_reset_on_unban) {
            beginResetModel();
            endResetModel();
        }
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
        m_reset_on_unban = false;
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
    bool m_reset_on_unban{false};
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
        WalletSectionRole,
        BalanceSatoshiRole,
        BalanceKnownRole,
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
        if (role == BalanceSatoshiRole) return qint64{0};
        if (role == BalanceKnownRole) return false;
        if (role == WalletSectionRole) return m_wallet_load_states.at(index.row()) == 1 ? QStringLiteral("open") : QStringLiteral("closed");
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
            {WalletSectionRole, "walletSection"},
            {BalanceSatoshiRole, "balanceSatoshi"},
            {BalanceKnownRole, "balanceKnown"},
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
        Q_EMIT dataChanged(changed_index, changed_index, {LoadStateRole, WalletSectionRole});
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
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(quint64 attemptId MEMBER m_attempt_id NOTIFY stateChanged)
    Q_PROPERTY(quint64 commitId MEMBER m_commit_id NOTIFY stateChanged)
    Q_PROPERTY(bool requireUnlock READ requireUnlock WRITE setRequireUnlock NOTIFY resultChanged)
    Q_PROPERTY(QString oldFee MEMBER m_old_fee NOTIFY resultChanged)
    Q_PROPERTY(QString newFee MEMBER m_new_fee NOTIFY resultChanged)
    Q_PROPERTY(QString feeIncrease MEMBER m_fee_increase NOTIFY resultChanged)
    Q_PROPERTY(QString oldTxid MEMBER m_old_txid NOTIFY resultChanged)
    Q_PROPERTY(QString newTxid MEMBER m_new_txid NOTIFY resultChanged)
    Q_PROPERTY(int walletRecordState MEMBER m_wallet_record_state NOTIFY resultChanged)
    Q_PROPERTY(int walletSubmissionState MEMBER m_wallet_submission_state NOTIFY resultChanged)
    Q_PROPERTY(QString errorText MEMBER m_error_text NOTIFY resultChanged)
    Q_PROPERTY(QString unlockErrorText MEMBER m_unlock_error_text NOTIFY resultChanged)
    Q_PROPERTY(bool needsUnlock MEMBER m_needs_unlock NOTIFY needsUnlockChanged)
    Q_PROPERTY(int preparationCount READ preparationCount NOTIFY stateChanged)
    Q_PROPERTY(int observationCount READ observationCount NOTIFY stateChanged)
    Q_PROPERTY(bool canRecheckOutcome READ canRecheckOutcome NOTIFY stateChanged)

public:
    enum State { Idle, Preparing, NeedsConfirmation, Committing, Succeeded, Failed, Discarding, Uncertain, Closed, NeedsRevalidation };
    Q_ENUM(State)

    enum WalletRecordState { WalletRecordUnknown, WalletRecordAbsentAtObservation, WalletRecordRecorded };
    Q_ENUM(WalletRecordState)

    enum WalletSubmissionState { WalletSubmissionUnknown, WalletSubmissionMempool, WalletSubmissionConfirmed,
                                 WalletSubmissionNotInMempool, WalletSubmissionConflicted, WalletSubmissionAbandoned };
    Q_ENUM(WalletSubmissionState)

    enum ActionType { SpeedUp };
    Q_ENUM(ActionType)

    int state() const { return m_state; }
    bool busy() const { return m_preparing || m_committing; }
    bool requireUnlock() const { return m_require_unlock; }
    int preparationCount() const { return m_preparation_count; }

    Q_INVOKABLE void revalidateOnNextConfirm() { m_revalidate_next_confirm = true; }

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

    int observationCount() const { return m_observation_count; }
    bool canRecheckOutcome() const { return m_state == Uncertain && !busy(); }
    Q_INVOKABLE quint64 requestReviewFeeBump(const QString& txid, unsigned int targetBlocks)
    {
        if (m_state == Uncertain) return m_old_txid == txid ? m_attempt_id : 0;
        return requestPrepareFeeBump(txid, targetBlocks);
    }
    Q_INVOKABLE quint64 requestRecheckFeeBumpOutcome()
    {
        if (!canRecheckOutcome()) return 0;
        ++m_observation_count;
        Q_EMIT stateChanged();
        return static_cast<quint64>(m_observation_count);
    }
    Q_INVOKABLE quint64 requestPrepareFeeBump(const QString& txid, unsigned int targetBlocks)
    {
        Q_UNUSED(targetBlocks);
        if (busy()) return 0;
        const auto attempt = ++m_attempt_id, revision = ++m_revision;
        ++m_preparation_count;
        m_preparing = true;
        m_old_txid = txid;
        m_new_txid.clear(); m_error_text.clear(); m_unlock_error_text.clear(); m_needs_unlock = false;
        m_wallet_record_state = BumpTransactionModel::WalletRecordUnknown;
        m_wallet_submission_state = BumpTransactionModel::WalletSubmissionUnknown;
        m_old_fee = QStringLiteral("0.00000500 BTC");
        m_new_fee = QStringLiteral("0.00001000 BTC");
        m_fee_increase = QStringLiteral("0.00000500 BTC");
        setState(Preparing);
        Q_EMIT resultChanged();
        QTimer::singleShot(0, this, [this, attempt, revision] {
            if (attempt != m_attempt_id) return;
            m_preparing = false;
            setState(revision == m_revision ? NeedsConfirmation : Idle);
        });
        return attempt;
    }

    Q_INVOKABLE quint64 requestConfirmFeeBump()
    {
        if (busy() || m_state != NeedsConfirmation) return 0;
        const auto attempt = m_attempt_id, commit = ++m_commit_id;
        m_committing = true; m_needs_unlock = false; m_error_text.clear(); m_unlock_error_text.clear();
        setState(Committing);
        Q_EMIT needsUnlockChanged();
        QTimer::singleShot(0, this, [this, attempt, commit] {
            if (attempt != m_attempt_id || commit != m_commit_id) return;
            m_committing = false;
            if (m_require_unlock) {
                m_needs_unlock = true;
                setState(NeedsConfirmation);
            } else if (std::exchange(m_revalidate_next_confirm, false)) {
                m_error_text = QStringLiteral("The reviewed replacement no longer meets current node policy. Prepare it again before confirming.");
                setState(NeedsRevalidation);
            } else {
                m_new_txid = QStringLiteral("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
                m_wallet_record_state = BumpTransactionModel::WalletRecordRecorded;
                m_wallet_submission_state = BumpTransactionModel::WalletSubmissionMempool;
                setState(Succeeded);
            }
            Q_EMIT resultChanged();
            Q_EMIT needsUnlockChanged();
        });
        return commit;
    }

    Q_INVOKABLE quint64 requestConfirmFeeBumpWithPassphrase(const QString& passphrase)
    {
        Q_UNUSED(passphrase);
        if (busy() || m_state != NeedsConfirmation) return 0;
        m_require_unlock = false;
        return requestConfirmFeeBump();
    }

    Q_INVOKABLE void reset()
    {
        ++m_revision;
        if (m_committing) return;
        m_state = m_preparing ? Discarding : Idle;
        m_old_fee.clear();
        m_new_fee.clear();
        m_fee_increase.clear();
        m_old_txid.clear();
        m_new_txid.clear();
        m_wallet_record_state = BumpTransactionModel::WalletRecordUnknown;
        m_wallet_submission_state = BumpTransactionModel::WalletSubmissionUnknown;
        m_error_text.clear();
        m_unlock_error_text.clear();
        m_needs_unlock = false;
        m_require_unlock = false;
        m_revalidate_next_confirm = false;
        m_preparation_count = 0;
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
    int m_observation_count{0};
    quint64 m_attempt_id{0}, m_commit_id{0}, m_revision{0};
    bool m_preparing{false}, m_committing{false};
    QString m_old_fee;
    QString m_new_fee;
    QString m_fee_increase;
    QString m_old_txid;
    QString m_new_txid;
    int m_wallet_record_state{BumpTransactionModel::WalletRecordUnknown};
    int m_wallet_submission_state{BumpTransactionModel::WalletSubmissionUnknown};
    QString m_error_text;
    QString m_unlock_error_text;
    bool m_needs_unlock{false};
    bool m_require_unlock{false};
    bool m_revalidate_next_confirm{false};
    int m_preparation_count{0};
};

// Configurable presentation fixture. Transaction interpretation and filtering
// correctness are covered by the C++ model tests; this supplies their QML roles.
class MockActivityValueModel : public QAbstractListModel
{
public:
    explicit MockActivityValueModel(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : m_values.size(); }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "entry"}, {Qt::UserRole + 1, "content"}}; }
    QVariant data(const QModelIndex& index, int role) const override { if (!index.isValid() || index.row() >= m_values.size()) return {}; return role == Qt::UserRole ? m_values[index.row()] : role == Qt::UserRole + 1 ? m_values[index.row()].toMap().value("content") : QVariant{}; }
    void setValues(QVariantList values) { beginResetModel(); m_values = std::move(values); endResetModel(); }
private:
    QVariantList m_values;
};

class MockActivityDetailModel;
class MockTransactionActivityModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap selectedTransactionDetails READ selectedTransactionDetails NOTIFY transactionDetailsChanged)
    Q_PROPERTY(QAbstractItemModel* detailInputs READ detailInputs CONSTANT)
    Q_PROPERTY(QAbstractItemModel* detailOutputs READ detailOutputs CONSTANT)
    Q_PROPERTY(QAbstractItemModel* detailActions READ detailActions CONSTANT)
    Q_PROPERTY(QAbstractItemModel* rawTransactionLines READ rawTransactionLines CONSTANT)
    Q_PROPERTY(QAbstractItemModel* entryDocument READ entryDocument CONSTANT)
    Q_PROPERTY(bool entryDetailsBusy READ entryDetailsBusy NOTIFY entryDetailsChanged)
    Q_PROPERTY(QString entryDetailsError READ entryDetailsError NOTIFY entryDetailsChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(bool loading MEMBER m_loading NOTIFY countChanged)
    Q_PROPERTY(QString loadError MEMBER m_load_error NOTIFY countChanged)
public:
    Q_INVOKABLE MockActivityDetailModel* createDetailView(QObject* owner);
    QVariantList rowsSnapshot() const { return m_rows; }
    bool activityLoading() const { return m_loading; }
    QString activityError() const { return m_load_error; }
    enum ActivityType { Send, Receive, Multiple, Consolidation, Split, InternalTransfer, Mined, Other };
    Q_ENUM(ActivityType)
    enum ActionDirection { SendAction, ReceiveAction, InternalAction };
    Q_ENUM(ActionDirection)
    enum ActionSource { Output, WalletInputs };
    Q_ENUM(ActionSource)

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    QHash<int, QByteArray> roleNames() const override
    {
        QHash<int, QByteArray> roles;
        const QList<QByteArray> names{
            "activityId", "activityType", "txid", "requestId", "label", "address", "amount", "netAmountSat", "timestamp",
            "status", "depth", "blocksToMaturity", "statusKnown", "isInactive", "isPending", "isPendingRequest",
            "replacedByTxid", "hasPaymentRequest", "actions", "type", "paymentRequests", "canBump", "presentationLimited", "paymentRequestCount"};
        for (int i = 0; i < names.size(); ++i) roles.insert(Qt::UserRole + 500 + i, names[i]);
        return roles;
    }
    QVariant data(const QModelIndex& index, int role) const override
    {
        return index.isValid() && index.row() < m_rows.size() ? m_rows[index.row()].toMap().value(QString::fromUtf8(roleNames().value(role))) : QVariant{};
    }
    Q_INVOKABLE void setRows(const QVariantList& rows)
    {
        beginResetModel();
        m_rows = rows;
        endResetModel();
        Q_EMIT countChanged();
        requestTransactionDetails(m_requested_txid);
    }
    QAbstractItemModel* detailInputs() { return &m_inputs; }
    QAbstractItemModel* detailOutputs() { return &m_outputs; }
    QAbstractItemModel* detailActions() { return &m_actions; }
    QAbstractItemModel* rawTransactionLines() { return &m_raw; }
    QAbstractItemModel* entryDocument() { return &m_document; }
    bool entryDetailsBusy() const { return false; }
    QString entryDetailsError() const { return {}; }
    QVariantMap selectedTransactionDetails() const { auto value = transactionDetails(m_requested_txid, true); value["rawAvailable"] = !value.value("rawTransaction").toString().isEmpty(); value.remove("rawTransaction"); return value; }
    Q_INVOKABLE void reload() {}
    Q_INVOKABLE void requestTransactionDetails(const QString& txid) {
        m_requested_txid = txid;
        const auto value = transactionDetails(txid, true); const auto flow = value.value("flow").toMap();
        m_inputs.setValues(flow.value("inputs").toList()); m_outputs.setValues(flow.value("outputs").toList()); m_actions.setValues(value.value("actions").toList());
        QVariantList raw; const auto hex = value.value("rawTransaction").toString(); for (qsizetype offset = 0; offset < hex.size(); offset += 1024) raw.append(QVariantMap{{"content", hex.mid(offset, 1024)}}); m_raw.setValues(raw);
        Q_EMIT transactionDetailsChanged();
    }
    Q_INVOKABLE quint64 requestCopyRawTransaction(const QString& txid) {
        const auto text = rawTransaction(txid); if (text.isEmpty()) return 0;
        const auto ticket = ++m_copy_ticket;
        const QPointer<MockTransactionActivityModel> observer{this};
        QTimer::singleShot(0, QCoreApplication::instance(), [observer, ticket, text] { const bool copied = QGuiApplication::clipboard(); if (copied) QGuiApplication::clipboard()->setText(text); if(observer)Q_EMIT observer->copyFinished(ticket, copied, {}); }); return ticket;
    }
    Q_INVOKABLE quint64 requestEntryDetails(const QString& side, int row) {
        const auto details = transactionDetails(m_requested_txid, true); const auto flow = details.value("flow").toMap();
        const auto values = side == "actions" ? details.value("actions").toList() : flow.value(side).toList(); if (row < 0 || row >= values.size()) return 0;
        QVariantList lines; std::function<void(QString, QVariant)> flatten;
        flatten = [&](QString field, QVariant value) { if (value.metaType().id() == QMetaType::QVariantMap) { const auto map = value.toMap(); for (auto item = map.cbegin(); item != map.cend(); ++item) flatten(field.isEmpty() ? item.key() : field + "." + item.key(), item.value()); } else if (value.metaType().id() == QMetaType::QVariantList) { const auto list = value.toList(); for (int i = 0; i < list.size(); ++i) flatten(field + QStringLiteral("[%1]").arg(i), list[i]); } else { const auto text = value.toString(); for (qsizetype offset = 0; offset < std::max(qsizetype(1), text.size()); offset += 1024) lines.append(QVariantMap{{"field", field}, {"content", text.mid(offset, 1024)}}); } };
        flatten({}, values[row]); m_document.setValues(lines); Q_EMIT entryDetailsChanged(); return 1;
    }
    Q_INVOKABLE QString requestedTransaction() const { return m_requested_txid; }
    Q_INVOKABLE QString rawTransaction(const QString& txid) const { return transactionDetails(txid).value("rawTransaction").toString(); }
    Q_INVOKABLE QString paymentRequestUri(const QString& request_id) const
    {
        for (const auto& row : m_rows) if (row.toMap().value("requestId").toString() == request_id) return row.toMap().value("uri").toString();
        return {};
    }
    Q_INVOKABLE QVariantMap transactionDetails(const QString& txid, bool include_flow = false) const
    {
        Q_UNUSED(include_flow);
        for (const auto& row : m_rows) if (row.toMap().value("txid").toString() == txid) {
            auto value = row.toMap(); auto flow = value.value("flow").toMap(); if (!flow.isEmpty()) { flow["presentation"] = PrepareTransactionFlowPresentation(flow); value["flow"] = flow; } return value;
        }
        return {};
    }
Q_SIGNALS:
    void countChanged();
    void transactionDetailsChanged();
    void entryDetailsChanged();
    void copyFinished(quint64 ticket, bool copied, const QString& error);
private:
    MockActivityValueModel m_inputs{this}, m_outputs{this}, m_actions{this}, m_raw{this}, m_document{this};
    quint64 m_copy_ticket{0};
    QVariantList m_rows;
    bool m_loading{false};
    QString m_load_error;
    QString m_requested_txid;
};

class MockActivityDetailModel : public MockTransactionActivityModel
{
    Q_OBJECT
    Q_PROPERTY(QString transactionId READ transactionId WRITE setTransactionId NOTIFY transactionIdChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(MockActivityDetailModel::State state READ state NOTIFY transactionDetailsChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY transactionDetailsChanged)
    Q_PROPERTY(QString error READ error NOTIFY transactionDetailsChanged)
public:
    enum State { Inactive, Idle, Loading, Ready, Failed, Closed };
    Q_ENUM(State)
    State state() const {return !m_active?Inactive:m_txid.isEmpty()?Idle:Ready;}
    MockActivityDetailModel(MockTransactionActivityModel& source,QObject* owner) {
        setParent(owner); QQmlEngine::setObjectOwnership(this,QQmlEngine::CppOwnership);
        setRows(source.rowsSnapshot());
        connect(&source,&MockTransactionActivityModel::countChanged,this,[this,&source] {setRows(source.rowsSnapshot());});
    }
    QString transactionId() const {return m_txid;}
    bool active() const {return m_active;}
    bool pending() const {return false;}
    QString error() const {return {};}
    void setTransactionId(const QString& txid) {if(m_txid==txid)return;m_txid=txid;requestTransactionDetails(m_active?m_txid:QString{});Q_EMIT transactionIdChanged();}
    void setActive(bool value) {if(m_active==value)return;m_active=value;requestTransactionDetails(m_active?m_txid:QString{});Q_EMIT activeChanged();}
Q_SIGNALS:
    void transactionIdChanged();
    void activeChanged();
private:
    QString m_txid;
    bool m_active{false};
};
MockActivityDetailModel* MockTransactionActivityModel::createDetailView(QObject* owner)
{
    return owner?new MockActivityDetailModel{*this,owner}:nullptr;
}

class MockActivityFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY sourceStateChanged)
    Q_PROPERTY(QString error READ error NOTIFY sourceStateChanged)
    Q_PROPERTY(ActivityExportOperation* exportOperation READ exportOperation NOTIFY exportChanged)
    Q_PROPERTY(ActivityExportResult exportResult READ exportResult NOTIFY exportChanged)
    Q_PROPERTY(bool exportChoosing READ exportChoosing NOTIFY exportChanged)
    Q_PROPERTY(bool exportBusy READ exportBusy NOTIFY exportChanged)
    Q_PROPERTY(bool exportRejected READ exportRejected NOTIFY exportChanged)
    Q_PROPERTY(QString exportError READ exportError NOTIFY exportChanged)
    Q_PROPERTY(QString exportedPath READ exportedPath NOTIFY exportChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(DateFilter dateFilter READ dateFilter WRITE setDateFilter NOTIFY dateFilterChanged)
    Q_PROPERTY(TypeFilter typeFilter READ typeFilter WRITE setTypeFilter NOTIFY typeFilterChanged)
    Q_PROPERTY(QList<int> typeFilters READ typeFilters WRITE setTypeFilters NOTIFY typeFiltersChanged)
    Q_PROPERTY(int displayUnit READ displayUnit WRITE setDisplayUnit NOTIFY displayUnitChanged)
    Q_PROPERTY(qint64 minAmount READ minAmount WRITE setMinAmount NOTIFY minAmountChanged)
    Q_PROPERTY(qint64 maxAmount READ maxAmount WRITE setMaxAmount NOTIFY maxAmountChanged)
    Q_PROPERTY(qint64 availableMaxAmount READ availableMaxAmount NOTIFY availableMaxAmountChanged)
    Q_PROPERTY(QDate rangeStart READ rangeStart NOTIFY rangeChanged)
    Q_PROPERTY(QDate rangeEnd READ rangeEnd NOTIFY rangeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int transactionCount READ transactionCount NOTIFY countChanged)
    Q_PROPERTY(int requestCount READ requestCount NOTIFY countChanged)
    Q_PROPERTY(qint64 pendingBalanceSat READ pendingBalanceSat NOTIFY pendingBalanceChanged)
    Q_PROPERTY(QString pendingBalance READ pendingBalance NOTIFY pendingBalanceChanged)
    Q_PROPERTY(GroupBy groupBy READ groupBy WRITE setGroupBy NOTIFY groupByChanged)

public:
    enum DateFilter {
        DateAll,
        Today,
        ThisWeek,
        ThisMonth,
        ThisYear,
        CustomRange
    };
    Q_ENUM(DateFilter)

    enum TypeFilter {
        TypeAll,
        Received,
        Sent,
        SentToSelf,
        Mined,
        Other,
        PaymentRequest,
        Multiple,
        Consolidation,
        Split
    };
    Q_ENUM(TypeFilter)

    enum GroupBy { Month, Day };
    Q_ENUM(GroupBy)
    enum SectionRole { SectionKeyRole = Qt::UserRole + 200, SectionLabelRole, DateTimeLabelRole };
    GroupBy groupBy() const { return m_group_by; }
    void setGroupBy(GroupBy group)
    {
        if (group == m_group_by) return;
        m_group_by = group;
        if (rowCount()) Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, 0));
        Q_EMIT groupByChanged();
    }
    bool grouped() const { return qobject_cast<MockTransactionActivityModel*>(sourceModel()); }
    QVariant namedData(const QModelIndex& index, const QByteArray& name) const
    {
        const auto roles = index.model()->roleNames();
        return index.data(roles.key(name, -1));
    }
    int requestCount() const
    {
        int count{0};
        for (int i = 0; i < rowCount(); ++i) count += namedData(mapToSource(index(i, 0)), "isPendingRequest").toBool();
        return count;
    }
    int transactionCount() const { return rowCount() - requestCount(); }
    qint64 pendingBalanceSat() const
    {
        qint64 total{0};
        if (!grouped()) return total;
        for (int i = 0; i < rowCount(); ++i) {
            const auto row = mapToSource(index(i, 0));
            if (namedData(row, "isPending").toBool() && !namedData(row, "isPendingRequest").toBool()) {
                total += namedData(row, "netAmountSat").toLongLong();
            }
        }
        return total;
    }
    QString pendingBalance() const
    {
        MockBitcoinAmount amount;
        amount.setUnit(static_cast<MockBitcoinAmount::Unit>(m_display_unit));
        amount.setSatoshi(pendingBalanceSat());
        return (pendingBalanceSat() > 0 ? QStringLiteral("+") : QString{}) + amount.displayWithUnit();
    }

    explicit MockActivityFilterProxyModel(QObject* parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
        connect(this, &QAbstractItemModel::rowsInserted, this, &MockActivityFilterProxyModel::countChanged);
        connect(this, &QAbstractItemModel::rowsRemoved, this, &MockActivityFilterProxyModel::countChanged);
        connect(this, &QAbstractItemModel::modelReset, this, &MockActivityFilterProxyModel::countChanged);
        connect(this, &QAbstractItemModel::layoutChanged, this, &MockActivityFilterProxyModel::countChanged);
        connect(this, &MockActivityFilterProxyModel::countChanged, this, &MockActivityFilterProxyModel::pendingBalanceChanged);
        connect(this, &QAbstractItemModel::dataChanged, this, &MockActivityFilterProxyModel::pendingBalanceChanged);
        connect(this, &MockActivityFilterProxyModel::displayUnitChanged, this, &MockActivityFilterProxyModel::pendingBalanceChanged);
    }

    QHash<int, QByteArray> roleNames() const override
    {
        auto roles = sourceModel() ? sourceModel()->roleNames() : QHash<int, QByteArray>{};
        if (grouped()) {
            roles.insert(SectionKeyRole, "sectionKey");
            roles.insert(SectionLabelRole, "sectionLabel");
            roles.insert(DateTimeLabelRole, "dateTimeLabel");
        }
        return roles;
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (grouped() && (role == SectionKeyRole || role == SectionLabelRole || role == DateTimeLabelRole)) {
            const auto source = mapToSource(index);
            const bool pending = namedData(source, "isPending").toBool();
            const auto date = QDateTime::fromSecsSinceEpoch(namedData(source, "timestamp").toLongLong());
            if (role == DateTimeLabelRole) return date.toString(m_group_by == Day && !pending ? "h:mm AP" : "MMM d, h:mm AP");
            if (pending) return QStringLiteral("Pending");
            return date.toString(m_group_by == Day ? "MMMM d, yyyy" : "MMMM yyyy");
        }
        return QSortFilterProxyModel::data(index, role);
    }

    void setSourceModel(QAbstractItemModel* source_model) override
    {
        if (sourceModel() == source_model) return;
        for (const auto& connection : m_source_connections) disconnect(connection);
        m_source_connections.clear();
        QSortFilterProxyModel::setSourceModel(source_model);
        if (source_model) {
            m_source_connections << connect(source_model, &QAbstractItemModel::modelReset, this, &MockActivityFilterProxyModel::availableMaxAmountChanged);
            m_source_connections << connect(source_model, &QAbstractItemModel::rowsInserted, this, &MockActivityFilterProxyModel::availableMaxAmountChanged);
            m_source_connections << connect(source_model, &QAbstractItemModel::rowsRemoved, this, &MockActivityFilterProxyModel::availableMaxAmountChanged);
            m_source_connections << connect(source_model, &QAbstractItemModel::dataChanged, this, &MockActivityFilterProxyModel::availableMaxAmountChanged);
            if (auto* activity = qobject_cast<MockTransactionActivityModel*>(source_model)) {
                m_source_connections << connect(activity, &MockTransactionActivityModel::countChanged, this, &MockActivityFilterProxyModel::sourceStateChanged);
            }
            m_source_connections << connect(source_model, &QObject::destroyed, this, &MockActivityFilterProxyModel::sourceStateChanged);
        }
        Q_EMIT sourceStateChanged();
        Q_EMIT availableMaxAmountChanged();
        if (grouped()) sort(0, Qt::DescendingOrder);
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

    TypeFilter typeFilter() const { return m_type_filters.isEmpty() ? TypeAll : static_cast<TypeFilter>(m_type_filters.first()); }
    QList<int> typeFilters() const { return m_type_filters; }
    void setTypeFilter(TypeFilter type) { setTypeFilters(type == TypeAll ? QList<int>{} : QList<int>{type}); }
    void setTypeFilters(const QList<int>& types)
    {
        QList<int> normalized;
        for (int type : types) {
            if (type > TypeAll && type <= Split && !normalized.contains(type)) normalized.append(type);
        }
        std::sort(normalized.begin(), normalized.end());
        if (m_type_filters == normalized) return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
#endif
        m_type_filters = normalized;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
        Q_EMIT typeFilterChanged();
        Q_EMIT typeFiltersChanged();
        Q_EMIT countChanged();
    }

    int displayUnit() const { return m_display_unit; }
    void setDisplayUnit(int display_unit)
    {
        if (m_display_unit == display_unit) return;
        m_display_unit = display_unit;
        Q_EMIT displayUnitChanged();
    }

    qint64 minAmount() const { return m_min_amount; }
    qint64 maxAmount() const { return m_max_amount; }
    void setMinAmount(qint64 value) { setAmountRange(value, m_max_amount); }
    void setMaxAmount(qint64 value) { setAmountRange(m_min_amount, value); }
    Q_INVOKABLE bool setAmountRange(qint64 minimum, qint64 maximum)
    {
        // Negative bounds are unset; an inverted range must not hide all activity.
        minimum = minimum < 0 ? -1 : minimum;
        maximum = maximum < 0 ? -1 : maximum;
        if (minimum >= 0 && maximum >= 0 && minimum > maximum) return false;
        const bool min_changed = m_min_amount != minimum;
        const bool max_changed = m_max_amount != maximum;
        if (!min_changed && !max_changed) return true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
#endif
        m_min_amount = minimum;
        m_max_amount = maximum;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
        if (min_changed) Q_EMIT minAmountChanged();
        if (max_changed) Q_EMIT maxAmountChanged();
        Q_EMIT countChanged();
        return true;
    }

    qint64 availableMaxAmount() const
    {
        qint64 result{0};
        if (sourceModel()) for (int i = 0; i < sourceModel()->rowCount(); ++i) {
            const auto row = sourceModel()->index(i, 0);
            result = qMax(result, qAbs(namedData(row, "netAmountSat").toLongLong()));
        }
        return result;
    }

    QDate rangeStart() const { return m_range_start; }
    QDate rangeEnd() const { return m_range_end; }

    Q_INVOKABLE bool applyCustomRange(const QString& start_iso, const QString& end_iso)
    {
        const QDate start = QDate::fromString(start_iso, Qt::ISODate);
        const QDate end = QDate::fromString(end_iso, Qt::ISODate);
        if (!start.isValid() || !end.isValid() || start > end) return false;

        const bool range_changed = start != m_range_start || end != m_range_end;
        const bool filter_changed = m_date_filter != CustomRange;
        if (!range_changed && !filter_changed) return true;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
#endif
        m_range_start = start;
        m_range_end = end;
        m_date_filter = CustomRange;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif

        if (range_changed) Q_EMIT rangeChanged();
        if (filter_changed) Q_EMIT dateFilterChanged();
        Q_EMIT countChanged();
        return true;
    }

    int count() const { return rowCount(); }

    bool busy() const {
        const auto* source = qobject_cast<MockTransactionActivityModel*>(sourceModel());
        return source && source->activityLoading();
    }
    QString error() const {
        const auto* source = qobject_cast<MockTransactionActivityModel*>(sourceModel());
        return source ? source->activityError() : QString{};
    }
    ActivityExportOperation* exportOperation() const { return nullptr; }
    Q_INVOKABLE void clearDateFilter() { setDateFilter(DateAll); }
    bool exportChoosing() const { return !m_chooser_token.isEmpty(); }
    bool exportBusy() const { return m_export_busy || exportChoosing(); }
    Q_INVOKABLE QString captureExport() {
        if (exportBusy() || busy() || !sourceModel()) return {};
        m_chooser_token = QString::number(++m_chooser_sequence); m_chooser_source = sourceModel(); Q_EMIT exportChanged(); return m_chooser_token;
    }
    Q_INVOKABLE void cancelExport(const QString& token) {
        if (token.isEmpty() || token != m_chooser_token) return;
        m_chooser_token.clear(); m_chooser_source.clear(); Q_EMIT exportChanged();
    }
    Q_INVOKABLE quint64 requestCapturedExportCsv(const QString& token, const QString& path) {
        if (token.isEmpty() || token != m_chooser_token || m_chooser_source != sourceModel()) return 0;
        m_chooser_token.clear(); m_chooser_source.clear(); return requestExportCsv(path);
    }
    ActivityExportResult exportResult() const { return {}; }
    bool exportRejected() const { return false; }
    QString exportError() const { return {}; }
    QString exportedPath() const { return m_export_path; }
    Q_INVOKABLE quint64 requestExportCsv(const QString& path) {
        if (m_export_busy || path.isEmpty()) return 0;
        m_export_busy = true; m_export_path.clear(); const auto ticket = ++m_ticket; Q_EMIT exportChanged();
        QTimer::singleShot(0, this, [this, path, ticket] { m_export_busy = false; m_export_path = path; Q_EMIT exportChanged(); Q_EMIT exportFinished(ticket, true, path, {}); }); return ticket;
    }
    Q_INVOKABLE quint64 captureRowContext(int row) { if (row < 0 || row >= rowCount()) return 0; m_context_txid = namedData(mapToSource(index(row, 0)), "txid").toString(); return ++m_context; }
    Q_INVOKABLE void releaseRowContext(quint64 token) { if (token == m_context) { m_context_txid.clear(); ++m_context; } }
    Q_INVOKABLE quint64 requestCopyRawTransaction(quint64 context) { auto* model = qobject_cast<MockTransactionActivityModel*>(sourceModel()); return model && context == m_context ? model->requestCopyRawTransaction(m_context_txid) : 0; }

    bool matchesTypes(const QModelIndex& row) const
    {
        using Model = MockTransactionActivityModel;
        if (m_type_filters.isEmpty()) return true;
        const bool request = namedData(row, "isPendingRequest").toBool();
        const int kind = namedData(row, "activityType").toInt();
        for (int type : m_type_filters) {
            if (type == PaymentRequest && (request || namedData(row, "hasPaymentRequest").toBool())) return true;
            if (request) continue;
            if ((type == Multiple && kind == Model::Multiple) || (type == Consolidation && kind == Model::Consolidation)
                || (type == Split && kind == Model::Split) || (type == Mined && kind == Model::Mined)
                || (type == Other && kind == Model::Other) || (type == SentToSelf && (kind == Model::InternalTransfer || kind == Model::Consolidation || kind == Model::Split))) return true;
            if (type == Sent || type == Received) {
                if ((type == Sent && kind == Model::Send) || (type == Received && kind == Model::Receive)) return true;
                for (const auto& action : namedData(row, "actions").toList()) {
                    if (action.toMap().value("direction").toInt() == (type == Sent ? Model::SendAction : Model::ReceiveAction)) return true;
                }
            }
        }
        return false;
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override
    {
        if (grouped()) {
            const auto row = sourceModel()->index(source_row, 0, source_parent);
            QString search = namedData(row, "label").toString() + namedData(row, "address").toString() + namedData(row, "txid").toString();
            for (const auto& action : namedData(row, "actions").toList()) search += action.toMap().value("label").toString() + action.toMap().value("address").toString();
            return search.contains(m_search_text.trimmed(), Qt::CaseInsensitive)
                && (m_min_amount < 0 || qAbs(namedData(row, "netAmountSat").toLongLong()) >= m_min_amount)
                && (m_max_amount < 0 || qAbs(namedData(row, "netAmountSat").toLongLong()) <= m_max_amount)
                && matchesTypes(row) && m_date_filter == DateAll;
        }
        Q_UNUSED(source_row);
        Q_UNUSED(source_parent);
        return m_search_text.trimmed().isEmpty() &&
            m_date_filter == DateAll &&
            m_type_filters.isEmpty();
    }

    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override
    {
        if (!grouped()) return QSortFilterProxyModel::lessThan(left, right);
        const bool a = namedData(left, "isPending").toBool(), b = namedData(right, "isPending").toBool();
        if (a != b) return !a;
        return namedData(left, "timestamp").toLongLong() < namedData(right, "timestamp").toLongLong();
    }

Q_SIGNALS:
    void exportChanged();
    void exportFinished(quint64 ticket, bool saved, const QString& path, const QString& error);
    void sourceStateChanged();
    void searchTextChanged();
    void dateFilterChanged();
    void typeFilterChanged();
    void typeFiltersChanged();
    void displayUnitChanged();
    void minAmountChanged();
    void maxAmountChanged();
    void availableMaxAmountChanged();
    void rangeChanged();
    void countChanged();

    void groupByChanged();
    void pendingBalanceChanged();

private:
    QString m_chooser_token; quint64 m_chooser_sequence{0}; QPointer<QAbstractItemModel> m_chooser_source;
    bool m_export_busy{false};
    quint64 m_ticket{0}, m_context{0};
    QString m_export_path, m_context_txid;
    QString m_search_text;
    GroupBy m_group_by{Month};
    DateFilter m_date_filter{DateAll};
    QList<int> m_type_filters;
    QList<QMetaObject::Connection> m_source_connections;
    int m_display_unit{0};
    qint64 m_min_amount{-1};
    qint64 m_max_amount{-1};
    QDate m_range_start;
    QDate m_range_end;
};

class MockDesktopWindowBehaviorModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool desktopPlatform READ desktopPlatform CONSTANT)
    Q_PROPERTY(bool settingsReady READ settingsReady CONSTANT)
    Q_PROPERTY(bool persistencePending READ persistencePending CONSTANT)
    Q_PROPERTY(QString persistenceError READ persistenceError CONSTANT)
    Q_PROPERTY(bool showTrayIcon READ showTrayIcon WRITE setShowTrayIcon NOTIFY showTrayIconChanged)
    Q_PROPERTY(bool minimizeToTray READ minimizeToTray WRITE setMinimizeToTray NOTIFY minimizeToTrayChanged)
    Q_PROPERTY(bool minimizeOnClose READ minimizeOnClose WRITE setMinimizeOnClose NOTIFY minimizeOnCloseChanged)

public:
    Q_INVOKABLE void refreshSettings() {}

    bool settingsReady() const { return true; }
    bool persistencePending() const { return false; }
    QString persistenceError() const { return {}; }
    Q_INVOKABLE void retryPersistence() {}
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
    Q_PROPERTY(bool warningsAndErrorsOnly READ warningsAndErrorsOnly WRITE setWarningsAndErrorsOnly NOTIFY warningsAndErrorsOnlyChanged)
    Q_PROPERTY(QString openError READ openError NOTIFY openErrorChanged)
    Q_PROPERTY(bool logAvailable READ logAvailable NOTIFY logAvailableChanged)
    Q_PROPERTY(bool fileLoggingEnabled READ fileLoggingEnabled NOTIFY fileLoggingEnabledChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int loadMoreCalls READ loadMoreCalls NOTIFY loadMoreCallsChanged)
    Q_PROPERTY(int openLogFileCalls READ openLogFileCalls NOTIFY openLogFileCallsChanged)

public:
    enum Role {
        MessageRole = Qt::UserRole + 1,
        TimestampRole,
        IsErrorRole,
        IsWarningRole,
    };
    Q_ENUM(Role)

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
        case MessageRole: return row.message;
        case TimestampRole: return row.timestamp;
        case IsErrorRole: return row.is_error;
        case IsWarningRole: return row.is_warning;
        default: return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {MessageRole, "message"},
            {TimestampRole, "timestamp"},
            {IsErrorRole, "isError"},
            {IsWarningRole, "isWarning"},
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
    QString openError() const { return m_open_error; }
    bool logAvailable() const { return m_log_available; }
    Q_INVOKABLE void setLogAvailable(bool available)
    {
        if (m_log_available == available) return;
        m_log_available = available;
        Q_EMIT logAvailableChanged();
    }
    bool fileLoggingEnabled() const { return m_file_logging_enabled; }
    Q_INVOKABLE void setFileLoggingEnabled(bool enabled)
    {
        if (m_file_logging_enabled == enabled) return;
        m_file_logging_enabled = enabled;
        Q_EMIT fileLoggingEnabledChanged();
    }
    bool warningsAndErrorsOnly() const { return m_warnings_and_errors_only; }
    void setWarningsAndErrorsOnly(bool warnings_and_errors_only)
    {
        if (m_warnings_and_errors_only == warnings_and_errors_only) return;
        m_warnings_and_errors_only = warnings_and_errors_only;
        Q_EMIT warningsAndErrorsOnlyChanged();
    }
    int loadMoreCalls() const { return m_load_more_calls; }
    int openLogFileCalls() const { return m_open_log_file_calls; }

    Q_INVOKABLE void refresh(bool = false) {}
    Q_INVOKABLE void loadMore()
    {
        ++m_load_more_calls;
        Q_EMIT loadMoreCallsChanged();
        prependRowsForTest(20);
        setHasMoreLinesForTest(false);
    }
    Q_INVOKABLE bool openLogFile()
    {
        ++m_open_log_file_calls;
        Q_EMIT openLogFileCallsChanged();
        const QString next = m_open_result ? QString{} : m_pending_error;
        if (m_open_error != next) {
            m_open_error = next;
            Q_EMIT openErrorChanged();
        }
        return m_open_result;
    }
    Q_INVOKABLE void setOpenLogFileResult(bool ok, const QString& error)
    {
        m_open_result = ok;
        m_pending_error = error;
    }
    Q_INVOKABLE void clearOpenError()
    {
        if (m_open_error.isEmpty()) return;
        m_open_error.clear();
        Q_EMIT openErrorChanged();
    }
    Q_INVOKABLE void reset()
    {
        m_open_result = true;
        m_pending_error.clear();
        setLogAvailable(true);
        setFileLoggingEnabled(true);
        if (!m_open_error.isEmpty()) {
            m_open_error.clear();
            Q_EMIT openErrorChanged();
        }
    }

    Q_INVOKABLE void resetForTest(int count, bool has_more_lines)
    {
        m_filter.clear();
        m_warnings_and_errors_only = false;
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
        m_open_log_file_calls = 0;
        Q_EMIT countChanged();
        Q_EMIT filterChanged();
        Q_EMIT warningsAndErrorsOnlyChanged();
        Q_EMIT loadMoreCallsChanged();
        Q_EMIT openLogFileCallsChanged();
        setHasMoreLinesForTest(has_more_lines);
    }

    Q_INVOKABLE void prependRowsForTest(int count)
    {
        if (count <= 0) return;

        QList<Row> added;
        added.reserve(count);
        for (int i = 0; i < count; ++i) {
            added.append(makeRow(QStringLiteral("old-%1").arg(m_next_old_row++)));
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
            m_rows.append(makeRow(QStringLiteral("new-%1").arg(m_next_new_row++)));
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
        Q_EMIT dataChanged(changed_index, changed_index, {MessageRole});
    }

    Q_INVOKABLE void setStructuredFieldsForTest(int row,
                                                bool is_error,
                                                const QString& timestamp)
    {
        if (row < 0 || row >= m_rows.size()) return;
        Row& item = m_rows[row];
        item.is_error = is_error;
        item.is_warning = false;
        item.timestamp = timestamp;
        const QModelIndex changed_index = index(row, 0);
        Q_EMIT dataChanged(changed_index, changed_index,
                           {IsErrorRole, IsWarningRole, TimestampRole});
    }

    Q_INVOKABLE void setWarningForTest(int row, bool is_warning)
    {
        if (row < 0 || row >= m_rows.size()) return;
        Row& item = m_rows[row];
        item.is_warning = is_warning;
        if (is_warning) item.is_error = false;
        const QModelIndex changed_index = index(row, 0);
        Q_EMIT dataChanged(changed_index, changed_index, {IsErrorRole, IsWarningRole});
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
    void warningsAndErrorsOnlyChanged();
    void openErrorChanged();
    void logAvailableChanged();
    void fileLoggingEnabledChanged();
    void newLinesAdded(int count);
    void countChanged();
    void loadMoreCallsChanged();
    void openLogFileCallsChanged();

private:
    struct Row {
        QString message;
        QString timestamp;
        bool is_error{false};
        bool is_warning{false};
    };

    static Row makeRow(const QString& message)
    {
        return Row{
            message,
            QStringLiteral("15:42:08"),
            false,
            false,
        };
    }

    bool m_active{false};
    bool m_has_more_lines{false};
    QString m_filter;
    bool m_warnings_and_errors_only{false};
    QList<Row> m_rows;
    int m_load_more_calls{0};
    int m_open_log_file_calls{0};
    int m_next_new_row{0};
    int m_next_old_row{0};
    QString m_open_error;
    QString m_pending_error;
    bool m_open_result{true};
    bool m_log_available{true};
    bool m_file_logging_enabled{true};
};

class MockDesktopTrayIconController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool visible MEMBER m_visible NOTIFY visibleChanged)
    Q_PROPERTY(bool windowVisible MEMBER m_window_visible NOTIFY windowVisibleChanged)

public:
    bool supported() const { return false; }
    Q_INVOKABLE void showMainWindow() { m_window_visible = true; Q_EMIT windowVisibleChanged(); }
    Q_INVOKABLE void hideMainWindow() { m_window_visible = false; Q_EMIT windowVisibleChanged(); }

Q_SIGNALS:
    void visibleChanged();
    void windowVisibleChanged();
    void showRequested();
    void hideRequested();
    void quitRequested();

private:
    bool m_visible{false};
    bool m_window_visible{true};
};

class MockRpcOutputModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount CONSTANT)

public:
    int rowCount(const QModelIndex& = {}) const override { return 0; }
    QVariant data(const QModelIndex&, int) const override { return {}; }
    QHash<int, QByteArray> roleNames() const override
    {
        return {{Qt::UserRole + 1, "timestamp"}, {Qt::UserRole + 2, "content"}, {Qt::UserRole + 3, "category"}};
    }
};

class MockRpcConsoleModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int commandCount READ commandCount NOTIFY commandSubmitted)
    Q_PROPERTY(QString lastTarget READ lastTarget NOTIFY commandSubmitted)
    Q_PROPERTY(QString lastWalletName READ lastWalletName NOTIFY commandSubmitted)
    Q_PROPERTY(QString completionPrefix MEMBER m_completion_prefix)
    Q_PROPERTY(QStringList completionSuggestions READ completionSuggestions NOTIFY completionChanged)
    Q_PROPERTY(int clearCount READ clearCount NOTIFY outputCleared)
    Q_PROPERTY(bool completionBusy READ completionBusy CONSTANT)
    Q_PROPERTY(QString completionError READ completionError CONSTANT)
    Q_PROPERTY(bool executing READ executing CONSTANT)
    Q_PROPERTY(QStringList availableCommands READ availableCommands CONSTANT)
    Q_PROPERTY(QObject* outputModel READ outputModel CONSTANT)
    Q_PROPERTY(QColor requestColor MEMBER m_request_color)
    Q_PROPERTY(QColor replyColor MEMBER m_reply_color)
    Q_PROPERTY(QColor errorColor MEMBER m_error_color)
    Q_PROPERTY(QColor keyColor MEMBER m_key_color)

public:
    int commandCount() const { return m_command_count; }
    QString lastTarget() const { return m_last_target; }
    QString lastWalletName() const { return m_last_wallet_name; }
    bool executing() const { return false; }
    QStringList availableCommands() const { return {}; }
    QStringList completionSuggestions() const { return m_suggestions; }
    int clearCount() const { return m_clear_count; }
    Q_INVOKABLE void setCompletionSuggestionsForTest(const QStringList& suggestions) { m_suggestions = suggestions; Q_EMIT completionChanged(); }
    bool completionBusy() const { return false; }
    QString completionError() const { return {}; }
    QObject* outputModel() { return &m_output; }
    Q_INVOKABLE bool submitCommand(const QString&, const QString&) { return false; }
    Q_INVOKABLE bool submitInput(const QString& command, bool wallet, const QString& name, bool available) {
        if (command.trimmed() == QLatin1String("stop")) return submitNodeCommand(command);
        if (!available) return false;
        return wallet ? submitWalletCommand(command, name) : submitNodeCommand(command);
    }
    Q_INVOKABLE bool submitNodeCommand(const QString&) { ++m_command_count; m_last_target = QStringLiteral("node"); m_last_wallet_name.clear(); Q_EMIT commandSubmitted(); return false; }
    Q_INVOKABLE bool submitWalletCommand(const QString&, const QString& name) { ++m_command_count; m_last_target = QStringLiteral("wallet"); m_last_wallet_name = name; Q_EMIT commandSubmitted(); return false; }
    Q_INVOKABLE QString browseHistory(int, const QString& current) { return current; }
    Q_INVOKABLE void resetHistoryNavigation() {}
    Q_INVOKABLE void clear() { ++m_clear_count; Q_EMIT outputCleared(); }

Q_SIGNALS:
    void commandSubmitted();
    void completionChanged();
    void outputCleared();

private:
    int m_command_count{0}, m_clear_count{0};
    QStringList m_suggestions;
    QString m_last_target, m_last_wallet_name;
    MockRpcOutputModel m_output;
    QString m_completion_prefix;
    QColor m_request_color;
    QColor m_reply_color;
    QColor m_error_color;
    QColor m_key_color;
};

class TestIconProvider : public QQuickImageProvider
{
public:
    TestIconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}

    QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requested_size) override
    {
        const QPixmap pixmap = QIcon(QStringLiteral(":/icons/") +
            (id == QStringLiteral("app") ? QStringLiteral("bitcoin") : id)).pixmap(requested_size);
        if (size) *size = pixmap.size();
        return pixmap;
    }
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

// Public projection fixture: same content role and observable model shape as
// StartupDiagnosticsModel. Domain intake/reduction is tested with manual services.
class MockStartupDiagnostics : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY projectionChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY projectionChanged)
    Q_PROPERTY(QString summary READ summary NOTIFY projectionChanged)
    Q_PROPERTY(bool present READ present NOTIFY projectionChanged)
public:
    explicit MockStartupDiagnostics(QObject* parent = nullptr) : QAbstractListModel(parent) {}
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    QVariant data(const QModelIndex& index, int role) const override {
        return index.isValid() && index.row() >= 0 && index.row() < m_rows.size() && role == Qt::UserRole + 1
            ? QVariant{m_rows.at(index.row())} : QVariant{};
    }
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole + 1, "content"}}; }
    bool busy() const { return m_busy; }
    bool present() const { return m_present; }
    QString summary() const { return m_summary; }
    Q_INVOKABLE QString contentAt(int row) const { return row >= 0 && row < m_rows.size() ? m_rows.at(row) : QString{}; }
    Q_INVOKABLE void project(const QString& summary, const QStringList& rows, bool present, bool pending) {
        beginResetModel(); m_rows = rows; m_summary = summary; m_present = present; m_busy = pending; endResetModel();
        Q_EMIT projectionChanged();
    }
Q_SIGNALS:
    void projectionChanged();
private:
    QStringList m_rows; QString m_summary; bool m_present{false}, m_busy{false};
};

class MockApplicationModels : public QObject
{
    Q_OBJECT
    Q_PROPERTY(MockNodeModel* nodeModel READ nodeModel CONSTANT)
    Q_PROPERTY(MockChainModel* chainModel READ chainModel CONSTANT)
    Q_PROPERTY(MockBlockClockModel* blockClockModel READ blockClockModel CONSTANT)
    Q_PROPERTY(PresentationServices* presentationServices READ presentationServices CONSTANT)
    Q_PROPERTY(MockOptionsModel* optionsModel READ optionsModel NOTIFY settingsModelsChanged)
    Q_PROPERTY(MockNetworkTrafficTower* networkTrafficTower READ networkTrafficTower CONSTANT)
    Q_PROPERTY(MockNetworkStatusModel* networkStatusModel READ networkStatusModel CONSTANT)
    Q_PROPERTY(MockPeerTableModel* peerTableModel READ peerTableModel CONSTANT)
    Q_PROPERTY(MockPeerListModelProxy* peerListModelProxy READ peerListModelProxy CONSTANT)
    Q_PROPERTY(MockBanListModel* banListModel READ banListModel CONSTANT)
    Q_PROPERTY(MockDebugLogModel* debugLogModel READ debugLogModel CONSTANT)
    Q_PROPERTY(QObject* rpcConsoleModel READ rpcConsoleModel CONSTANT)
    Q_PROPERTY(MockWalletController* walletController READ walletController CONSTANT)
    Q_PROPERTY(SignerDiscoveryModel* signers READ signers CONSTANT)
    Q_PROPERTY(MockWalletListModel* walletListModel READ walletListModel CONSTANT)
    Q_PROPERTY(MockDesktopWindowBehaviorModel* desktopWindowBehaviorModel READ desktopWindowBehaviorModel CONSTANT)
    Q_PROPERTY(QObject* desktopIntentModel READ desktopIntentModel CONSTANT)
    Q_PROPERTY(QObject* desktopTrayIconController READ desktopTrayIconController CONSTANT)
    Q_PROPERTY(MockOptionsModel* onboardingOptionsModel READ onboardingOptionsModel NOTIFY settingsModelsChanged)
    Q_PROPERTY(bool testAutomationEnabled READ testAutomationEnabled CONSTANT)
    Q_PROPERTY(QString startupError READ startupError NOTIFY startupErrorChanged)
    Q_PROPERTY(QAbstractItemModel* startupErrorDetails READ startupErrorDetails NOTIFY startupErrorChanged)
    Q_PROPERTY(bool startupErrorPending READ startupErrorPending NOTIFY startupErrorChanged)

public:
    MockStartupDiagnostics* diagnostics() const { return m_context->contextProperty(QStringLiteral("testStartupDiagnostics")).value<MockStartupDiagnostics*>(); }
    QAbstractItemModel* startupErrorDetails() const { auto* source = diagnostics(); return source && source->present() ? source : nullptr; }
    bool startupErrorPending() const { auto* source = diagnostics(); return source && source->busy(); }

    explicit MockApplicationModels(QQmlEngine* engine) : QObject(engine), m_context(engine->rootContext()) {
        if (auto* source = diagnostics()) connect(source, &MockStartupDiagnostics::projectionChanged, this, &MockApplicationModels::startupErrorChanged);
    }

    MockNodeModel* nodeModel() const { return m_context->contextProperty(QStringLiteral("nodeModel")).value<MockNodeModel*>(); }
    MockChainModel* chainModel() const { return m_context->contextProperty(QStringLiteral("chainModel")).value<MockChainModel*>(); }
    MockBlockClockModel* blockClockModel() const { return m_context->contextProperty(QStringLiteral("blockClockModel")).value<MockBlockClockModel*>(); }
    PresentationServices* presentationServices() const { return m_context->contextProperty(QStringLiteral("presentationServices")).value<PresentationServices*>(); }
    MockOptionsModel* optionsModel() const { return m_preinit ? nullptr : m_context->contextProperty(QStringLiteral("optionsModel")).value<MockOptionsModel*>(); }
    MockNetworkTrafficTower* networkTrafficTower() const { return m_context->contextProperty(QStringLiteral("networkTrafficTower")).value<MockNetworkTrafficTower*>(); }
    MockNetworkStatusModel* networkStatusModel() const { return m_context->contextProperty(QStringLiteral("networkStatusModel")).value<MockNetworkStatusModel*>(); }
    MockPeerTableModel* peerTableModel() const { return m_context->contextProperty(QStringLiteral("peerTableModel")).value<MockPeerTableModel*>(); }
    MockPeerListModelProxy* peerListModelProxy() const { return m_context->contextProperty(QStringLiteral("peerListModelProxy")).value<MockPeerListModelProxy*>(); }
    MockBanListModel* banListModel() const { return m_context->contextProperty(QStringLiteral("banListModel")).value<MockBanListModel*>(); }
    MockDebugLogModel* debugLogModel() const { return m_context->contextProperty(QStringLiteral("debugLogModel")).value<MockDebugLogModel*>(); }
    QObject* rpcConsoleModel() const { return m_context->contextProperty(QStringLiteral("rpcConsoleModel")).value<QObject*>(); }
    SignerDiscoveryModel* signers() const { return walletController()->signers(); }
    MockWalletController* walletController() const { return m_context->contextProperty(QStringLiteral("walletController")).value<MockWalletController*>(); }
    MockWalletListModel* walletListModel() const { return m_context->contextProperty(QStringLiteral("walletListModel")).value<MockWalletListModel*>(); }
    MockDesktopWindowBehaviorModel* desktopWindowBehaviorModel() const { return m_context->contextProperty(QStringLiteral("desktopWindowBehaviorModel")).value<MockDesktopWindowBehaviorModel*>(); }
    QObject* desktopIntentModel() const { return m_context->contextProperty(QStringLiteral("desktopIntentModel")).value<QObject*>(); }
    QObject* desktopTrayIconController() const { return m_context->contextProperty(QStringLiteral("desktopTrayIconController")).value<QObject*>(); }
    MockOptionsModel* onboardingOptionsModel() const { return !m_preinit ? nullptr : m_context->contextProperty(QStringLiteral("optionsModel")).value<MockOptionsModel*>(); }
    bool testAutomationEnabled() const { return m_context->contextProperty(QStringLiteral("testAutomationEnabled")).toBool(); }
    QString startupError() const { auto* source = diagnostics(); return source && !source->summary().isEmpty() ? source->summary() : m_context->contextProperty(QStringLiteral("message")).toString(); }
    Q_INVOKABLE void setPreInitForTest(bool preinit)
    {
        if (m_preinit == preinit) return;
        m_preinit = preinit;
        Q_EMIT settingsModelsChanged();
    }

Q_SIGNALS:
    void startupErrorChanged();
    void settingsModelsChanged();
private:
    bool m_preinit{false};
    QQmlContext* m_context;
};

// A fixture can observe physical deletion without keeping its QML objects alive.
// Include visual children: a Repeater delegate need not have the visual parent
// as its QObject parent.
class DestructionReceipt : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int pending READ pending NOTIFY pendingChanged)
    Q_PROPERTY(int count READ count CONSTANT)

public:
    DestructionReceipt(QObject* root, QObject* parent) : QObject(parent)
    {
        QList<QObject*> queue;
        if (root) queue.push_back(root);
        QSet<QObject*> visited;
        while (!queue.empty()) {
            auto* object = queue.takeLast();
            if (visited.contains(object)) continue;
            visited.insert(object);
            queue.append(object->children());
            if (auto* item = qobject_cast<QQuickItem*>(object)) {
                for (auto* child : item->childItems()) queue.push_back(child);
            }
            if (auto* window = qobject_cast<QQuickWindow*>(object)) queue.push_back(window->contentItem());
            connect(object, &QObject::destroyed, this, [this] {
                --m_pending;
                Q_EMIT pendingChanged();
            });
        }
        m_count = m_pending = static_cast<int>(visited.size());
    }

    int pending() const { return m_pending; }
    int count() const { return m_count; }

Q_SIGNALS:
    void pendingChanged();

private:
    int m_pending{0};
    int m_count{0};
};

// Reads what a screen reader receives for an item. Controls fill in their
// roles, names and states only once an assistive tool attaches. On older Qt
// (6.5 and earlier) activate() only reaches the items that already exist,
// and they don't track later changes, so tests call it after creating items
// and again after changing them.
class TestAccessibility : public QObject
{
    Q_OBJECT

public:
    // Returns whether accessibility is now active. Qt 6.5 and older only
    // notify observers here; without a platform bridge isActive() stays
    // false and controls never fill in their automatic names and states.
    Q_INVOKABLE bool activate()
    {
        QAccessible::setActive(true);
        return QAccessible::isActive();
    }
    Q_INVOKABLE void deactivate() { QAccessible::setActive(false); }
    Q_INVOKABLE QVariantMap info(QObject* object) const
    {
        QVariantMap result;
        QAccessibleInterface* iface = object ? QAccessible::queryAccessibleInterface(object) : nullptr;
        if (!iface) return result;
        const QAccessible::State state = iface->state();
        result[QStringLiteral("role")] = int(iface->role());
        result[QStringLiteral("name")] = iface->text(QAccessible::Name);
        result[QStringLiteral("description")] = iface->text(QAccessible::Description);
        result[QStringLiteral("checkable")] = bool(state.checkable);
        result[QStringLiteral("checked")] = bool(state.checked);
        return result;
    }
};

class TestImage : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE QColor pixelAt(const QImage& image, const QPointF& point, qreal logical_width) const
    {
        if (image.isNull() || logical_width <= 0) return {};
        // The grab has device pixels; the sample point uses item coordinates.
        const qreal ratio = image.width() / logical_width;
        return image.pixelColor(qRound(point.x() * ratio), qRound(point.y() * ratio));
    }
};

// QML can't list a resource folder; the design system test needs the
// shared controls and components to check that each has a showcase entry.
class TestResources : public QObject
{
    Q_OBJECT

public:
    Q_INVOKABLE QStringList qmlTypes(const QString& folder) const
    {
        QStringList names;
        const QDir dir(QStringLiteral(":/qml/") + folder);
        for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.qml")}, QDir::Files, QDir::Name)) {
            names << info.completeBaseName();
        }
        return names;
    }
};

class QmlTestsSetup : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supportsApplicationFonts READ supportsApplicationFonts CONSTANT)

public:
    bool hadWarnings() const { return m_had_warnings; }
    // The minimal platform cannot register application fonts. FontLoader
    // fixtures use the platform's fallback fonts there.
    bool supportsApplicationFonts() const { return QGuiApplication::platformName() != QStringLiteral("minimal"); }

public Q_SLOTS:
    QString localeName() const { return QLocale{}.name(); }
    void setLocale(const QString& name) { QLocale::setDefault(QLocale{name}); }
    QObject* watchDestruction(QObject* root) { return new DestructionReceipt(root, this); }
    void releaseDestructionWatch(QObject* receipt)
    {
        if (receipt && receipt->parent() == this) delete qobject_cast<DestructionReceipt*>(receipt);
    }
    int namedChildCount(QObject* object, const QString& name) const { return object ? object->findChildren<QObject*>(name).size() : 0; }
    QObject* findVisualDescendant(QObject* root, const QString& name) const
    {
        QList<QObject*> pending;
        QSet<QObject*> visited;
        if (root) pending.push_back(root);
        while (!pending.empty()) {
            auto* object = pending.takeLast();
            if (visited.contains(object)) continue;
            visited.insert(object);
            if (object->objectName() == name) return object;
            pending.append(object->children());
            if (auto* item = qobject_cast<QQuickItem*>(object)) {
                for (auto* child : item->childItems()) pending.push_back(child);
            }
            if (auto* window = qobject_cast<QQuickWindow*>(object)) pending.push_back(window->contentItem());
        }
        return nullptr;
    }
    QString entryFieldText(QObject* object, const QString& field) const {
        const auto* model = qobject_cast<QAbstractItemModel*>(object); QString text;
        if (model) for (int row = 0; row < model->rowCount(); ++row) {
            const auto entry = model->data(model->index(row, 0), Qt::UserRole).toMap();
            if (entry.value("field").toString() == field) text += entry.value("content").toString();
        }
        return text;
    }
    QString nativeClipboardText() const { return QGuiApplication::clipboard()->text(); }
    void setNativeClipboardText(const QString& text) { QGuiApplication::clipboard()->setText(text); }
    QVariantMap prepareTransactionFlow(const QVariantMap& input) {
        auto flow = input; flow["presentation"] = PrepareTransactionFlowPresentation(flow); return flow;
    }
    bool replaceText(QObject* object, const QString& text)
    {
        auto* item = qobject_cast<QQuickItem*>(object);
        if (!item || !item->window()) return false;
        item->forceActiveFocus();
        if (!QMetaObject::invokeMethod(item, "selectAll")) return false;
        QTest::keyClick(item->window(), Qt::Key_Backspace, Qt::NoModifier, 1);
        QInputMethodEvent edit;
        edit.setCommitString(text);
        QCoreApplication::sendEvent(item, &edit);
        return edit.isAccepted();
    }

    void failOnWarnings()
    {
        // QTest::failOnWarning() was added in Qt 6.3. Keep this a no-op on Qt 6.2.
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
        // The minimal plugin cannot register application fonts or raise windows.
        // Fatal error tests call raise() to bring the error window forward.
        // Allow only the two exact plugin messages. Other Qt warnings still fail
        // this test, and qmlEngineAvailable() rejects all QML engine warnings.
        QTest::failOnWarning(QRegularExpression(QStringLiteral(
            "^(?!This plugin does not support (?:application fonts|raise\\(\\))$).*")));
#endif
    }

    void applicationAvailable()
    {
        if (!m_settings.isValid()) qFatal("Cannot create QML test settings directory");
        QCoreApplication::setOrganizationName(QStringLiteral("BitcoinCoreQmlTests"));
        QCoreApplication::setOrganizationDomain(QStringLiteral("bitcoincore.org"));
        QCoreApplication::setApplicationName(QStringLiteral("bitcoinqml_qmltests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, m_settings.path());
        m_qr_executor = std::make_unique<BackendExecutor>(execution::Options{});
        MockFeeServices() = {m_qr_executor->scheduler(), [this](std::function<void()> work) { QMetaObject::invokeMethod(this, std::move(work), Qt::QueuedConnection); }};
        qmlRegisterUncreatableMetaObject(SettingsSaveResult::staticMetaObject,"org.bitcoincore.qt",1,0,"SettingsSaveResult","Retained settings outcome");
        qRegisterMetaType<SettingsSaveResult>();
        qmlRegisterUncreatableType<SettingsEditorModel>("org.bitcoincore.qt",1,0,"SettingsEditorModel","Actual controlled editor");
        qmlRegisterUncreatableType<SettingsEntryModel>("org.bitcoincore.qt",1,0,"SettingsEntryModel","Actual controlled setting");
        qmlRegisterUncreatableType<FeeSelectionModel>("org.bitcoincore.qt", 1, 0, "FeeSelectionModel", "Owned fee policy");
        qmlRegisterUncreatableType<FeeRateEntry>("org.bitcoincore.qt", 1, 0, "FeeRateEntry", "Owned fee input");
        m_presentation_services = std::make_unique<PresentationServices>(PresentationServices::Ports{m_qr_executor->scheduler(),
            [this](std::function<void()> work) { QMetaObject::invokeMethod(this, std::move(work), Qt::QueuedConnection); },
            [this](std::function<void()> work) { return m_qr_executor->submit(this, std::move(work), [] {}); }, {}});
        qmlRegisterType<MonospaceOutputModel>("org.bitcoincore.qt", 1, 0, "MonospaceOutputModel");
        static ButtonKeyActivation button_key_activation;
        // Exercise the same customizable controls used by the application.
        qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");
    }

    void processDeferredDeletes()
    {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    Q_INVOKABLE UiPreferencesStore* beginControlledPreferences(QObject* context, bool refuse_read) {
        if (m_controlled_preferences || !context) return nullptr;
        auto* engine = qmlEngine(context); if (!engine) return nullptr;
        auto fixture = std::make_unique<ControlledPreferences>(); fixture->engine = engine;
        fixture->original = engine->rootContext()->contextProperty(QStringLiteral("uiPreferencesStore")).value<UiPreferencesStore*>();
        UiPreferencesNamespace identity; identity.file = fixture->directory.filePath(QStringLiteral("preferences.ini"));
        auto* state = fixture.get();
        fixture->store = new UiPreferencesStore{{fixture->transport.scheduler, [state](auto turn) { state->gui.push_back(std::move(turn)); }}, identity, engine};
        QObject::connect(fixture->store, &UiPreferencesStore::stateChanged, fixture->store, [state] {
            const auto receipt = state->store->lastWriteReceipt();
            if (receipt && receipt != state->last_write) {
                state->last_write = receipt;
                ++state->write_count;
            }
        });
        engine->rootContext()->setContextProperty(QStringLiteral("uiPreferencesStore"), fixture->store.data());
        if (refuse_read) fixture->transport.rejectLane(execution::Lane::Core);
        m_controlled_preferences = std::move(fixture); m_controlled_preferences->store->start();
        return m_controlled_preferences->store;
    }
    Q_INVOKABLE bool stepControlledPreferences(const QString& lane) {
        if (!m_controlled_preferences) return false;
        auto& fixture = *m_controlled_preferences;
        if (lane == QStringLiteral("core")) return fixture.transport.step(execution::Lane::Core);
        if (lane == QStringLiteral("domain")) return fixture.transport.step(execution::Lane::Domain);
        if (lane != QStringLiteral("gui") || fixture.gui.empty()) return false;
        auto turn = std::move(fixture.gui.front()); fixture.gui.pop_front(); turn(); return true;
    }
    Q_INVOKABLE bool runControlledPreferenceWork() {
        if (!m_controlled_preferences) return false;
        for (int turn = 0; turn < 4096; ++turn) {
            if (m_controlled_preferences->transport.queuedIdle()) return true;
            if (!m_controlled_preferences->transport.stepNext()) return false;
        }
        return false;
    }
    Q_INVOKABLE quint64 controlledPreferenceOperationId() const {
        return m_controlled_preferences && m_controlled_preferences->store ? m_controlled_preferences->store->operationId() : 0;
    }
    Q_INVOKABLE quint64 controlledPreferenceWriteCount() const {
        return m_controlled_preferences ? m_controlled_preferences->write_count : 0;
    }
    Q_INVOKABLE bool settleControlledPreferences() {
        if (!m_controlled_preferences) return false;
        auto& fixture = *m_controlled_preferences;
        for (int turn = 0; turn < 4096; ++turn) {
            if (!fixture.transport.queuedIdle()) { if (!fixture.transport.stepNext()) return false; continue; }
            if (!fixture.gui.empty()) { auto next = std::move(fixture.gui.front()); fixture.gui.pop_front(); next(); continue; }
            // A live store deliberately retains scheduler resource leases.
            // Settling work/publication is distinct from retiring the store.
            return fixture.transport.transportIdle();
        }
        return false;
    }
    Q_INVOKABLE bool endControlledPreferences() {
        if (!m_controlled_preferences) return true;
        auto& fixture = *m_controlled_preferences;
        if (fixture.store) fixture.store->closeAdmission();
        const bool closed = settleControlledPreferences();
        if (fixture.engine) fixture.engine->rootContext()->setContextProperty(QStringLiteral("uiPreferencesStore"), fixture.original.data());
        delete fixture.store.data(); fixture.store = nullptr;
        const bool retired = settleControlledPreferences(); const bool drained = fixture.transport.quiescent(); m_controlled_preferences.reset();
        return closed && retired && drained;
    }

    Q_INVOKABLE QVariant blockClockSnapshot(const QList<qreal>& fractions) const
    {
        auto frame = std::make_shared<blockclock::Projection>(); frame->fractions = fractions;
        m_block_clock_fixture = std::move(frame);
        return QVariant::fromValue(blockclock::WeakSnapshot{m_block_clock_fixture});
    }

    int incubatingObjectCount(QObject* object) const
    {
        const auto* engine = qmlEngine(object);
        const auto* controller = engine ? engine->incubationController() : nullptr;
        return controller ? controller->incubatingObjectCount() : 0;
    }

    void cleanupTestCase()
    {
        if (!endControlledPreferences()) m_had_warnings = true;
        for(const auto& store:m_preferences_stores)if(store)store->closeAdmission();
        for (const auto& owner : MockFeeOwners()) if (owner) owner->close();
        for(const auto& editor:MockSettingsOwners())if(editor){editor->close();editor->deleteLater();}
        MockFeeServices() = {};
        QEventLoop loop;
        bool retired{false};
        m_presentation_services->close();
        m_presentation_services->whenRetired(&loop, [&] { retired = true; loop.quit(); });
        while (!retired) loop.exec();
        bool drained{false};
        BackendExecutor::shutdownAll(&loop, [&] { drained = true; loop.quit(); });
        while (!drained) loop.exec();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }

    void qmlEngineAvailable(QQmlEngine* engine)
    {
        QString coverage_error;
        if (!QmlCoverage::install(engine, &coverage_error)) {
            m_had_warnings = true;
            std::cerr << coverage_error.toStdString() << '\n';
            return;
        }
        // Match failOnWarnings(): Qt 6.2 skips the new component warning policy.
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
        // Qt Test's per-function warning policy does not cover QML loading or
        // engine destruction. Observe both phases so these warnings also fail.
        connect(engine, &QQmlEngine::warnings, engine, [this](const QList<QQmlError>& warnings) {
            for (const auto& warning : warnings) {
                m_had_warnings = true;
                std::cerr << "Unexpected QML warning: " << warning.toString().toStdString() << '\n';
            }
        });
#endif
        engine->rootContext()->setContextProperty(QStringLiteral("testStartupDiagnostics"), new MockStartupDiagnostics(engine));
        auto* preferences=new UiPreferencesStore{{m_qr_executor->scheduler(),[this](auto turn){QMetaObject::invokeMethod(this,std::move(turn),Qt::QueuedConnection);}},UiPreferencesNamespace::Capture(),engine};
        m_preferences_stores.emplace_back(preferences);
        engine->rootContext()->setContextProperty(QStringLiteral("uiPreferencesStore"),preferences);
        preferences->start();
        QEventLoop preferences_loop;
        connect(preferences,&UiPreferencesStore::stateChanged,&preferences_loop,[&]{if(preferences->ready()||!preferences->error().isEmpty())preferences_loop.quit();});
        while(!preferences->ready()&&preferences->error().isEmpty())preferences_loop.exec();
        engine->rootContext()->setContextProperty(QStringLiteral("testSetup"), this);
        engine->addImageProvider(QStringLiteral("images"), new TestIconProvider);
        engine->addImageProvider(QStringLiteral("qr"), new QRImageProvider{m_qr_executor->scheduler(), QRImageProvider::RenderImage, engine});
        engine->addImportPath(QStringLiteral(BITCOINQML_QML_TEST_MOCKS_DIR));
        static MockAppMode app_mode;
        static MockBitcoinUri bitcoin_uri;
        static TestAccessibility test_accessibility;
        static TestImage test_image;
        static TestResources test_resources;
        static MockBuildInfo build_info;
        static MockOptionsModel options_model;
        static MockChainModel chain_model;
        static MockBlockClockModel block_clock_model;
        static MockNodeModel node_model;
        static MockPeerTableModel peer_table_model;
        static MockNetworkTrafficTower network_traffic_tower;
        static MockNetworkStatusModel network_status_model;
        static MockPeerListModelProxy peer_list_model_proxy;
        static MockBanListModel ban_list_model;
        static MockPeerDetailsModel peer_details_model;
        static MockPeerDetailsModel other_peer_details_model;
        other_peer_details_model.m_node_id = 8;
        other_peer_details_model.m_raw_address = QStringLiteral("192.0.2.1");
        other_peer_details_model.m_address = QStringLiteral("192.0.2.1:8333");
        QQmlEngine::setObjectOwnership(&peer_details_model, QQmlEngine::CppOwnership);
        peer_details_model.setCommands(&node_model);
        other_peer_details_model.setCommands(&node_model);
        peer_list_model_proxy.setPeerDetailsForTest(&peer_details_model, &other_peer_details_model);
        node_model.m_refresh_peer_inventories = [] { peer_table_model.refresh(); ban_list_model.refresh(); };
        static MockWalletQmlModelTransaction wallet_transaction;
        static MockPaymentRequest payment_request;
        static MockSendRecipient send_recipient;
        static MockRecipientsModel recipients_model;
        static MockCoinsListModel coins_list_model;
        static MockWalletQmlModel wallet_model;
        static MockWalletController wallet_controller;
        options_model.setSettingsSignerForTest(&wallet_controller);
        static MockWalletListModel wallet_list_model;
        static MockTransactionActivityModel transaction_activity_model;
        static MockBumpTransactionModel bump_model;
        static MockSignVerifyMessageModel sign_verify_model;
        static MockDesktopWindowBehaviorModel desktop_window_behavior_model;
        static MockDesktopTrayIconController desktop_tray_icon_controller;
        static MockRpcConsoleModel rpc_console_model;
        static MockDebugLogModel debug_log_model;
        static MockUrlOpener url_opener;
        recipients_model.setCurrent(&send_recipient);
        wallet_model.setTransactionActivityModel(&transaction_activity_model);
        wallet_model.setBumpModel(&bump_model);
        wallet_model.setSignVerifyMessageModel(&sign_verify_model);
        wallet_model.setRecipients(&recipients_model);
        wallet_model.setCoinsListModel(&coins_list_model);
        wallet_model.setCurrentTransaction(&wallet_transaction);
        wallet_model.setCurrentPaymentRequest(&payment_request);
        wallet_controller.setSelectedWalletObject(&wallet_model);
        // A second, fully independent wallet for per-wallet view tests.
        static MockWalletQmlModelTransaction second_wallet_transaction;
        static MockPaymentRequest second_payment_request;
        static MockSendRecipient second_send_recipient;
        static MockRecipientsModel second_recipients_model;
        static MockCoinsListModel second_coins_list_model;
        static MockTransactionActivityModel second_transaction_activity_model;
        static MockBumpTransactionModel second_bump_model;
        static MockSignVerifyMessageModel second_sign_verify_model;
        static MockWalletQmlModel second_wallet_model;
        second_recipients_model.setCurrent(&second_send_recipient);
        second_wallet_model.setTransactionActivityModel(&second_transaction_activity_model);
        second_wallet_model.setBumpModel(&second_bump_model);
        second_wallet_model.setSignVerifyMessageModel(&second_sign_verify_model);
        second_wallet_model.setRecipients(&second_recipients_model);
        second_wallet_model.setCoinsListModel(&second_coins_list_model);
        second_wallet_model.setCurrentTransaction(&second_wallet_transaction);
        second_wallet_model.setCurrentPaymentRequest(&second_payment_request);
        second_wallet_model.setProperty("name", QStringLiteral("second"));
        qmlRegisterSingletonType<MockApplicationModels>("org.bitcoincore.qt", 1, 0, "ApplicationModels",
            [](QQmlEngine* qml_engine, QJSEngine*) -> QObject* { return new MockApplicationModels(qml_engine); });
        qmlRegisterSingletonInstance<MockAppMode>("org.bitcoincore.qt", 1, 0, "AppMode", &app_mode);
        qmlRegisterSingletonInstance<MockBuildInfo>("org.bitcoincore.qt", 1, 0, "BuildInfo", &build_info);
        qmlRegisterSingletonInstance<MockUrlOpener>("org.bitcoincore.qt", 1, 0, "UrlOpener", &url_opener);
        qmlRegisterUncreatableType<MockRuntimePrompt>("org.bitcoincore.qt",1,0,"RuntimePromptModel","Original mock prompt");
        qmlRegisterUncreatableType<MockNodeLifecycle>("org.bitcoincore.qt",1,0,"NodeLifecycle","Lifecycle values");
        qmlRegisterSingletonType<GlobalSettings>("org.bitcoincore.qt", 1, 0, "GlobalSettings", [](QQmlEngine*, QJSEngine*) -> QObject* {
            return new GlobalSettings(QCoreApplication::applicationName());
        });
        qmlRegisterSingletonType<MotionPreference>("org.bitcoincore.qt", 1, 0, "MotionPreference", [](QQmlEngine*, QJSEngine*) -> QObject* {
            return new MotionPreference(QStringLiteral("off"));
        });
        qmlRegisterUncreatableType<MockPeerDetailsModel>(
            "org.bitcoincore.qt",
            1,
            0,
            "PeerDetailsModel",
            "Test stub type"
        );
        qmlRegisterUncreatableType<MockPeerCommandOperation>("org.bitcoincore.qt", 1, 0, "PeerCommandOperation", "Captured peer operation");
        qmlRegisterType<MockPeerSelectionModel>("org.bitcoincore.qt", 1, 0, "PeerSelectionModel");
        qRegisterMetaType<MockPeerCommandResult>();
        qmlRegisterUncreatableMetaObject(MockPeerCommandResult::staticMetaObject, "org.bitcoincore.qt", 1, 0, "PeerCommandResult", "Retained peer result");
        qmlRegisterUncreatableType<MockNodeModel>("org.bitcoincore.qt", 1, 0, "NodeModel", "Provided by ApplicationModels");
        qmlRegisterType<BitcoinAmount>("org.bitcoincore.qt", 1, 0, "BitcoinAmount");
        RegisterReceiveTypes();
        qmlRegisterType<ReceiveQmlFixture>("org.bitcoincore.qt", 1, 0, "ControlledReceiveFixture");
        qmlRegisterType<OwnedQrImage>("org.bitcoincore.qt", 1, 0, "OwnedQrImage");
        qmlRegisterType<MockBitcoinAddress>("org.bitcoincore.qt", 1, 0, "BitcoinAddress");
        qRegisterMetaType<ActivityExportResult>();
        qmlRegisterUncreatableType<ActivityExportOperation>("org.bitcoincore.qt", 1, 0, "ActivityExportOperation", "Owned by the original export action");
        qmlRegisterType<MockActivityFilterProxyModel>("org.bitcoincore.qt", 1, 0, "ActivityFilterProxyModel");
        qmlRegisterUncreatableType<MockTransactionActivityModel>("org.bitcoincore.qt", 1, 0, "TransactionActivityModel", "Test fixture");
        qmlRegisterUncreatableType<MockActivityDetailModel>("org.bitcoincore.qt",1,0,"ActivityDetailModel","Per-view factory");
        qmlRegisterType<AppPreferences>("org.bitcoincore.qt",1,0,"AppPreferences");
        qmlRegisterUncreatableType<MockCoreSettingsModel>("org.bitcoincore.qt",1,0,"CoreSettingsModel","Test fixture");
        qmlRegisterUncreatableType<MockCoreProxyDraft>("org.bitcoincore.qt",1,0,"CoreProxyDraft","Test fixture");
        qmlRegisterUncreatableType<MockOptionsTransactionLinks>("org.bitcoincore.qt", 1, 0, "OptionsTransactionLinks", "Test fixture");
        qmlRegisterType<MockSignVerifyMessageModel>("org.bitcoincore.qt", 1, 0, "SignVerifyMessageModel");
        qmlRegisterUncreatableType<MessageSigningPrompt>("org.bitcoincore.qt", 1, 0, "MessageSigningPrompt", "Owned by signing attempt");
        qmlRegisterType<ExactTextDocument>("org.bitcoincore.qt", 1, 0, "ExactTextDocument");
        qmlRegisterUncreatableType<TextEditSessionModel>("org.bitcoincore.qt", 1, 0, "TextEditSessionModel", "Owned by editor factory");
        qmlRegisterUncreatableType<TextEditInput>("org.bitcoincore.qt", 1, 0, "TextEditInput", "Owned by editor");
        qmlRegisterUncreatableType<TextEditClosePrompt>("org.bitcoincore.qt", 1, 0, "TextEditClosePrompt", "Owned by editor");
        qRegisterMetaType<TextEditError>(); qRegisterMetaType<TextEditSaveResult>(); qRegisterMetaType<TextEditConflict>(); qRegisterMetaType<QList<TextEditConflict>>();
        qRegisterMetaType<MessageSigningError>(); qRegisterMetaType<MessageVerificationResult>();
        qmlRegisterUncreatableType<MockAddressListModel>("org.bitcoincore.qt", 1, 0, "AddressListModel", "Test stub type");
        qmlRegisterType<MockPaymentRequest>("org.bitcoincore.qt", 1, 0, "PaymentRequest");
        qmlRegisterUncreatableType<MockImageExportOperation>("org.bitcoincore.qt", 1, 0, "ImageExportOperation", "Test fixture");
        qmlRegisterUncreatableType<MockReceiveRequestOperation>("org.bitcoincore.qt", 1, 0, "ReceiveRequestOperation", "Test fixture");
        qmlRegisterUncreatableType<MockTransaction>("org.bitcoincore.qt", 1, 0, "Transaction", "Test stub type");
        qmlRegisterUncreatableType<MockSendRecipient>("org.bitcoincore.qt", 1, 0, "SendRecipient", "Test stub type");
        qmlRegisterUncreatableType<MockBumpTransactionModel>("org.bitcoincore.qt", 1, 0, "BumpTransactionModel", "Test stub type");
        qmlRegisterType<MockBumpTransactionModel>("org.bitcoincore.qt", 1, 0, "TestBumpTransactionModel");
        qmlRegisterUncreatableType<MockBackupFileOperation>("org.bitcoincore.qt", 1, 0, "BackupFileOperation", "Test stub type");
        qmlRegisterUncreatableType<MockFileExportOperation>("org.bitcoincore.qt", 1, 0, "FileExportOperationModel", "Test stub type");
        qmlRegisterUncreatableType<MockWalletSecurity>("org.bitcoincore.qt", 1, 0, "WalletSecurityModel", "Test fixture");
        qmlRegisterUncreatableType<MockSecurityOperation>("org.bitcoincore.qt", 1, 0, "SecurityOperationModel", "Test fixture");
        qmlRegisterUncreatableType<MockWalletQmlModel>("org.bitcoincore.qt", 1, 0, "WalletQmlModel", "Test stub type");
        qmlRegisterSingletonInstance<MockBitcoinUri>("org.bitcoincore.qt", 1, 0, "BitcoinUri", &bitcoin_uri);
        qmlRegisterUncreatableType<ImportOperation>("org.bitcoincore.qt", 1, 0, "ImportOperation", "Test stub type");
        qmlRegisterUncreatableType<ImportTarget>("org.bitcoincore.qt", 1, 0, "ImportTarget", "Test stub type");
        qmlRegisterUncreatableType<MockPaymentImportOperation>("org.bitcoincore.qt", 1, 0, "PaymentImportOperation", "Test stub type");
        qmlRegisterType<MockWalletImportTarget>("org.bitcoincore.qt", 1, 0, "WalletImportTarget");
        qmlRegisterType<SendImportModel>("org.bitcoincore.qt", 1, 0, "SendImportModel");
        qmlRegisterUncreatableType<MockWalletCreationModel>("org.bitcoincore.qt", 1, 0, "WalletCreationModel", "Test creation editor");
        qmlRegisterUncreatableType<MockWalletCreationText>("org.bitcoincore.qt", 1, 0, "WalletCreationText", "Test creation input");
        qmlRegisterUncreatableType<SignerDiscoveryModel>("org.bitcoincore.qt", 1, 0, "SignerDiscoveryModel", "Actual controlled signer projection");
        qmlRegisterUncreatableMetaObject(SignerDiscoveryError::staticMetaObject, "org.bitcoincore.qt", 1, 0, "SignerDiscoveryError", "Error value");
    qRegisterMetaType<SignerDiscoveryError>();
        qmlRegisterUncreatableType<MockWalletOperationModel>("org.bitcoincore.qt", 1, 0, "WalletOperationModel", "Test immutable operation");
        qmlRegisterUncreatableType<MockWalletOperationPrompt>("org.bitcoincore.qt", 1, 0, "WalletOperationPrompt", "Test original prompt");
        qmlRegisterType<WalletOperationPresentationModel>("org.bitcoincore.qt", 1, 0, "WalletOperationPresentationModel");
        qRegisterMetaType<WalletCreationReceipt>(); qRegisterMetaType<WalletOperationError>();
        qmlRegisterUncreatableType<MockWalletProvisioningCheck>("org.bitcoincore.qt", 1, 0, "WalletProvisioningCheck", "Test stub type");
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
        qmlRegisterType<MockFileDialog>("org.bitcoincore.qt", 1, 0, "AppFileDialog");
        qmlRegisterType<BlockClockDial>("org.bitcoincore.qt", 1, 0, "BlockClockDial");
        qmlRegisterUncreatableType<PresentationServices>("org.bitcoincore.qt", 1, 0, "PresentationServices", "Owned by the test application");
        qmlRegisterType<LineGraph>("org.bitcoincore.qt", 1, 0, "LineGraph");
        engine->rootContext()->setContextProperty(QStringLiteral("optionsModel"), &options_model);
        engine->rootContext()->setContextProperty(QStringLiteral("chainModel"), &chain_model);
        engine->rootContext()->setContextProperty(QStringLiteral("blockClockModel"), &block_clock_model);
        engine->rootContext()->setContextProperty(QStringLiteral("presentationServices"), m_presentation_services.get());
        engine->rootContext()->setContextProperty(QStringLiteral("nodeModel"), &node_model);
        engine->rootContext()->setContextProperty(QStringLiteral("debugLogModel"), &debug_log_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testUrlOpener"), &url_opener);
        engine->rootContext()->setContextProperty(QStringLiteral("peerTableModel"), &peer_table_model);
        engine->rootContext()->setContextProperty(QStringLiteral("networkTrafficTower"), &network_traffic_tower);
        engine->rootContext()->setContextProperty(QStringLiteral("testNetworkTrafficTower"), &network_traffic_tower);
        engine->rootContext()->setContextProperty(QStringLiteral("networkStatusModel"), &network_status_model);
        engine->rootContext()->setContextProperty(QStringLiteral("peerListModelProxy"), &peer_list_model_proxy);
        engine->rootContext()->setContextProperty(QStringLiteral("banListModel"), &ban_list_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testPeerDetailsModel"), &peer_details_model);
        engine->rootContext()->setContextProperty(QStringLiteral("otherPeerDetailsModel"), &other_peer_details_model);
        engine->rootContext()->setContextProperty(QStringLiteral("walletController"), &wallet_controller);
        engine->rootContext()->setContextProperty(QStringLiteral("walletListModel"), &wallet_list_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testWalletModel"), &wallet_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testSecondWalletModel"), &second_wallet_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testWalletTransaction"), &wallet_transaction);
        engine->rootContext()->setContextProperty(QStringLiteral("testPaymentRequest"), &payment_request);
        engine->rootContext()->setContextProperty(QStringLiteral("testTransactionActivityModel"), &transaction_activity_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testSendRecipient"), &send_recipient);
        engine->rootContext()->setContextProperty(QStringLiteral("testAutomationEnabled"), QVariant{false});
        engine->rootContext()->setContextProperty(QStringLiteral("testRecipientsModel"), &recipients_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testCoinsListModel"), &coins_list_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testBumpModel"), &bump_model);
        engine->rootContext()->setContextProperty(QStringLiteral("desktopWindowBehaviorModel"), &desktop_window_behavior_model);
        engine->rootContext()->setContextProperty(QStringLiteral("desktopTrayIconController"), &desktop_tray_icon_controller);
        engine->rootContext()->setContextProperty(QStringLiteral("rpcConsoleModel"), &rpc_console_model);
        engine->rootContext()->setContextProperty(QStringLiteral("testAccessibility"), &test_accessibility);
        engine->rootContext()->setContextProperty(QStringLiteral("testImage"), &test_image);
        engine->rootContext()->setContextProperty(QStringLiteral("testResources"), &test_resources);
        engine->rootContext()->setContextProperty(QStringLiteral("testDebugLogModel"), &debug_log_model);
        engine->addImportPath(QStringLiteral(BITCOINQML_QML_SOURCE_DIR));
    }

private:
    struct ControlledPreferences {
        ManualRoutedExecution transport;
        std::deque<std::function<void()>> gui;
        QTemporaryDir directory;
        QPointer<QQmlEngine> engine;
        QPointer<UiPreferencesStore> original, store;
        std::shared_ptr<const std::atomic<UiPreferencesStore::Outcome>> last_write;
        quint64 write_count{0};
    };
    std::unique_ptr<ControlledPreferences> m_controlled_preferences;
    std::vector<QPointer<UiPreferencesStore>> m_preferences_stores;
    mutable blockclock::Snapshot m_block_clock_fixture;
    std::unique_ptr<BackendExecutor> m_qr_executor;
    std::unique_ptr<PresentationServices> m_presentation_services;
    QTemporaryDir m_settings;
    bool m_had_warnings{false};
};

int main(int argc, char* argv[])
{
    // Transition animations are irrelevant to assertions and make QML runs flaky.
    qputenv("BITCOIN_QML_TEST_NO_ANIMATIONS", "1");
#if defined(BITCOINQML_QML_TEST_DEV_MODE) && BITCOINQML_QML_TEST_DEV_MODE
    if (QmlCoverage::requested()) {
        std::cerr << "QML coverage requires QML_TEST_DEV_MODE=OFF (embedded resources)." << '\n';
        return EXIT_FAILURE;
    }
#endif
    QString coverage_error;
    if (!QmlCoverage::prepare(&coverage_error)) {
        std::cerr << coverage_error.toStdString() << '\n';
        return EXIT_FAILURE;
    }
#if !defined(BITCOINQML_QML_TEST_DEV_MODE) || !BITCOINQML_QML_TEST_DEV_MODE
    if (QmlCoverage::requested()) {
        Q_CLEANUP_RESOURCE(bitcoin_qml);
        Q_CLEANUP_RESOURCE(bitcoin_compat);
        if (!QmlCoverage::registerResources(&coverage_error)) {
            std::cerr << coverage_error.toStdString() << '\n';
            return EXIT_FAILURE;
        }
    }
#endif
    QmlTestsSetup setup;
    const int status = quick_test_main_with_setup(argc, argv, "bitcoinqml_qmltests", QUICK_TEST_SOURCE_DIR, &setup);
    if (!QmlCoverage::finish(&coverage_error)) {
        std::cerr << coverage_error.toStdString() << '\n';
        return EXIT_FAILURE;
    }
    if (status != 0) return status;
    return setup.hadWarnings() ? EXIT_FAILURE : EXIT_SUCCESS;
}

#include <qml_tests_main.moc>
