// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_COINSLISTMODEL_H
#define BITCOIN_QML_MODELS_COINSLISTMODEL_H

#include <interfaces/wallet.h>
#include <qml/models/coinselection.h>
#include <qml/snapshotquery.h>
#include <qml/walletopening.h>
#include <QAbstractListModel>
#include <QDateTime>
#include <QStringList>
#include <deque>
#include <set>
#include <vector>

class WalletQmlModel;

class CoinsListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int lockedCoinsCount READ lockedCoinsCount NOTIFY coinCountChanged)
    Q_PROPERTY(int selectedCoinsCount READ selectedCoinsCount NOTIFY selectedCoinsCountChanged)
    Q_PROPERTY(int coinCount READ coinCount NOTIFY coinCountChanged)
    Q_PROPERTY(int visibleCount READ rowCount NOTIFY viewChanged)
    Q_PROPERTY(QString totalSelected READ totalSelected NOTIFY selectedCoinsCountChanged)
    Q_PROPERTY(qint64 totalSelectedSatoshi READ totalSelectedSatoshi NOTIFY selectedCoinsCountChanged)
    Q_PROPERTY(qint64 totalSatoshi READ totalSatoshi NOTIFY coinCountChanged)
    Q_PROPERTY(qint64 lockedSatoshi READ lockedSatoshi NOTIFY coinCountChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY viewChanged)
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY viewChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY viewChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY viewChanged)
    Q_PROPERTY(QString groupBy READ groupBy WRITE setGroupBy NOTIFY viewChanged)
    Q_PROPERTY(qint64 availableMinAmount READ availableMinAmount NOTIFY coinCountChanged)
    Q_PROPERTY(qint64 availableMaxAmount READ availableMaxAmount NOTIFY coinCountChanged)
    Q_PROPERTY(qint64 minAmount READ minAmount WRITE setMinAmount NOTIFY viewChanged)
    Q_PROPERTY(qint64 maxAmount READ maxAmount WRITE setMaxAmount NOTIFY viewChanged)
    Q_PROPERTY(QString readError READ readError NOTIFY readErrorChanged)
    Q_PROPERTY(QString lockError READ lockError NOTIFY lockOperationChanged)
    Q_PROPERTY(bool lockPending READ lockPending NOTIFY lockOperationChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY viewChanged)
    Q_PROPERTY(bool selectionPending READ selectionPending NOTIFY selectionSnapshotChanged)
    Q_PROPERTY(quint64 selectionRevision READ selectionRevision NOTIFY selectionSnapshotChanged)
    Q_PROPERTY(QString selectionToken READ selectionToken NOTIFY selectionSnapshotChanged)
    Q_PROPERTY(bool manualInputSelection READ manualInputSelection NOTIFY selectionSnapshotChanged)
    Q_PROPERTY(bool selectionReconciliationRequired READ selectionReconciliationRequired NOTIFY selectionSnapshotChanged)
    Q_PROPERTY(int missingSelectedCoins READ missingSelectedCoins NOTIFY selectionSnapshotChanged)
    Q_PROPERTY(int markedCount READ markedCount NOTIFY marksChanged)
    Q_PROPERTY(bool marksPending READ marksPending NOTIFY marksChanged)
