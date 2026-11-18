// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_FLOWDETAILSMODEL_H
#define BITCOIN_QML_MODELS_FLOWDETAILSMODEL_H
#include <qml/snapshotquery.h>
#include <QAbstractListModel>
#include <QPointer>
#include <QVariantMap>
#include <atomic>

struct FlowDetailSnapshot;
struct FlowDetailInput;
struct FlowDetailProjection { quint64 generation{0}; std::shared_ptr<const FlowDetailSnapshot> snapshot; };
class FlowDetailRowsModel final : public QAbstractListModel
{
public:
    enum class Kind { Inputs, Outputs, Recipients, Document };
    FlowDetailRowsModel(Kind kind, std::shared_ptr<const std::atomic_bool> closed, QObject* parent);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "entry"}}; }
    void setSnapshot(std::shared_ptr<const FlowDetailSnapshot> snapshot, bool quiet = false);
private:
    const QVariantList* values() const;
    quint64 m_sequence{0};
    bool m_resetting{false};
    const Kind m_kind;
    const std::shared_ptr<const std::atomic_bool> m_closed;
    std::shared_ptr<const FlowDetailSnapshot> m_snapshot;
};

/** Original review adapter. Root composition passes opaque native owners; QML
 * receives only bounded rows and virtualized complete-entry documents. */
class FlowDetailsModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* context READ context NOTIFY snapshotChanged)
    Q_PROPERTY(quint64 revision READ revision NOTIFY snapshotChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY snapshotChanged)
    Q_PROPERTY(QString error READ error NOTIFY snapshotChanged)
    Q_PROPERTY(QAbstractItemModel* inputRows READ inputRows CONSTANT)
    Q_PROPERTY(QAbstractItemModel* outputRows READ outputRows CONSTANT)
    Q_PROPERTY(QAbstractItemModel* recipientRows READ recipientRows CONSTANT)
    Q_PROPERTY(QAbstractItemModel* entryDocument READ entryDocument CONSTANT)
    Q_PROPERTY(bool entryDetailsBusy READ entryDetailsBusy NOTIFY entryDetailsChanged)
    Q_PROPERTY(QString entryDetailsError READ entryDetailsError NOTIFY entryDetailsChanged)
public:
    explicit FlowDetailsModel(QObject* parent = nullptr);
    ~FlowDetailsModel() override;
    void setServices(execution::SnapshotServices services);
    void publishSource(const QVariantMap& flow, const QVariantList& recipients, QObject* context,
                       quint64 revision, std::shared_ptr<const void> original_anchor = {}, int display_unit = 0);
    void clearSource();
    void closeAdmission();
    void retryPublications();
    QObject* context() const { return m_context; }
    quint64 revision() const { return m_revision; }
    bool pending() const { return !*m_closed && m_pending; }
    QString error() const { return m_error; }
    bool entryDetailsBusy() const { return !*m_closed && m_document_pending; }
    QString entryDetailsError() const { return m_document_error; }
    QAbstractItemModel* inputRows() { return &m_inputs; }
    QAbstractItemModel* outputRows() { return &m_outputs; }
    QAbstractItemModel* recipientRows() { return &m_recipients; }
    QAbstractItemModel* entryDocument() { return &m_document; }
    Q_INVOKABLE quint64 requestEntryDetails(const QString& side, int row, QObject* expected_context, quint64 expected_revision);
Q_SIGNALS:
    void snapshotChanged();
    void entryDetailsChanged();
private:
    struct State;
    void publishSnapshot();
    void publishDocument();
    void clearRows(bool quiet);
    const std::shared_ptr<std::atomic_bool> m_closed{std::make_shared<std::atomic_bool>(false)};
    execution::SnapshotServices m_services;
    std::shared_ptr<State> m_state;
    std::shared_ptr<execution::SnapshotQuery<FlowDetailProjection>> m_query;
    std::shared_ptr<execution::OwnedCommand<std::shared_ptr<const FlowDetailSnapshot>>> m_document_command;
    std::shared_ptr<const FlowDetailSnapshot> m_snapshot;
    FlowDetailRowsModel m_inputs{FlowDetailRowsModel::Kind::Inputs, m_closed, this};
    FlowDetailRowsModel m_outputs{FlowDetailRowsModel::Kind::Outputs, m_closed, this};
    FlowDetailRowsModel m_recipients{FlowDetailRowsModel::Kind::Recipients, m_closed, this};
    FlowDetailRowsModel m_document{FlowDetailRowsModel::Kind::Document, m_closed, this};
    QPointer<QObject> m_context;
    QMetaObject::Connection m_context_connection;
    quint64 m_revision{0}, m_generation{0}, m_document_ticket{0}, m_document_generation{0};
    bool m_pending{false}, m_document_pending{false};
    QString m_error, m_document_error;
};
#endif // BITCOIN_QML_MODELS_FLOWDETAILSMODEL_H
