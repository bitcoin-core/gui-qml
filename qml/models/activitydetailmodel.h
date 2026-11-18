// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_ACTIVITYDETAILMODEL_H
#define BITCOIN_QML_MODELS_ACTIVITYDETAILMODEL_H

#include <qml/models/activitysnapshot.h>
#include <qml/models/activitystore.h>
#include <qml/snapshotquery.h>
#include <QAbstractItemModel>
#include <QPointer>
#include <QVariantMap>

class TransactionActivityModel;
class ActivityValueListModel;

/** One observer's detail interest; history and authority belong to the original
* opening. Closing a view never changes another observer's selection. */
class ActivityDetailModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString transactionId READ transactionId WRITE setTransactionId NOTIFY transactionIdChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(State state READ state NOTIFY transactionDetailsChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY transactionDetailsChanged)
    Q_PROPERTY(QString error READ error NOTIFY transactionDetailsChanged)
    Q_PROPERTY(QVariantMap selectedTransactionDetails READ selectedTransactionDetails NOTIFY transactionDetailsChanged)
    Q_PROPERTY(QAbstractItemModel* detailInputs READ detailInputs CONSTANT)
    Q_PROPERTY(QAbstractItemModel* detailOutputs READ detailOutputs CONSTANT)
    Q_PROPERTY(QAbstractItemModel* detailActions READ detailActions CONSTANT)
    Q_PROPERTY(QAbstractItemModel* rawTransactionLines READ rawTransactionLines CONSTANT)
    Q_PROPERTY(QAbstractItemModel* entryDocument READ entryDocument CONSTANT)
    Q_PROPERTY(bool entryDetailsBusy READ entryDetailsBusy NOTIFY entryDetailsChanged)
    Q_PROPERTY(QString entryDetailsError READ entryDetailsError NOTIFY entryDetailsChanged)
public:
    enum State { Inactive, Idle, Loading, Ready, Failed, Closed };
    Q_ENUM(State)
    explicit ActivityDetailModel(TransactionActivityModel& source, QObject* owner);
    ~ActivityDetailModel() override;
    QString transactionId() const { return m_txid; }
    bool active() const { return m_active_interest && !m_closed; }
    State state() const {
        if (m_closed) return Closed;
        if (!active()) return Inactive;
        if (m_pending) return Loading;
        if (!m_error.isEmpty()) return Failed;
        return m_full && hasCurrentSnapshot() ? Ready : Idle;
    }
    bool pending() const { return m_pending && active(); }
    QString error() const { return m_error; }
    void setTransactionId(const QString& txid);
    void setActive(bool active);
    QVariantMap selectedTransactionDetails() const;
    QAbstractItemModel* detailInputs();
    QAbstractItemModel* detailOutputs();
    QAbstractItemModel* detailActions();
    QAbstractItemModel* rawTransactionLines();
    QAbstractItemModel* entryDocument();
    bool entryDetailsBusy() const { return bool(m_entry_operation) && !m_closed; }
    QString entryDetailsError() const { return m_entry_error; }
    Q_INVOKABLE void reload();
    Q_INVOKABLE quint64 requestEntryDetails(const QString& side, int row);
    Q_INVOKABLE quint64 requestCopyRawTransaction(const QString& txid);
    void close();
    void retryPublications();
Q_SIGNALS:
    void transactionIdChanged();
    void activeChanged();
    void transactionDetailsChanged();
    void entryDetailsChanged();
    void copyFinished(quint64 ticket, bool copied, const QString& error);
private:
    struct Input;
    struct Projection { quint64 generation{0}; quint64 transaction_revision{0};
        std::shared_ptr<const activityview::Hold> hold; };
    void refresh();
    void start();
    void startPreview();
    std::shared_ptr<Input> captureInput();
    static Projection prepare(const std::shared_ptr<Input>& input);
    void publish();
    void publishPreview();
    void publishEntry();
    bool publishRows(bool quiet = false);
    void clearRows(bool quiet);
    bool hasCurrentSnapshot() const {
        return m_hold && m_hold->snapshot && m_hold->snapshot->selected_txid == m_txid;
    }
    QPointer<TransactionActivityModel> m_source;
    const std::shared_ptr<walletopening::Session> m_opening;
    const std::function<void(std::function<void()>)> m_post;
    std::shared_ptr<execution::OwnedCommand<Projection>> m_operation;
    std::shared_ptr<execution::OwnedCommand<Projection>> m_preview_operation;
    std::shared_ptr<execution::OwnedCommand<Projection>> m_entry_operation;
    std::shared_ptr<const activityview::Hold> m_hold;
    std::unique_ptr<ActivityValueListModel> m_inputs, m_outputs, m_actions, m_raw, m_document;
    QString m_txid, m_error, m_entry_error;
    quint64 m_generation{0}, m_entry_generation{0}, m_ticket{0}, m_operation_native_revision{0};
    bool m_closed{false}, m_active_interest{false}, m_pending{false}, m_full{false}, m_again{false}, m_publishing{false}, m_refresh_queued{false};
};
#endif // BITCOIN_QML_MODELS_ACTIVITYDETAILMODEL_H