public:
    using Selection = std::set<COutPoint>;
    struct LockItemOutcome {
        enum class State { Changed, Failed, Unknown, NotAttempted, AlreadyLocked };
        QString coin_id;
        COutPoint outpoint;
        State state{State::NotAttempted};
    };
    struct LockOutcome { bool failed{false}, uncertain{false}; size_t requested{0}, changed{0}; std::shared_ptr<const Selection> selection; quint64 selection_revision{0}; std::shared_ptr<const Selection> marks; quint64 marks_revision{0}; std::vector<LockItemOutcome> items; bool already_locked{false}; };
    struct MarkOutcome { std::shared_ptr<const Selection> marks; quint64 revision{0}; bool failed{false}; };
    struct SelectionOutcome { std::shared_ptr<const Selection> selection; quint64 revision{0}; bool failed{false}; coinselection::InputMode input_mode{coinselection::InputMode::Automatic}; bool editing{false}; };
    std::shared_ptr<const execution::OwnedCommand<LockOutcome>> lockOperation() const { return m_last_lock; }
    std::shared_ptr<const execution::OwnedCommand<SelectionOutcome>> selectionOperation() const { return m_last_selection; }
    explicit CoinsListModel(WalletQmlModel* parent = nullptr);
    ~CoinsListModel() override;
    void setOpening(std::shared_ptr<walletopening::Session> opening, execution::SnapshotServices services);
    void closeAdmission();
    void retryPublications();
    QString readError() const { return m_read_error; }
    QString lockError() const { return m_lock_error; }
    bool lockPending() const { return !m_lock_commands.empty(); }
    bool loading() const { return m_loading || m_incomplete || bool(m_target); }
    bool selectionPending() const { return m_selection_commands != 0 || lockPending(); }
    quint64 selectionRevision() const { return m_selection_revision; }
    QString selectionToken() const { return QString::number(m_selection_revision); }
    bool manualInputSelection() const { return m_input_mode == coinselection::InputMode::Manual; }
    bool selectionReconciliationRequired() const { return m_missing_selected_coins != 0; }
    int missingSelectedCoins() const { return m_missing_selected_coins; }
    int markedCount() const { return m_marked_count; }
    bool marksPending() const { return !m_mark_operations.empty(); }
    std::shared_ptr<const Selection> selectionSnapshot() const { return m_selection; }
    quint64 requestSelectionDelta(const COutPoint& outpoint, bool selected);
    quint64 requestReplaceSelection(std::shared_ptr<const Selection> selection);
    enum CoinsRoles { AddressRole = Qt::UserRole + 1, AmountRole, DateTimeRole, LabelRole,
        LockedRole, SelectedRole, AmountSatoshiRole, CoinIdRole, GroupRole, GroupFirstRole, GroupLastRole, MarkedRole };
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int lockedCoinsCount() const { return m_locked_count; }
    int selectedCoinsCount() const { return m_selected_count; }
    int coinCount() const { return m_coin_count; }
    QString totalSelected() const { return m_selected_text; }
    qint64 totalSelectedSatoshi() const { return m_selected_total; }
    qint64 totalSatoshi() const { return m_total; }
    qint64 lockedSatoshi() const { return m_locked_total; }
    QString searchText() const { return m_search; }
    QString sortBy() const { return m_sort; }
    bool sortDescending() const { return m_descending; }
    QString filter() const { return m_filter; }
    QString groupBy() const { return m_group; }
    qint64 availableMinAmount() const { return m_available_min; }
    qint64 availableMaxAmount() const { return m_available_max; }
    qint64 minAmount() const { return m_min_amount; }
    qint64 maxAmount() const { return m_max_amount; }
    void setSearchText(const QString& value);
    void setSortBy(const QString& value);
    void setSortDescending(bool value);
    void setFilter(const QString& value);
    void setGroupBy(const QString& value);
    void setMinAmount(qint64 value);
    void setMaxAmount(qint64 value);
    Q_INVOKABLE void update();
    Q_INVOKABLE void refreshSelection();
    Q_INVOKABLE void toggleCoinSelection(int index);
    Q_INVOKABLE bool toggleCoinSelectionById(const QString& coin_id);
    Q_INVOKABLE bool toggleMarkById(const QString& coin_id);
    Q_INVOKABLE bool clearMarks();
    Q_INVOKABLE bool lockMarkedCoins(bool locked);
    Q_INVOKABLE void beginSelection();
    Q_INVOKABLE void applySelection();
    Q_INVOKABLE void cancelSelection();
    Q_INVOKABLE bool setManualInputSelection(bool manual);
    // Admission receipt only; lockPending/lockError and lockOperation own the outcome.
    Q_INVOKABLE bool setCoinsLocked(const QStringList& coin_ids, bool locked);
Q_SIGNALS:
    void lockedCoinsCountChanged();
    void selectedCoinsCountChanged();
    void coinCountChanged();
    void viewChanged();
    void readErrorChanged();
    void lockOperationChanged();
    void selectionSnapshotChanged();
    void marksChanged();
private:
    enum class SelectionMode { Unselect, Select, Toggle, Replace, Begin, Cancel, Apply, AutomaticInputs, ManualInputs };
    struct Coin;
    struct Controls;
    struct View;
    struct Backend;
    struct Rows;
    struct Retired;
    void retireValues();
    bool m_changing_rows{false};
    using LockResult = LockOutcome;
    using SelectionResult = SelectionOutcome;
    struct LockCommand { std::shared_ptr<execution::OwnedCommand<LockResult>> operation; };
    std::deque<LockCommand> m_lock_commands;
    std::deque<std::shared_ptr<execution::OwnedCommand<MarkOutcome>>> m_mark_operations;
    std::shared_ptr<execution::OwnedCommand<LockResult>> m_last_lock;
    std::shared_ptr<execution::OwnedCommand<SelectionResult>> m_last_selection;
    std::deque<std::shared_ptr<execution::OwnedCommand<SelectionResult>>> m_selection_operations;
    std::shared_ptr<Backend> m_backend;
    std::shared_ptr<Retired> m_retired;
    std::shared_ptr<Rows> m_rows;
    std::shared_ptr<const View> m_target, m_published_view;
    std::shared_ptr<const Selection> m_selection{std::make_shared<const Selection>()};
    std::shared_ptr<walletopening::Session> m_opening;
    execution::SnapshotServices m_services;
    WalletQmlModel* m_wallet_model;
    bool m_stopping{false}, m_loading{false}, m_incomplete{false}, m_descending{true};
    bool m_selection_editing{false};
    int m_coin_count{0}, m_locked_count{0}, m_selected_count{0}, m_selection_commands{0}, m_marked_count{0};
    int m_missing_selected_coins{0};
    qint64 m_total{0}, m_locked_total{0}, m_selected_total{0}, m_available_min{0}, m_available_max{0};
    quint64 m_mark_revision{0}, m_view_revision{0}, m_selection_revision{0}, m_selection_ticket{0}, m_reconcile_turn{0}, m_setup_revision{0};
    coinselection::InputMode m_input_mode{coinselection::InputMode::Automatic};
    int m_remove_cursor{-1}, m_insert_cursor{0};
    QString m_read_error, m_lock_error, m_selected_text;
    QString m_search, m_sort{"date"}, m_filter{"all"}, m_group{"date"};
    qint64 m_min_amount{-1}, m_max_amount{-1};
    void publishView();
    void reconcile(quint64 turn);
    void publishLocks();
    void publishSelections();
    void publishMarks();
    bool markCommand(std::optional<COutPoint> outpoint);
    bool lockCoins(const QStringList& ids, bool locked, bool marked);
    void rebuildView();
    quint64 selectionCommand(std::optional<COutPoint> outpoint, SelectionMode mode, std::shared_ptr<const Selection> replacement = {});
};
#endif // BITCOIN_QML_MODELS_COINSLISTMODEL_H
