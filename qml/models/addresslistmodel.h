// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_ADDRESSLISTMODEL_H
#define BITCOIN_QML_MODELS_ADDRESSLISTMODEL_H

#include <consensus/amount.h>

#include <QAbstractListModel>
#include <QString>
#include <QVariantList>

#include <vector>

class WalletQmlModel;

class AddressListModel : public QAbstractListModel
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
    Q_INVOKABLE bool setAddressLabel(const QString& address, const QString& label);
    Q_INVOKABLE QString addressAt(int row) const;

Q_SIGNALS:
    void categoryChanged();
    void showUsedChanged();
    void countChanged();

private:
    struct AddressEntry {
        QString address;
        QString label;
        Category category{SingleUse};
        bool used{false};
        CAmount current_balance{0};
        QString script_type;
        bool can_edit_label{false};
        bool can_create_payment_request{false};
    };

    void rebuild();
    std::vector<AddressEntry> collectEntries() const;

    WalletQmlModel* m_wallet_model{nullptr};
    std::vector<AddressEntry> m_entries;
    Category m_category{SingleUse};
    bool m_show_used{false};
    int m_display_unit{0};
};

#endif // BITCOIN_QML_MODELS_ADDRESSLISTMODEL_H
