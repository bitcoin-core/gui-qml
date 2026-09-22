// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_COINSLISTMODEL_H
#define BITCOIN_QML_MODELS_COINSLISTMODEL_H

#include <interfaces/wallet.h>
#include <QAbstractListModel>
#include <QDateTime>
#include <QStringList>
#include <optional>

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
    Q_PROPERTY(QString changeAmount READ changeAmount NOTIFY selectedCoinsCountChanged)
    Q_PROPERTY(bool overRequiredAmount READ overRequiredAmount NOTIFY selectedCoinsCountChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY viewChanged)
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY viewChanged)
    Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY viewChanged)
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY viewChanged)
    Q_PROPERTY(QString groupBy READ groupBy WRITE setGroupBy NOTIFY viewChanged)
    Q_PROPERTY(qint64 availableMinAmount READ availableMinAmount NOTIFY coinCountChanged)
    Q_PROPERTY(qint64 availableMaxAmount READ availableMaxAmount NOTIFY coinCountChanged)
    Q_PROPERTY(qint64 minAmount READ minAmount WRITE setMinAmount NOTIFY viewChanged)
    Q_PROPERTY(qint64 maxAmount READ maxAmount WRITE setMaxAmount NOTIFY viewChanged)
public:
    explicit CoinsListModel(WalletQmlModel* parent = nullptr);
    enum CoinsRoles {
        AddressRole = Qt::UserRole + 1, AmountRole, DateTimeRole, LabelRole,
        LockedRole, SelectedRole, AmountSatoshiRole, CoinIdRole, GroupRole,
        GroupFirstRole, GroupLastRole,
    };
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int lockedCoinsCount() const;
    int selectedCoinsCount() const;
    int coinCount() const { return static_cast<int>(m_coins.size()); }
    QString totalSelected() const;
    qint64 totalSelectedSatoshi() const;
    qint64 totalSatoshi() const;
    qint64 lockedSatoshi() const;
    QString changeAmount() const;
    bool overRequiredAmount() const;
    QString searchText() const { return m_search; }
    QString sortBy() const { return m_sort; }
    bool sortDescending() const { return m_descending; }
    QString filter() const { return m_filter; }
    QString groupBy() const { return m_group; }
    qint64 availableMinAmount() const;
    qint64 availableMaxAmount() const;
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
    Q_INVOKABLE void beginSelection();
    Q_INVOKABLE void applySelection();
    Q_INVOKABLE void cancelSelection();
    Q_INVOKABLE bool setCoinsLocked(const QStringList& coin_ids, bool locked);
Q_SIGNALS:
    void lockedCoinsCountChanged();
    void selectedCoinsCountChanged();
    void coinCountChanged();
    void viewChanged();
private:
    struct Coin {
        COutPoint outpoint;
        QString address;
        QString note;
        CAmount amount;
        QDateTime date;
        bool locked;
        QString id() const;
    };
    void rebuildView();
    QString groupTitle(const Coin& coin) const;
    WalletQmlModel* m_wallet_model;
    std::vector<Coin> m_coins;
    std::vector<size_t> m_visible;
    std::optional<std::vector<COutPoint>> m_previous_selection;
    QString m_search;
    QString m_sort{"date"};
    QString m_filter{"all"};
    QString m_group{"date"};
    qint64 m_min_amount{-1};
    qint64 m_max_amount{-1};
    bool m_descending{true};
};
#endif
