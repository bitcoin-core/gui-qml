// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_ACTIVITYFILTERPROXYMODEL_H
#define BITCOIN_QML_MODELS_ACTIVITYFILTERPROXYMODEL_H

#include <consensus/amount.h>

#include <QByteArray>
#include <QDate>
#include <QLocale>
#include <QTimeZone>
#include <QHash>
#include <QList>
#include <QModelIndex>
#include <QAbstractProxyModel>
#include <QTimer>
#include <QPointer>
#include <qml/models/activitysnapshot.h>
#include <qml/models/activityexportoperation.h>
#include <qml/snapshotquery.h>
#include <QString>

#include <optional>

class ActivityFilterProxyModel : public QAbstractProxyModel
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
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

    using CsvResult = activityexport::Result;
    using CsvRequest = activityexport::Request;
    struct Services {
        execution::SnapshotServices execution;
        std::function<QDate()> today;
        std::function<CsvResult(const CsvRequest&)> write;
        std::function<QLocale()> locale{};
        std::function<QTimeZone()> timezone{};
        bool automaticDateRefresh{true};
    };
    explicit ActivityFilterProxyModel(QObject* parent = nullptr);
    ActivityFilterProxyModel(Services services, QObject* parent = nullptr);
    ~ActivityFilterProxyModel() override;
    bool busy() const { return m_scheduled || bool(m_job) || m_source_loading; }
    QString error() const { return m_error.isEmpty() ? m_source_error : m_error; }
    bool exportChoosing() const { return bool(m_chooser_snapshot); }
    bool exportBusy() const { return exportChoosing() || bool(m_export) || (m_export_operation && (m_export_operation->pending() || m_export_operation->confirmationRequired())); }
    ActivityExportOperation* exportOperation() const { return m_export_operation; }
    ActivityExportResult exportResult() const { return m_export_result; }
    bool exportRejected() const { return m_export_rejected; }
    QString exportError() const { return m_export_error; }
    QString exportedPath() const { return m_exported_path; }
    void closeAdmission();
    struct WorkReport { size_t examined{0}, prepared{0}, patches{0}, turns{0}, largestTurn{0}; bool delta{false}; };
    WorkReport lastWork() const { return m_last_work; }
    // Bounded clock handoff; manual clocks call this explicitly.
    void refreshDate();
    Q_INVOKABLE void clearDateFilter();
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 1; }
    QModelIndex index(int row, int column = 0, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex&) const override { return {}; }
    QModelIndex mapToSource(const QModelIndex& proxy_index) const override;
    QModelIndex mapFromSource(const QModelIndex& source_index) const override;

    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    void setSourceModel(QAbstractItemModel* source_model) override;

    QString searchText() const;
    void setSearchText(const QString& search_text);

    DateFilter dateFilter() const;
    void setDateFilter(DateFilter date_filter);

    TypeFilter typeFilter() const;
    void setTypeFilter(TypeFilter type_filter);

    QList<int> typeFilters() const { return m_type_filters; }
    void setTypeFilters(const QList<int>& types);

    int displayUnit() const;
    void setDisplayUnit(int display_unit);

    CAmount minAmount() const;
    void setMinAmount(CAmount min_amount);

    CAmount maxAmount() const { return m_max_amount; }
    void setMaxAmount(CAmount max_amount);
    CAmount availableMaxAmount() const { return m_available_max_amount; }
    Q_INVOKABLE bool setAmountRange(qint64 minimum, qint64 maximum);

    QDate rangeStart() const;
    QDate rangeEnd() const;

    // Apply a custom date range from ISO yyyy-MM-dd strings, selecting the
    // CustomRange date filter with it. QML passes strings rather than assigning
    // the QDate properties directly because the JavaScript Date to QDate
    // conversion shifts the day across time zones. Returns false, changing
    // nothing, when either date is unparseable or the range is inverted; the
    // range is only ever set through here, which is why the two date properties
    // are read-only.
    Q_INVOKABLE bool applyCustomRange(const QString& start_iso, const QString& end_iso);

    int count() const;
    int transactionCount() const;
    int requestCount() const;
    CAmount pendingBalanceSat() const;
    QString pendingBalance() const;
    GroupBy groupBy() const { return m_group_by; }
    void setGroupBy(GroupBy group_by);

    Q_INVOKABLE QString captureExport();
    Q_INVOKABLE void cancelExport(const QString& token);
    Q_INVOKABLE quint64 requestCapturedExportCsv(const QString& token, const QString& path);
    Q_INVOKABLE quint64 requestExportCsv(const QString& path);
    Q_INVOKABLE quint64 captureRowContext(int row);
    Q_INVOKABLE void releaseRowContext(quint64 token);
    Q_INVOKABLE quint64 requestCopyRawTransaction(quint64 context);
    static CsvResult WriteCsv(const CsvRequest& request);

