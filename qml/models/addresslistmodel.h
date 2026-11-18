// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_ADDRESSLISTMODEL_H
#define BITCOIN_QML_MODELS_ADDRESSLISTMODEL_H

#include <consensus/amount.h>
#include <qml/models/receiverequestoperation.h>
#include <qml/models/texteditsessionmodel.h>
#include <qml/snapshotquery.h>
#include <qml/walletopening.h>

#include <QAbstractListModel>
#include <QString>
#include <QVariantList>
#include <QPointer>

#include <exception>
#include <vector>

class WalletQmlModel;
namespace interfaces { class Wallet; }

class AddressListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(Category category READ category WRITE setCategory NOTIFY categoryChanged)
    Q_PROPERTY(QVariantList categoryOptions READ categoryOptions CONSTANT)
    Q_PROPERTY(bool showUsed READ showUsed WRITE setShowUsed NOTIFY showUsedChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString readError READ readError NOTIFY readErrorChanged)
    Q_PROPERTY(bool refreshPending READ refreshPending NOTIFY refreshStateChanged)

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
        CanCreatePaymentRequestRole
    };

    explicit AddressListModel(WalletQmlModel* parent = nullptr);
    ~AddressListModel() override;
    void close();
    QString readError() const { return m_read_error; }
    bool refreshPending() const;
    void configureSession(std::shared_ptr<walletopening::Session> opening, execution::SnapshotServices services);
    QModelIndex index(int row, int column = 0, const QModelIndex& parent = {}) const override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int count() const;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Category category() const;
    void setCategory(Category category);
    QVariantList categoryOptions() const;
    bool showUsed() const;
    void setShowUsed(bool show_used);
    void setDisplayUnit(int unit);

    Q_INVOKABLE void refresh();
    void refreshLabels();
    ReceiveRequestOperation* setAddressLabel(const QString& address, const QString& label);
    Q_INVOKABLE TextEditSessionModel* editLabel(const QString& address);
    Q_INVOKABLE TextEditSessionModel* labelEditor(const QString& address) const;
    Q_INVOKABLE QString addressAt(int row) const;

Q_SIGNALS:
    void categoryChanged();
    void showUsedChanged();
    void countChanged();
    void readErrorChanged();
    void refreshStateChanged();

private:
    void closeImpl(bool notify);
    void attachSession(std::shared_ptr<walletopening::Session> opening, execution::SnapshotServices services);
    struct AddressEntry {
        QString address;
        QString label;
        Category category{SingleUse};
        bool used{false};
        CAmount current_balance{0};
        QString script_type;
        bool can_edit_label{false};
        bool can_create_payment_request{false};
        QString formatted_address, ellipses_address, category_text, balance_text, display_amount;
        bool operator==(const AddressEntry&) const = default;
    };

    struct RowSlot;
    struct Node;
    struct Projection;
    struct LabelJob;
    void rebuild();
    void rebuildLabels();
    void cancelLabelJob();
    void notifyRefreshState();
    bool prepareLabelPublication(quint64 generation, bool failed);
    void publishLabels(std::shared_ptr<const Projection> projection, std::exception_ptr error, quint64 generation);
    void publishSnapshot();
    void applyTurn(quint64 generation);
    static std::vector<AddressEntry> collectEntries(interfaces::Wallet& wallet, Category category, bool show_used, int unit);
    std::shared_ptr<execution::SnapshotQuery<std::shared_ptr<const Projection>>> m_query;
    std::shared_ptr<LabelJob> m_label_job;
    std::shared_ptr<walletopening::Session> m_opening;
    execution::SnapshotServices m_services;
    std::shared_ptr<QObject> m_bridge;
    std::shared_ptr<const Projection> m_visible, m_pending;
    const Node* m_root{nullptr};
    size_t m_pending_step{0};
    quint64 m_query_generation{0};
    QString m_read_error;
    bool m_stopping{false}, m_dirty{true}, m_labels_dirty{false}, m_pending_labels{false}, m_refresh_pending{false}, m_inventory_failed{false}, m_notifying{false};
    WalletQmlModel* m_wallet_model{nullptr};
    QHash<QString, QPointer<TextEditSessionModel>> m_label_editors;
    Category m_category{SingleUse};
    bool m_show_used{false};
    int m_display_unit{0};
};

#endif // BITCOIN_QML_MODELS_ADDRESSLISTMODEL_H
