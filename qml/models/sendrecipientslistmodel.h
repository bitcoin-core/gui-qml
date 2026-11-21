// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_SENDRECIPIENTSLISTMODEL_H
#define BITCOIN_QML_MODELS_SENDRECIPIENTSLISTMODEL_H
#include <atomic>
#include <functional>

#include <qml/models/sendrecipient.h>
#include <qml/indexedsequence.h>
#include <QAbstractListModel>
#include <QPointer>
#include <map>
#include <deque>
#include <vector>

namespace paymentimport { struct TargetPort; }

class WalletQmlModel;
class SendRecipientsListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString draftId READ draftId CONSTANT)
    Q_PROPERTY(QString revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(DraftState state READ state NOTIFY validationChanged)
    Q_PROPERTY(int maximumRecipients READ maximumRecipients CONSTANT)
    Q_PROPERTY(bool canAddRecipient READ canAddRecipient NOTIFY validationChanged)
    Q_PROPERTY(QString budgetError READ budgetError NOTIFY budgetErrorChanged)
    Q_PROPERTY(SendRecipient* maximumRecipient READ maximumRecipient NOTIFY maximumRecipientChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(SendRecipient* current READ currentRecipient NOTIFY currentRecipientChanged)
    Q_PROPERTY(QString totalAmount READ totalAmount NOTIFY totalAmountChanged)
    Q_PROPERTY(qint64 totalAmountSatoshi READ totalAmountSatoshi NOTIFY totalAmountChanged)
    Q_PROPERTY(bool allValid READ allValid NOTIFY validationChanged)
    Q_PROPERTY(QString validationError READ validationError NOTIFY validationChanged)
    Q_PROPERTY(bool validationPending READ validationPending NOTIFY validationChanged)
    Q_PROPERTY(bool importPending READ importPending NOTIFY validationChanged)
public:
    using DraftSnapshot = senddraft::Snapshot;
    enum class DraftState { Empty, Editing, Ready, Retired };
    Q_ENUM(DraftState)
    enum Roles { RecipientIdRole = Qt::UserRole + 20, AddressRole = Qt::UserRole + 1, RecipientRole, LabelRole, AmountRole, MessageRole, FormattedAddressRole, AmountUnitLabelRole };
    explicit SendRecipientsListModel(QObject* parent = nullptr, senddraft::Limits limits = {});
    QString draftId() const { return QString::number(m_identity); }
    QString revision() const { return QString::number(m_intent_revision); }
    DraftState state() const;
    int maximumRecipients() const { return m_limits.recipients; }
    bool canAddRecipient() const { return !m_closed && !importPending() && count() < maximumRecipients() && m_pending_row_creations < senddraft::Limits::pending_recipients; }
    QString budgetError() const { return m_command_error; }
    SendRecipient* maximumRecipient() const { return m_maximum_recipient; }
    void chooseAmountMode(SendRecipient* recipient, SendRecipient::AmountMode mode);
    Q_INVOKABLE void removeRecipient(const QString& recipient_id);
    ~SendRecipientsListModel() override;
    void setOpening(std::shared_ptr<walletopening::Session> opening, execution::SnapshotServices services);
    void close();
    void retryPublications();
    std::shared_ptr<const DraftSnapshot> draftSnapshot() const;
    std::shared_ptr<const paymentimport::TargetPort> importTarget(SendRecipient* recipient);
    bool importPending() const { return m_import_pending->load(); }
    quint64 recipientToken(const SendRecipient* recipient) const { return recipient ? recipient->identity() : 0; }
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE void add();
    Q_INVOKABLE void next();
    Q_INVOKABLE void prev();
    Q_INVOKABLE void remove();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void clearToFront();
    int currentIndex() const { return m_current + 1; }
    Q_INVOKABLE void setCurrentIndex(int row);
    Q_INVOKABLE void removeAt(int row);
    SendRecipient* currentRecipient() const;
    int count() const { return m_recipients->size(); }
    // Test/legacy C++ inspection only. Production capture uses draftSnapshot().
    QList<SendRecipient*> recipients() const;
    QString totalAmount() const { return m_total_text; }
    qint64 totalAmountSatoshi() const { return m_totalAmount; }
    bool validationPending() const { return !m_closed && (m_pending || m_initializing || m_clearing || m_units_pending || (!m_failed && m_snapshot && m_snapshot->revision == m_revision && m_snapshot->pending_count > 0)); }
    bool allValid() const { return !m_closed && !importPending() && !m_failed && !validationPending() && m_snapshot && m_snapshot->revision == m_revision && m_snapshot->all_valid; }
    QString validationError() const { return m_error; }
Q_SIGNALS:
    void intentSubmitted();
    void revisionChanged();
    void budgetErrorChanged();
    void commandRejected(const QString& error);
    void maximumRecipientChanged();
    void currentIndexChanged();
    void currentRecipientChanged();
    void countChanged();
    void totalAmountChanged();
    void listCleared();
    void validationChanged();
    void recipientDraftChanged(quint64 token, quint64 validation_revision);
private:
    using Rows = IndexedSequence<SendRecipient*>;
    struct Backend;
    struct Retired;
    static void retireClosedRows(std::shared_ptr<Retired> retired);
    void retireValues();
    bool m_changing_rows{false};
    void connectRecipientSignals(SendRecipient* recipient);
    int recipientRow(const SendRecipient* recipient) const;
    void submitRecipient(SendRecipient* recipient);
    void submitIntent();
    void removeDraft(quint64 token);
    void publishDraft();
    bool projectImport(const senddraft::AppliedImport& value, quint64 revision);
    void initializeRecipients(int row);
    void retryRecipients(int row);
    void synchronizeUnits(BitcoinAmount::Unit unit, int row, quint64 generation);
    void retireRecipients(std::shared_ptr<Rows> rows);
    void trimToFront(quint64 generation);
    SendRecipient* createRecipient();
    WalletQmlModel* m_wallet;
    const senddraft::Limits m_limits;
    size_t m_pending_row_creations{0};
    const quint64 m_identity;
    QPointer<SendRecipient> m_maximum_recipient;
    QString m_command_error;
    void rejectCommand(const QString& error);
    bool clearCommandError();
    std::shared_ptr<QObject> m_recipient_owner;
    std::shared_ptr<std::atomic_bool> m_owner_closed{std::make_shared<std::atomic_bool>(false)};
    std::shared_ptr<std::atomic_bool> m_import_pending{std::make_shared<std::atomic_bool>(false)};
    quint64 m_import_owner{0}, m_import_child{0};
    bool m_projecting_import{false};
    std::function<void(std::function<void()>)> m_retirement_post;
    std::shared_ptr<Retired> m_retired;
    std::shared_ptr<Rows> m_empty_rows{std::make_shared<Rows>()};
    std::shared_ptr<Rows> m_recipients{std::make_shared<Rows>()};
    std::shared_ptr<Backend> m_backend;
    std::shared_ptr<walletopening::Session> m_opening;
    execution::SnapshotServices m_services;
    std::shared_ptr<const DraftSnapshot> m_snapshot;
    std::shared_ptr<execution::SnapshotQuery<std::shared_ptr<const DraftSnapshot>>> m_query;
    int m_current{0}, m_initialize_row{0}, m_unit_row{0};
    BitcoinAmount::Unit m_pending_unit{BitcoinAmount::Unit::BTC};
    qint64 m_totalAmount{0};
    quint64 m_revision{0}, m_intent_revision{0}, m_epoch{0}, m_unit_generation{0}, m_clear_generation{0}, m_setup_revision{0};
    QString m_total_text, m_error;
    bool m_syncing_units{false}, m_pending{true}, m_closed{false}, m_initializing{false}, m_clearing{false}, m_retrying{false}, m_units_pending{false}, m_failed{false};
};
#endif // BITCOIN_QML_MODELS_SENDRECIPIENTSLISTMODEL_H