Q_SIGNALS:
    void stateChanged();
    void exportChanged();
    void exportFinished(quint64 ticket, bool saved, const QString& path, const QString& error);
    void copyFinished(quint64 ticket, bool copied, const QString& error);
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
    void pendingBalanceChanged();
    void groupByChanged();

private:
    struct Parameters;
    struct Owner;
    struct Job;
    struct Mailbox;
    quint64 startExport(const QString& path, std::shared_ptr<const activityview::Hold> snapshot, int unit, bool captured);
    void reload();
    void start();
    void receive();
    void apply();
    void finishClose();
    void sourceChanged();
    static void step(const std::shared_ptr<Job>& job);
    static void patchRow(Owner& owner, const QString& id, activityview::SharedRow row);
    static bool typeMatches(const activityview::Row& row, TypeFilter type);
    static bool dateMatches(qint64 timestamp, const Parameters& parameters);
    static bool matches(const activityview::Row& row, const Parameters& parameters);
    static QString orderKey(const activityview::Row& row);
    static QString typeLabel(const activityview::Row& row);
    static activityview::Row present(const activityview::Row& row, const Parameters& parameters);
    Services m_services;
    execution::SnapshotServices m_execution;
    std::shared_ptr<Mailbox> m_mailbox;
    std::shared_ptr<Job> m_job;
    std::shared_ptr<const activityview::Hold> m_current, m_input, m_applied_input;
    activityview::Tree m_root;
    QHash<int, QByteArray> m_roles;
    QTimer m_start_timer, m_apply_timer, m_date_timer;
    QDate m_date_observed;
    WorkReport m_last_work;
    QPointer<QAbstractItemModel> m_deferred_source;
    bool m_source_deferred{false};
    quint64 m_generation{0}, m_settings_generation{0}, m_applied_settings{0}, m_source_intent{0};
    bool m_scheduled{false}, m_closed{false}, m_source_loading{false}, m_in_update{false};
    QString m_error, m_source_error;
    size_t m_patch_position{0};
    int m_transactions{0}, m_requests{0};
    CAmount m_pending_total{0};
    QString m_pending_text;
    std::shared_ptr<execution::OwnedCommand<CsvResult>> m_export;
    QPointer<ActivityExportOperation> m_export_operation;
    quint64 m_export_ticket{0}, m_copy_ticket{0};
    QString m_export_error, m_exported_path;
    bool m_export_rejected{false};
    ActivityExportResult m_export_result;
    std::shared_ptr<const activityview::Hold> m_chooser_snapshot;
    QPointer<QAbstractItemModel> m_chooser_source;
    QString m_chooser_token;
    quint64 m_chooser_sequence{0};
    int m_chooser_unit{0};
    std::shared_ptr<const activityview::Hold> m_context;
    QString m_context_txid;
    quint64 m_context_token{0};
    QString m_search_text;
    DateFilter m_date_filter{DateAll};
    QList<int> m_type_filters;
    int m_display_unit{0};
    CAmount m_min_amount{-1};
    CAmount m_max_amount{-1};
    CAmount m_available_max_amount{0};
    QList<QMetaObject::Connection> m_source_connections;
    QDate m_range_start;
    QDate m_range_end;
    GroupBy m_group_by{Month};
};

#endif // BITCOIN_QML_MODELS_ACTIVITYFILTERPROXYMODEL_H
