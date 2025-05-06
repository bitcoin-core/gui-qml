// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_COINSLISTMODEL_H
#define BITCOIN_QML_MODELS_COINSLISTMODEL_H

#include <consensus/amount.h>
#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <qml/models/transaction.h>

#include <QAbstractListModel>
#include <QString>

class WalletQmlModel;

class CoinsListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int lockedCoinsCount READ lockedCoinsCount NOTIFY lockedCoinsCountChanged)
    Q_PROPERTY(int selectedCoinsCount READ selectedCoinsCount NOTIFY selectedCoinsCountChanged)
    Q_PROPERTY(int coinCount READ coinCount NOTIFY coinCountChanged)
    Q_PROPERTY(QString totalSelected READ totalSelected NOTIFY selectedCoinsCountChanged)
    Q_PROPERTY(QString changeAmount READ changeAmount NOTIFY selectedCoinsCountChanged)
    Q_PROPERTY(bool overRequiredAmount READ overRequiredAmount NOTIFY selectedCoinsCountChanged)

public:
    explicit CoinsListModel(WalletQmlModel* parent = nullptr);
    ~CoinsListModel();

    enum CoinsRoles {
        AddressRole = Qt::UserRole + 1,
        AmountRole,
        DateTimeRole,
        LabelRole,
        LockedRole,
        SelectedRole
    };

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

public Q_SLOTS:
    void update();
    void setSortBy(const QString& roleName);
    void toggleCoinSelection(const int index);
    unsigned int lockedCoinsCount() const;
    unsigned int selectedCoinsCount() const;
    QString totalSelected() const;
    QString changeAmount() const;
    bool overRequiredAmount() const;
    int coinCount() const;

Q_SIGNALS:
    void sortByChanged(const QString& roleName);
    void lockedCoinsCountChanged();
    void selectedCoinsCountChanged();
    void coinCountChanged();

private:
    WalletQmlModel* m_wallet_model;
    std::unique_ptr<interfaces::Handler> m_handler_transaction_changed;
    std::vector<std::tuple<CTxDestination, COutPoint, interfaces::WalletTxOut>> m_coins;
    QString m_sort_by;
    CAmount m_total_amount;
};

#endif // BITCOIN_QML_MODELS_COINSLISTMODEL_H