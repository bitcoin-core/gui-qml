// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_NATIVELISTMODEL_H
#define BITCOIN_QML_NATIVELISTMODEL_H
#include <qml/snapshotquery.h>
#include <QAbstractProxyModel>
#include <QHash>
#include <QVariant>
#include <functional>
#include <memory>
#include <vector>
class BackendExecutor;
struct NativeListRow {
    QString key;
    QHash<int, QVariant> values;
    std::shared_ptr<const void> payload;
};
using NativeRowHandle = std::shared_ptr<const NativeListRow>;
struct NativeListVersion;
using NativeVersionHandle = std::shared_ptr<const NativeListVersion>;
/** Native inventories and persistent delta plans; GUI applies at most 32 Qt
 * notices per turn and performs logarithmic row bookkeeping. Aliases retain a
 * reserved cleanup lease, so final payload/tree destruction never runs on GUI.
 */
class NativeListModel : public QAbstractProxyModel
{
    Q_OBJECT
    Q_PROPERTY(bool projectionPending READ projectionPending NOTIFY projectionChanged)
    Q_PROPERTY(QString readError READ readError NOTIFY readErrorChanged)
public:
    using Read = std::function<std::vector<NativeRowHandle>()>;
    explicit NativeListModel(execution::SnapshotServices services, QObject* parent = nullptr);
    ~NativeListModel() override;
    QModelIndex parent(const QModelIndex&) const override { return {}; }
    int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : 1; }
    QModelIndex mapToSource(const QModelIndex&) const override { return {}; }
    QModelIndex mapFromSource(const QModelIndex&) const override { return {}; }
    int rowCount(const QModelIndex& parent = {}) const override;
    QModelIndex index(int row, int column = 0, const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override { return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags; }
    bool hasChildren(const QModelIndex& parent = {}) const override { return !parent.isValid() && rowCount() > 0; }
    bool projectionPending() const { return m_pending; }
    QString readError() const { return m_error; }
    virtual void closeAdmission();
    std::shared_ptr<execution::Scheduler> scheduler() const { return m_services.scheduler; }
    execution::SnapshotServices services() const { return m_services; }
    NativeVersionHandle completedSnapshot() const { return m_completed; }
    NativeRowHandle rowHandle(const QModelIndex& index) const;
    NativeRowHandle rowForKey(const QString& key) const;
    int indexForKey(const QString& key) const;
    // Worker/test-setup only. GUI must never materialize a whole inventory.
    static std::vector<NativeRowHandle> Rows(const NativeVersionHandle& version);
Q_SIGNALS:
    void projectionChanged();
    void readErrorChanged();
    void snapshotReady();
protected:
    void configureServices(execution::SnapshotServices services);
    void setRead(Read read, execution::Lane lane = execution::Lane::Core);
    void refreshNative();
    void invalidateProjection();
    bool stopping() const { return m_stopping; }
    virtual void rowUpdated(const QString&, const NativeRowHandle&) {}
    virtual void rowRemoved(const QString&) {}
private:
    struct Input;
    struct View;
    struct Plan;
    using PlanHandle = std::shared_ptr<const Plan>;
    void publishQuery();
    void acceptPlan(PlanHandle plan);
    bool stagePlan(PlanHandle plan);
    void finishClose();
    void rebase(PlanHandle target);
    void applyTurn(quint64 generation);
    void setError(const QString& error);
    std::shared_ptr<Input> m_input;
    std::shared_ptr<View> m_view;
    std::shared_ptr<execution::SnapshotQuery<PlanHandle>> m_query;
    std::shared_ptr<execution::OwnedCommand<PlanHandle>> m_rebase;
    PlanHandle m_plan, m_deferred;
    NativeVersionHandle m_version, m_completed;
    execution::SnapshotServices m_services;
    std::unique_ptr<BackendExecutor> m_executor;
    std::shared_ptr<QObject> m_bridge;
    Read m_read;
    execution::Lane m_lane{execution::Lane::Core};
    QString m_error;
    quint64 m_generation{0};
    size_t m_step{0};
    bool m_pending{false}, m_stopping{false}, m_notifying{false}, m_applying{false};
};
#endif // BITCOIN_QML_NATIVELISTMODEL_H
