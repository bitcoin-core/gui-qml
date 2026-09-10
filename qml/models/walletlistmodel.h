// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETLISTMODEL_H
#define BITCOIN_QML_MODELS_WALLETLISTMODEL_H

#include <interfaces/wallet.h>
#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QPair>
#include <QSet>
#include <QString>

#include <optional>

namespace interfaces {
class Node;
}

class WalletListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool walletDirLoaded READ walletDirLoaded NOTIFY walletDirLoadedChanged)

public:
    enum class LoadState {
        Closed    = 0,
        Open      = 1,
        Loading   = 2,
        LoadError = 3,
    };
    Q_ENUM(LoadState)

    WalletListModel(interfaces::Node& node, QObject *parent = nullptr);
    ~WalletListModel() = default;

    enum Roles {
        NameRole = Qt::UserRole + 1,
        FormatRole,
        DisplayNameRole,
        LoadStateRole,
        ErrorMessageRole,
        BalanceRole,
        KeySchemeKindRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool walletDirLoaded() const { return m_wallet_dir_loaded; }

Q_SIGNALS:
    void walletListChanged(bool has_wallets);
    void walletDirLoadedChanged();

public Q_SLOTS:
    void listWalletDir();
    void setWalletLoadState(const QString& name, LoadState state, const QString& error = {});
    void setDisplayUnit(int unit);
    void setWalletInfo(const QString& name, qint64 balance, int keySchemeKind);
    void refreshDisplayNames();

private:
    struct Item {
        QString name;
        QString format;
        bool from_wallet_dir{false};
        std::optional<qint64> balance;
        int keySchemeKind{0};   // 0 == WalletQmlModel::KeyScheme::SingleKey
    };

    bool itemLess(const Item& a, const Item& b) const;
    void sortItems(QList<Item>& items) const;
    bool applyUpdatedItems(QList<Item>&& updated_items);
    void updateLoadStateForAllRows();
    void emitTransientStateChanged();
    int rowForName(const QString& name) const;

    int m_display_unit{0};
    QList<Item> m_items;
    QSet<QString> m_open_wallet_names;
    QString m_loading_wallet;
    QPair<QString, QString> m_load_error;
    interfaces::Node& m_node;
    bool m_wallet_dir_loaded{false};
};

#endif // BITCOIN_QML_MODELS_WALLETLISTMODEL_H
